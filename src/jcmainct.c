/*
 * jcmainct.c
 *
 * Copyright (C) 1994-1996, Thomas G. Lane.
 * Modified 2003-2012 by Guido Vollbeding.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
 *
 * This file contains the main buffer controller for compression.
 * The main buffer lies between the pre-processor and the JPEG
 * compressor proper; it holds downsampled data in the JPEG colorspace.
 */

#include "jsc/jpegint.h"

// Abcouwer JSC 2021 - remove full buffer support

/* Private buffer controller object */

typedef struct {
  struct jpeg_c_main_controller pub; /* public fields */

  JDIMENSION cur_iMCU_row;        /* number of current iMCU row */
  JDIMENSION rowgroup_ctr;        /* counts row groups received in iMCU row */
  boolean suspended;                /* remember if we suspended output */
  J_BUF_MODE pass_mode;                /* current operating mode */

  /* If using just a strip buffer, this points to the entire set of buffers
   * (we allocate one for each component).  In the full-image case, this
   * points to the currently accessible strips of the virtual arrays.
   */
  JSAMPARRAY buffer[MAX_COMPONENTS];

  // Abcouwer JSC 2021 - remove full buffer support
} my_main_controller;

typedef my_main_controller * my_main_ptr;


/* Forward declarations */
JSC_METHODDEF(void) process_data_simple_main
        JPP((j_compress_ptr cinfo, JSAMPARRAY input_buf,
             JDIMENSION *in_row_ctr, JDIMENSION in_rows_avail));

// Abcouwer JSC 2021 - remove full buffer support


/*
 * Initialize for a processing pass.
 */

JSC_METHODDEF(void)
start_pass_main (j_compress_ptr cinfo, J_BUF_MODE pass_mode)
{
  my_main_ptr mainp = (my_main_ptr) cinfo->main;

  // Abcouwer JSC 2021 - remove raw data in

  mainp->cur_iMCU_row = 0;        /* initialize counters */
  mainp->rowgroup_ctr = 0;
  mainp->suspended = FALSE;

  // Abcouwer JSC 2021 - remove full buffer support
  JSC_ASSERT_2(pass_mode == JBUF_PASS_THRU, pass_mode, JBUF_PASS_THRU);
  mainp->pub.process_data = process_data_simple_main;
  mainp->pass_mode = pass_mode;        /* save mode for use by process_data */

}


/*
 * Process some data.
 * This routine handles the simple pass-through mode,
 * where we have only a strip buffer.
 */

JSC_METHODDEF(void)
process_data_simple_main (j_compress_ptr cinfo,
                          JSAMPARRAY input_buf, JDIMENSION *in_row_ctr,
                          JDIMENSION in_rows_avail)
{
  JSC_ASSERT(in_row_ctr != NULL);

  my_main_ptr mainp = (my_main_ptr) cinfo->main;

  while (mainp->cur_iMCU_row < cinfo->total_iMCU_rows) {
    /* Read input data if we haven't filled the main buffer yet */
    if (mainp->rowgroup_ctr < (JDIMENSION) cinfo->min_DCT_v_scaled_size)
      (*cinfo->prep->pre_process_data) (cinfo,
                                        input_buf, in_row_ctr, in_rows_avail,
                                        mainp->buffer, &mainp->rowgroup_ctr,
                                        (JDIMENSION) cinfo->min_DCT_v_scaled_size);

    /* If we don't have a full iMCU row buffered, return to application for
     * more data.  Note that preprocessor will always pad to fill the iMCU row
     * at the bottom of the image.
     */
    if (mainp->rowgroup_ctr != (JDIMENSION) cinfo->min_DCT_v_scaled_size)
      return;

    /* Send the completed row to the compressor */
    if (! (*cinfo->coef->compress_data) (cinfo, mainp->buffer)) {
      /* If compressor did not consume the whole row, then we must need to
       * suspend processing and return to the application.  In this situation
       * we pretend we didn't yet consume the last input row; otherwise, if
       * it happened to be the last row of the image, the application would
       * think we were done.
       */
      if (! mainp->suspended) {
        (*in_row_ctr)--;
        mainp->suspended = TRUE;
      }
      return;
    }

    // must not have suspended
    JSC_ASSERT(!mainp->suspended);
//    /* We did finish the row.  Undo our little suspension hack if a previous
//     * call suspended; then mark the main buffer empty.
//     */
//    if (mainp->suspended) {
//      (*in_row_ctr)++;
//      mainp->suspended = FALSE;
//    }
    mainp->rowgroup_ctr = 0;
    mainp->cur_iMCU_row++;
  }
}

// Abcouwer JSC 2021 - remove full buffer support

/*
 * Initialize main buffer controller.
 */

JSC_GLOBAL(void)
jinit_c_main_controller (j_compress_ptr cinfo, boolean need_full_buffer)
{
    JSC_ASSERT(cinfo != NULL);

    my_main_ptr mainp;
    JINT ci;
    jpeg_component_info *compptr;

    mainp = (my_main_ptr) (*cinfo->mem->get_mem)((j_common_ptr) cinfo,
            JPOOL_IMAGE, SIZEOF(my_main_controller));
    cinfo->main = &mainp->pub;
    mainp->pub.start_pass = start_pass_main;

    // Abcouwer JSC 2021 - remove raw data in

    /* Create the buffer.  It holds downsampled data, so each component
     * may be of a different size.
     */
    // Abcouwer JSC 2021 - remove full buffer support
    JSC_ASSERT(!need_full_buffer);
    /* Allocate a strip buffer for each component */
    for (ci = 0; ci < cinfo->num_components; ci++) {
        compptr = cinfo->comp_info + ci;
        mainp->buffer[ci] = (*cinfo->mem->get_sarray)(
                (j_common_ptr) cinfo, JPOOL_IMAGE,
                compptr->width_in_blocks
                        * ((JDIMENSION) compptr->DCT_h_scaled_size),
                (JDIMENSION) (compptr->v_samp_factor
                        * compptr->DCT_v_scaled_size));
    }
}
