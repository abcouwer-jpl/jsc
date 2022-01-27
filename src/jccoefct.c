/*
 * jccoefct.c
 *
 * Copyright (C) 1994-1997, Thomas G. Lane.
 * Modified 2003-2011 by Guido Vollbeding.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
 *
 * This file contains the coefficient buffer controller for compression.
 * This controller is the top level of the JPEG compressor proper.
 * The coefficient buffer lies between forward-DCT and entropy encoding steps.
 */

#include "jsc/jpegint.h"

// Abcouwer JSC 2021 - no full coef buffer

/* Private buffer controller object */

typedef struct {
  struct jpeg_c_coef_controller pub; /* public fields */

  JDIMENSION iMCU_row_num;        /* iMCU row # within image */
  JDIMENSION mcu_ctr;                /* counts MCUs processed in current row */
  JINT MCU_vert_offset;                /* counts MCU rows within iMCU row */
  JINT MCU_rows_per_iMCU_row;        /* number of such rows needed */

  /* For single-pass compression, it's sufficient to buffer just one MCU
   * (although this may prove a bit slow in practice).  We allocate a
   * workspace of C_MAX_BLOCKS_IN_MCU coefficient blocks, and reuse it for each
   * MCU constructed and sent.  (On 80x86, the workspace is FAR even though
   * it's not really very big; this is to keep the module interfaces unchanged
   * when a large coefficient buffer is necessary.)
   * In multi-pass modes, this array points to the current MCU's blocks
   * within the virtual arrays.
   */
  JBLOCKROW MCU_buffer[C_MAX_BLOCKS_IN_MCU];

  // Abcouwer JSC 2021 - remove virtual arrays
//  /* In multi-pass modes, we need a virtual block array for each component. */
//  jvirt_barray_ptr whole_image[MAX_COMPONENTS];
} my_coef_controller;

typedef my_coef_controller * my_coef_ptr;


/* Forward declarations */
JSC_METHODDEF(boolean) compress_data
    JPP((j_compress_ptr cinfo, JSAMPIMAGE input_buf));

// Abcouwer JSC 2021 - no FULL_COEF_BUFFER_SUPPORTED

JSC_LOCAL(void)
start_iMCU_row (j_compress_ptr cinfo)
/* Reset within-iMCU-row counters for a new row */
{
  JSC_ASSERT(cinfo != NULL);

  my_coef_ptr coef = (my_coef_ptr) cinfo->coef;

  /* In an interleaved scan, an MCU row is the same as an iMCU row.
   * In a noninterleaved scan, an iMCU row has v_samp_factor MCU rows.
   * But at the bottom of the image, process only what's left.
   */
  if (cinfo->comps_in_scan > 1) {
    coef->MCU_rows_per_iMCU_row = 1;
  } else {
    if (coef->iMCU_row_num < (cinfo->total_iMCU_rows-1))
      coef->MCU_rows_per_iMCU_row = cinfo->cur_comp_info[0]->v_samp_factor;
    else
      coef->MCU_rows_per_iMCU_row = cinfo->cur_comp_info[0]->last_row_height;
  }

  coef->mcu_ctr = 0;
  coef->MCU_vert_offset = 0;
}


/*
 * Initialize for a processing pass.
 */

JSC_METHODDEF(void)
start_pass_coef (j_compress_ptr cinfo, J_BUF_MODE pass_mode)
{
    JSC_ASSERT(cinfo != NULL);
    JSC_ASSERT_2(pass_mode == JBUF_PASS_THRU,
            pass_mode, JBUF_PASS_THRU);
    my_coef_ptr coef = (my_coef_ptr) cinfo->coef;

    JSC_ASSERT(coef != NULL);
    coef->iMCU_row_num = 0;
    start_iMCU_row(cinfo);

    // Abcouwer ZSC 2021 - remove virtual arrays
    coef->pub.compress_data = compress_data;
    // Abcouwer JSC 2021 - no FULL_COEF_BUFFER_SUPPORTED
}


/*
 * Process some data in the single-pass case.
 * We process the equivalent of one fully interleaved MCU row ("iMCU" row)
 * per call, ie, v_samp_factor block rows for each component in the image.
 * Returns TRUE if the iMCU row is completed, FALSE if suspended.
 *
 * NB: input_buf contains a plane for each component in image,
 * which we index according to the component's SOF position.
 */

