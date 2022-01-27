/*
 * jcprepct.c
 *
 * Copyright (C) 1994-1996, Thomas G. Lane.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
 *
 * This file contains the compression preprocessing controller.
 * This controller manages the color conversion, downsampling,
 * and edge expansion steps.
 *
 * Most of the complexity here is associated with buffering input rows
 * as required by the downsampler.  See the comments at the head of
 * jcsample.c for the downsampler's needs.
 */

#include "jsc/jpegint.h"

// Abcouwer JSC 2021 - remove context row support

/*
 * For the simple (no-context-row) case, we just need to buffer one
 * row group's worth of pixels for the downsampling step.  At the bottom of
 * the image, we pad to a full row group by replicating the last pixel row.
 * The downsampler's last output row is then replicated if needed to pad
 * out to a full iMCU row.
 *
 * When providing context rows, we must buffer three row groups' worth of
 * pixels.  Three row groups are physically allocated, but the row pointer
 * arrays are made five row groups high, with the extra pointers above and
 * below "wrapping around" to point to the last and first real row groups.
 * This allows the downsampler to access the proper context rows.
 * At the top and bottom of the image, we create dummy context rows by
 * copying the first or last real pixel row.  This copying could be avoided
 * by pointer hacking as is done in jdmainct.c, but it doesn't seem worth the
 * trouble on the compression side.
 */


/* Private buffer controller object */

typedef struct {
  struct jpeg_c_prep_controller pub; /* public fields */

  /* Downsampling input buffer.  This buffer holds color-converted data
   * until we have enough to do a downsample step.
   */
  JSAMPARRAY color_buf[MAX_COMPONENTS];

  JDIMENSION rows_to_go;        /* counts rows remaining in source image */
  JINT next_buf_row;                /* index of next row to store in color_buf */

  // Abcouwer JSC 2021 - remove input smoothing / context rows

} my_prep_controller;

/*
 * Initialize for a processing pass.
 */

JSC_METHODDEF(void)
start_pass_prep (j_compress_ptr cinfo, J_BUF_MODE pass_mode)
{
  my_prep_controller * prep = (my_prep_controller *) cinfo->prep;

  JSC_ASSERT_2(pass_mode == JBUF_PASS_THRU, pass_mode, JBUF_PASS_THRU);

  /* Initialize total-height counter for detecting bottom of image */
  prep->rows_to_go = cinfo->image_height;
  /* Mark the conversion buffer empty */
  prep->next_buf_row = 0;

  // Abcouwer JSC 2021 - remove input smoothing / context rows
}


/*
 * Expand an image vertically from height input_rows to height output_rows,
 * by duplicating the bottom row.
 */

JSC_LOCAL(void)
expand_bottom_edge (JSAMPARRAY image_data, JDIMENSION num_cols,
                    JINT input_rows, JINT output_rows)
{
  register JINT row;

  for (row = input_rows; row < output_rows; row++) {
    jcopy_sample_rows(image_data, input_rows-1, image_data, row,
                      1, num_cols);
  }
}


/*
 * Process some data in the simple no-context case.
 *
 * Preprocessor output data is counted in "row groups".  A row group
 * is defined to be v_samp_factor sample rows of each component.
 * Downsampling will produce this much data from each max_v_samp_factor
 * input rows.
 */

