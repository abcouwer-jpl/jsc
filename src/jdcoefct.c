/*
 * jdcoefct.c
 *
 * Copyright (C) 1994-1997, Thomas G. Lane.
 * Modified 2002-2011 by Guido Vollbeding.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
 *
 * This file contains the coefficient buffer controller for decompression.
 * This controller is the top level of the JPEG decompressor proper.
 * The coefficient buffer lies between entropy decoding and inverse-DCT steps.
 *
 * In buffered-image mode, this controller is the interface between
 * input-oriented processing and output-oriented processing.
 * Also, the input side (only) is used when reading a file for transcoding.
 */

#include "jsc/jpegint.h"

/* Private buffer controller object */

typedef struct {
  struct jpeg_d_coef_controller pub; /* public fields */

  /* These variables keep track of the current location of the input side. */
  /* cinfo->input_iMCU_row is also used for this. */
  JDIMENSION MCU_ctr;                /* counts MCUs processed in current row */
  JINT MCU_vert_offset;                /* counts MCU rows within iMCU row */
  JINT MCU_rows_per_iMCU_row;        /* number of such rows needed */

  /* The output side's location is represented by cinfo->output_iMCU_row. */

  /* In single-pass modes, it's sufficient to buffer just one MCU.
   * We allocate a workspace of D_MAX_BLOCKS_IN_MCU coefficient blocks,
   * and let the entropy decoder write into that workspace each time.
   * (On 80x86, the workspace is FAR even though it's not really very big;
   * this is to keep the module interfaces unchanged when a large coefficient
   * buffer is necessary.)
   * In multi-pass modes, this array points to the current MCU's blocks
   * within the virtual arrays; it is used only by the input side.
   */
  JBLOCKROW MCU_buffer[D_MAX_BLOCKS_IN_MCU];

  // Abcouwer JSC 2021 - no multiscan or blocksmoothing support
} my_coef_controller;

typedef my_coef_controller * my_coef_ptr;

/* Forward declarations */
JSC_METHODDEF(JINT) decompress_onepass
        JPP((j_decompress_ptr cinfo, JSAMPIMAGE output_buf));

// Abcouwer JSC 2021 - no multiscan or blocksmoothing support


JSC_LOCAL(void)
start_iMCU_row (j_decompress_ptr cinfo)
/* Reset within-iMCU-row counters for a new row (input side) */
{
  JSC_ASSERT(cinfo != NULL);

  my_coef_ptr coef = (my_coef_ptr) cinfo->coef;
  JSC_ASSERT(coef != NULL);

  /* In an interleaved scan, an MCU row is the same as an iMCU row.
   * In a noninterleaved scan, an iMCU row has v_samp_factor MCU rows.
   * But at the bottom of the image, process only what's left.
   */
  if (cinfo->comps_in_scan > 1) {
    coef->MCU_rows_per_iMCU_row = 1;
  } else {
    if (cinfo->input_iMCU_row < (cinfo->total_iMCU_rows-1))
      coef->MCU_rows_per_iMCU_row = cinfo->cur_comp_info[0]->v_samp_factor;
    else
      coef->MCU_rows_per_iMCU_row = cinfo->cur_comp_info[0]->last_row_height;
  }

  coef->MCU_ctr = 0;
  coef->MCU_vert_offset = 0;
}


/*
 * Initialize for an input processing pass.
 */

JSC_METHODDEF(void)
start_input_pass (j_decompress_ptr cinfo)
{
  JSC_ASSERT(cinfo != NULL);
  cinfo->input_iMCU_row = 0;
  start_iMCU_row(cinfo);
}


/*
 * Initialize for an output processing pass.
 */

JSC_METHODDEF(void)
start_output_pass (j_decompress_ptr cinfo)
{
  JSC_ASSERT(cinfo != NULL);
  // Abcouwer JSC 2021 - no block smoothing support
  cinfo->output_iMCU_row = 0;
}


/*
 * Decompress and return some data in the single-pass case.
 * Always attempts to emit one fully interleaved MCU row ("iMCU" row).
 * Input and output must run in lockstep since we have only a one-MCU buffer.
 * Return value is JPEG_ROW_COMPLETED, JPEG_SCAN_COMPLETED, or JPEG_SUSPENDED.
 *
 * NB: output_buf contains a plane for each component in image,
 * which we index according to the component's SOF position.
 */