JSC_METHODDEF(boolean)
compress_data (j_compress_ptr cinfo, JSAMPIMAGE input_buf)
{
  my_coef_ptr coef = (my_coef_ptr) cinfo->coef;
  JDIMENSION MCU_col_num;        /* index of current MCU within row */
  JDIMENSION last_MCU_col = cinfo->MCUs_per_row - 1;
  JDIMENSION last_iMCU_row = cinfo->total_iMCU_rows - 1;
  JINT blkn, bi, ci, yindex, yoffset, blockcnt;
  JDIMENSION ypos, xpos;
  jpeg_component_info *compptr;
  forward_DCT_ptr forward_DCT;

  /* Loop to write as much as one whole iMCU row */
  for (yoffset = coef->MCU_vert_offset; yoffset < coef->MCU_rows_per_iMCU_row;
       yoffset++) {
    for (MCU_col_num = coef->mcu_ctr; MCU_col_num <= last_MCU_col;
         MCU_col_num++) {
      /* Determine where data comes from in input_buf and do the DCT thing.
       * Each call on forward_DCT processes a horizontal row of DCT blocks
       * as wide as an MCU; we rely on having allocated the MCU_buffer[] blocks
       * sequentially.  Dummy blocks at the right or bottom edge are filled in
       * specially.  The data in them does not matter for image reconstruction,
       * so we fill them with values that will encode to the smallest amount of
       * data, viz: all zeroes in the AC entries, DC entries equal to previous
       * block's DC value.  (Thanks to Thomas Kinsman for this idea.)
       */
      blkn = 0;
      for (ci = 0; ci < cinfo->comps_in_scan; ci++) {
        compptr = cinfo->cur_comp_info[ci];
        forward_DCT = cinfo->fdct->forward_DCT[compptr->component_index];
        blockcnt = (MCU_col_num < last_MCU_col) ? compptr->MCU_width
                                                : compptr->last_col_width;
        xpos = MCU_col_num * compptr->MCU_sample_width;
        ypos = yoffset * compptr->DCT_v_scaled_size;
        /* ypos == (yoffset+yindex) * DCTSIZE */
        for (yindex = 0; yindex < compptr->MCU_height; yindex++) {
          if (coef->iMCU_row_num < last_iMCU_row ||
              yoffset+yindex < compptr->last_row_height) {
            (*forward_DCT) (cinfo, compptr,
                            input_buf[compptr->component_index],
                            coef->MCU_buffer[blkn],
                            ypos, xpos, (JDIMENSION) blockcnt);
            if (blockcnt < compptr->MCU_width) {
              /* Create some dummy blocks at the right edge of the image. */
              JSC_FMEMZERO((void *) coef->MCU_buffer[blkn + blockcnt],
                       (compptr->MCU_width - blockcnt) * SIZEOF(JBLOCK));
              for (bi = blockcnt; bi < compptr->MCU_width; bi++) {
                coef->MCU_buffer[blkn+bi][0][0] = coef->MCU_buffer[blkn+bi-1][0][0];
              }
            }
          } else {
            /* Create a row of dummy blocks at the bottom of the image. */
            JSC_FMEMZERO((void *) coef->MCU_buffer[blkn],
                     compptr->MCU_width * SIZEOF(JBLOCK));
            for (bi = 0; bi < compptr->MCU_width; bi++) {
              coef->MCU_buffer[blkn+bi][0][0] = coef->MCU_buffer[blkn-1][0][0];
            }
          }
          blkn += compptr->MCU_width;
          ypos += compptr->DCT_v_scaled_size;
        }
      }
      /* Try to write the MCU.  In event of a suspension failure, we will
       * re-DCT the MCU on restart (a bit inefficient, could be fixed...)
       */
      if (! (*cinfo->entropy->encode_mcu) (cinfo, coef->MCU_buffer)) {
        /* Suspension forced; update state counters and exit */
        coef->MCU_vert_offset = yoffset;
        coef->mcu_ctr = MCU_col_num;
        return FALSE;
      }
    }
    /* Completed an MCU row, but perhaps not an iMCU row */
    coef->mcu_ctr = 0;
  }
  /* Completed the iMCU row, advance counters for next one */
  coef->iMCU_row_num++;
  start_iMCU_row(cinfo);
  return TRUE;
}

// Abcouwer JSC 2021 - no FULL_COEF_BUFFER_SUPPORTED - remove fns

/*
 * Initialize coefficient buffer controller.
 */

JSC_GLOBAL(void)
jinit_c_coef_controller (j_compress_ptr cinfo, boolean need_full_buffer)
{
    JSC_ASSERT(cinfo != NULL);

  my_coef_ptr coef;

  coef = (my_coef_ptr)
    (*cinfo->mem->get_mem) ((j_common_ptr) cinfo, JPOOL_IMAGE,
                                SIZEOF(my_coef_controller));
  cinfo->coef = (struct jpeg_c_coef_controller *) coef;
  coef->pub.start_pass = start_pass_coef;

  /* Create the coefficient buffer. */
  // Abcouwer JSC 2021 - no FULL_COEF_BUFFER_SUPPORTED
  JSC_ASSERT(!need_full_buffer);
    /* We only need a single-MCU buffer. */
    JBLOCKROW buffer;
    JINT i;

    buffer = (JBLOCKROW)
      (*cinfo->mem->get_mem) ((j_common_ptr) cinfo, JPOOL_IMAGE,
                                  C_MAX_BLOCKS_IN_MCU * SIZEOF(JBLOCK));
    for (i = 0; i < C_MAX_BLOCKS_IN_MCU; i++) {
      coef->MCU_buffer[i] = buffer + i;
    }
    // Abcouwer ZSC 2021 - remove virtual arrays
}