JSC_METHODDEF(void)
pre_process_data (j_compress_ptr cinfo,
                  JSAMPARRAY input_buf, JDIMENSION *in_row_ctr,
                  JDIMENSION in_rows_avail,
                  JSAMPIMAGE output_buf, JDIMENSION *out_row_group_ctr,
                  JDIMENSION out_row_groups_avail)
{
  my_prep_controller * prep = (my_prep_controller *) cinfo->prep;
  JINT numrows;
  JINT ci;
  JDIMENSION inrows;
  jpeg_component_info * compptr;

  while (*in_row_ctr < in_rows_avail &&
         *out_row_group_ctr < out_row_groups_avail) {
    /* Do color conversion to fill the conversion buffer. */
    inrows = in_rows_avail - *in_row_ctr;
    numrows = cinfo->max_v_samp_factor - prep->next_buf_row;
    numrows = (JINT) JSC_MIN((JDIMENSION) numrows, inrows);
    (*cinfo->cconvert->color_convert) (cinfo, input_buf + *in_row_ctr,
                                       prep->color_buf,
                                       (JDIMENSION) prep->next_buf_row,
                                       numrows);
    *in_row_ctr += numrows;
    prep->next_buf_row += numrows;
    prep->rows_to_go -= numrows;
    /* If at bottom of image, pad to fill the conversion buffer. */
    if (prep->rows_to_go == 0 &&
        prep->next_buf_row < cinfo->max_v_samp_factor) {
      for (ci = 0; ci < cinfo->num_components; ci++) {
        expand_bottom_edge(prep->color_buf[ci], cinfo->image_width,
                           prep->next_buf_row, cinfo->max_v_samp_factor);
      }
      prep->next_buf_row = cinfo->max_v_samp_factor;
    }
    /* If we've filled the conversion buffer, empty it. */
    if (prep->next_buf_row == cinfo->max_v_samp_factor) {
      (*cinfo->downsample->downsample) (cinfo,
                                        prep->color_buf, (JDIMENSION) 0,
                                        output_buf, *out_row_group_ctr);
      prep->next_buf_row = 0;
      (*out_row_group_ctr)++;
    }
    /* If at bottom of image, pad the output to a full iMCU height.
     * Note we assume the caller is providing a one-iMCU-height output buffer!
     */
    if (prep->rows_to_go == 0 &&
        *out_row_group_ctr < out_row_groups_avail) {
      for (ci = 0; ci < cinfo->num_components; ci++) {
          compptr = cinfo->comp_info + ci;
        numrows = (compptr->v_samp_factor * compptr->DCT_v_scaled_size) /
                  cinfo->min_DCT_v_scaled_size;
        expand_bottom_edge(output_buf[ci],
                           compptr->width_in_blocks * compptr->DCT_h_scaled_size,
                           (JINT) (*out_row_group_ctr * numrows),
                           (JINT) (out_row_groups_avail * numrows));
      }
      *out_row_group_ctr = out_row_groups_avail;
      break;                        /* can exit outer loop without test */
    }
  }
}

// Abcouwer JSC 2021 - remove input smoothing / context rows

/*
 * Initialize preprocessing controller.
 */

JSC_GLOBAL(void)
jinit_c_prep_controller (j_compress_ptr cinfo, boolean need_full_buffer)
{
    JSC_ASSERT(cinfo != NULL);

    my_prep_controller * prep;
    JINT ci;
    jpeg_component_info *compptr;

    JSC_ASSERT(!need_full_buffer);

    prep = (my_prep_controller *) (*cinfo->mem->get_mem)((j_common_ptr) cinfo,
            JPOOL_IMAGE, SIZEOF(my_prep_controller));
    cinfo->prep = (struct jpeg_c_prep_controller*) prep;
    prep->pub.start_pass = start_pass_prep;

    /* Allocate the color conversion buffer.
     * We make the buffer wide enough to allow the downsampler to edge-expand
     * horizontally within the buffer, if it so chooses.
     */
    // Abcouwer JSC 2021 - remove input smoothing / context rows
    /* No context, just make it tall enough for one row group */
    prep->pub.pre_process_data = pre_process_data;
    for (ci = 0; ci < cinfo->num_components; ci++) {
        compptr = cinfo->comp_info + ci;
        prep->color_buf[ci] = (*cinfo->mem->get_sarray)((j_common_ptr) cinfo,
                JPOOL_IMAGE,
                (JDIMENSION) (((JLONG) compptr->width_in_blocks
                        * cinfo->min_DCT_h_scaled_size
                        * cinfo->max_h_samp_factor) / compptr->h_samp_factor),
                (JDIMENSION) cinfo->max_v_samp_factor);
    }
}