JSC_METHODDEF(JINT)
decompress_onepass (j_decompress_ptr cinfo, JSAMPIMAGE output_buf)
{
  JSC_ASSERT(cinfo != NULL);
  JSC_ASSERT(output_buf != NULL);

  my_coef_ptr coef = (my_coef_ptr) cinfo->coef;
  JDIMENSION MCU_col_num;        /* index of current MCU within row */
  JDIMENSION last_MCU_col = cinfo->MCUs_per_row - 1;
  JDIMENSION last_iMCU_row = cinfo->total_iMCU_rows - 1;
  JINT blkn;
  JINT ci;
  JINT xindex;
  JINT yindex;
  JINT yoffset;
  JINT useful_width;
  JSAMPARRAY output_ptr;
  JDIMENSION start_col;
  JDIMENSION output_col;
  jpeg_component_info *compptr;
  inverse_DCT_method_ptr inverse_DCT;

  JSC_ASSERT(coef != NULL);
  JSC_ASSERT(cinfo->entropy != NULL);
  JSC_ASSERT(cinfo->idct != NULL);

  /* Loop to process as much as one whole iMCU row */
  for (yoffset = coef->MCU_vert_offset; yoffset < coef->MCU_rows_per_iMCU_row;
       yoffset++) {
    for (MCU_col_num = coef->MCU_ctr; MCU_col_num <= last_MCU_col;
         MCU_col_num++) {
      /* Try to fetch an MCU.  Entropy decoder expects buffer to be zeroed. */
      if (cinfo->lim_Se)        /* can bypass in DC only case */
        JSC_FMEMZERO((void *) coef->MCU_buffer[0],
                 (JSIZE) (cinfo->blocks_in_MCU * SIZEOF(JBLOCK)));
      if (! (*cinfo->entropy->decode_mcu) (cinfo, coef->MCU_buffer)) {
        /* Suspension forced; update state counters and exit */
        coef->MCU_vert_offset = yoffset;
        coef->MCU_ctr = MCU_col_num;
        return JPEG_SUSPENDED;
      }
      /* Determine where data should go in output_buf and do the IDCT thing.
       * We skip dummy blocks at the right and bottom edges (but blkn gets
       * incremented past them!).  Note the inner loop relies on having
       * allocated the MCU_buffer[] blocks sequentially.
       */
      blkn = 0;                        /* index of current DCT block within MCU */
      for (ci = 0; ci < cinfo->comps_in_scan; ci++) {
        compptr = cinfo->cur_comp_info[ci];
        JSC_ASSERT(compptr != NULL);
        /* Don't bother to IDCT an uninteresting component. */
        if (! compptr->component_needed) {
          blkn += compptr->MCU_blocks;
          continue;
        }
        inverse_DCT = cinfo->idct->inverse_DCT[compptr->component_index];
        useful_width = (MCU_col_num < last_MCU_col) ? compptr->MCU_width
                                                    : compptr->last_col_width;
        output_ptr = output_buf[compptr->component_index] +
          yoffset * compptr->DCT_v_scaled_size;
        start_col = MCU_col_num * compptr->MCU_sample_width;
        for (yindex = 0; yindex < compptr->MCU_height; yindex++) {
          if (cinfo->input_iMCU_row < last_iMCU_row ||
              yoffset+yindex < compptr->last_row_height) {
            output_col = start_col;
            for (xindex = 0; xindex < useful_width; xindex++) {
              (*inverse_DCT) (cinfo, compptr,
                              (JCOEFPTR) coef->MCU_buffer[blkn+xindex],
                              output_ptr, output_col);
              output_col += compptr->DCT_h_scaled_size;
            }
          }
          blkn += compptr->MCU_width;
          output_ptr += compptr->DCT_v_scaled_size;
        }
      }
    }
    /* Completed an MCU row, but perhaps not an iMCU row */
    coef->MCU_ctr = 0;
  }
  /* Completed the iMCU row, advance counters for next one */
  cinfo->output_iMCU_row++;
  cinfo->input_iMCU_row++;
  if (cinfo->input_iMCU_row < cinfo->total_iMCU_rows) {
    start_iMCU_row(cinfo);
    return JPEG_ROW_COMPLETED;
  }
  /* Completed the scan */
  (*cinfo->inputctl->finish_input_pass) (cinfo);
  return JPEG_SCAN_COMPLETED;
}

/*
 * Dummy consume-input routine for single-pass operation.
 */
// Abcouwer JSC 2021 - should never be called
JSC_METHODDEF(JINT)
dummy_consume_data (j_decompress_ptr cinfo)
{
  JSC_ASSERT(0);
  return JPEG_SUSPENDED;        /* Always indicate nothing was done */
}

// Abcouwer JSC 2021 - no multiscan support

/*
 * Initialize coefficient buffer controller.
 */

JSC_GLOBAL(void)
jinit_d_coef_controller (j_decompress_ptr cinfo, boolean need_full_buffer)
{
    my_coef_ptr coef;

    coef = (my_coef_ptr) (*cinfo->mem->get_mem)((j_common_ptr) cinfo,
            JPOOL_IMAGE, SIZEOF(my_coef_controller));
    cinfo->coef = (struct jpeg_d_coef_controller*) coef;
    coef->pub.start_input_pass = start_input_pass;
    coef->pub.start_output_pass = start_output_pass;
    // Abcouwer JSC 2021 - no block smoothing support

  /* Create the coefficient buffer. */
    // Abcouwer JSC 2021 - no multiscan support
    JSC_ASSERT(!need_full_buffer);
    /* We only need a single-MCU buffer. */
    JBLOCKROW buffer;
    JINT i;

    buffer = (JBLOCKROW)
      (*cinfo->mem->get_mem) ((j_common_ptr) cinfo, JPOOL_IMAGE,
                                  D_MAX_BLOCKS_IN_MCU * SIZEOF(JBLOCK));
    for (i = 0; i < D_MAX_BLOCKS_IN_MCU; i++) {
      coef->MCU_buffer[i] = buffer + i;
    }
    if (cinfo->lim_Se == 0)        /* DC only case: want to bypass later */
      JSC_FMEMZERO((void *) buffer,
               (JSIZE) (D_MAX_BLOCKS_IN_MCU * SIZEOF(JBLOCK)));
    coef->pub.consume_data = dummy_consume_data;
    coef->pub.decompress_data = decompress_onepass;
    // Abcouwer ZSC 2021 - remove virtual arrays
    // coef->pub.coef_arrays = NULL; /* flag for no virtual arrays */
}
