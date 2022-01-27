/*
 * jcsample.c
 *
 * Copyright (C) 1991-1996, Thomas G. Lane.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
 *
 * This file contains downsampling routines.
 *
 * Downsampling input data is counted in "row groups".  A row group
 * is defined to be max_v_samp_factor pixel rows of each component,
 * from which the downsampler produces v_samp_factor sample rows.
 * A single row group is processed in each call to the downsampler module.
 *
 * The downsampler is responsible for edge-expansion of its output data
 * to fill an integral number of DCT blocks horizontally.  The source buffer
 * may be modified if it is helpful for this purpose (the source buffer is
 * allocated wide enough to correspond to the desired output width).
 * The caller (the prep controller) is responsible for vertical padding.
 *
 * The downsampler may request "context rows" by setting need_context_rows
 * during startup.  In this case, the input arrays will contain at least
 * one row group's worth of pixels above and below the passed-in data;
 * the caller will create dummy rows at image top and bottom by replicating
 * the first or last real pixel row.
 *
 * An excellent reference for image resampling is
 *   Digital Image Warping, George Wolberg, 1990.
 *   Pub. by IEEE Computer Society Press, Los Alamitos, CA. ISBN 0-8186-8944-7.
 *
 * The downsampling algorithm used here is a simple average of the source
 * pixels covered by the output pixel.  The hi-falutin sampling literature
 * refers to this as a "box filter".  In general the characteristics of a box
 * filter are not very good, but for the specific cases we normally use (1:1
 * and 2:1 ratios) the box is equivalent to a "triangle filter" which is not
 * nearly so bad.  If you intend to use other sampling ratios, you'd be well
 * advised to improve this code.
 *
 * A simple input-smoothing capability is provided.  This is mainly intended
 * for cleaning up color-dithered GIF input files (if you find it inadequate,
 * we suggest using an external filtering program such as pnmconvol).  When
 * enabled, each input pixel P is replaced by a weighted sum of itself and its
 * eight neighbors.  P's weight is 1-8*SF and each neighbor's weight is SF,
 * where SF = (smoothing_factor / 1024).
 * Currently, smoothing is only supported for 2h2v sampling factors.
 */

#include "jsc/jpegint.h"


/* Pointer to routine to downsample a single component */
typedef JMETHOD(void, downsample1_ptr,
                (j_compress_ptr cinfo, jpeg_component_info * compptr,
                 JSAMPARRAY input_data, JSAMPARRAY output_data));

/* Private subobject */

typedef struct {
  struct jpeg_downsampler pub;        /* public fields */

  /* Downsampling method pointers, one per component */
  downsample1_ptr methods[MAX_COMPONENTS];

  /* Height of an output row group for each component. */
  JINT rowgroup_height[MAX_COMPONENTS];

  /* These arrays save pixel expansion factors so that int_downsample need not
   * recompute them each time.  They are unused for other downsampling methods.
   */
  UINT8 h_expand[MAX_COMPONENTS];
  UINT8 v_expand[MAX_COMPONENTS];
} my_downsampler;

/*
 * Initialize for a downsampling pass.
 */

JSC_METHODDEF(void)
start_pass_downsample (j_compress_ptr cinfo)
{
  /* no work for now */
}


/*
 * Expand a component horizontally from width input_cols to width output_cols,
 * by duplicating the rightmost samples.
 */

JSC_LOCAL(void)
expand_right_edge (JSAMPARRAY image_data, JINT num_rows,
                   JDIMENSION input_cols, JDIMENSION output_cols)
{
  register JSAMPROW ptr;
  register JSAMPLE pixval;
  register JINT count;
  JINT row;
  JINT numcols = (JINT) (output_cols - input_cols);

  if (numcols > 0) {
    for (row = 0; row < num_rows; row++) {
      ptr = image_data[row] + input_cols;
      pixval = ptr[-1];                /* don't need GETJSAMPLE() here */
      for (count = numcols; count > 0; count--) {
        *ptr++ = pixval;
      }
    }
  }
}


/*
 * Do downsampling for a whole row group (all components).
 *
 * In this version we simply downsample each component independently.
 */

JSC_METHODDEF(void)
sep_downsample (j_compress_ptr cinfo,
                JSAMPIMAGE input_buf, JDIMENSION in_row_index,
                JSAMPIMAGE output_buf, JDIMENSION out_row_group_index)
{
  my_downsampler * downsample = (my_downsampler *) cinfo->downsample;
  JINT ci;
  jpeg_component_info * compptr;
  JSAMPARRAY in_ptr, out_ptr;

  for (ci = 0; ci < cinfo->num_components; ci++) {
      compptr = cinfo->comp_info + ci;
    in_ptr = input_buf[ci] + in_row_index;
    out_ptr = output_buf[ci] +
              (out_row_group_index * downsample->rowgroup_height[ci]);
    (*downsample->methods[ci]) (cinfo, compptr, in_ptr, out_ptr);
  }
}


/*
 * Downsample pixel values of a single component.
 * One row group is processed per call.
 * This version handles arbitrary integral sampling ratios, without smoothing.
 * Note that this version is not actually used for customary sampling ratios.
 */

JSC_METHODDEF(void)
int_downsample (j_compress_ptr cinfo, jpeg_component_info * compptr,
                JSAMPARRAY input_data, JSAMPARRAY output_data)
{
  JSC_ASSERT(cinfo != NULL);
  JSC_ASSERT(compptr != NULL);
  JSC_ASSERT(input_data != NULL);
  JSC_ASSERT(output_data != NULL);

  my_downsampler * downsample = (my_downsampler *) cinfo->downsample;
  JINT inrow;
  JINT outrow;
  JINT h_expand;
  JINT v_expand;
  JINT numpix;
  JINT numpix2;
  JINT h;
  JINT v;
  JDIMENSION outcol;
  JDIMENSION outcol_h;        /* outcol_h == outcol*h_expand */
  JDIMENSION output_cols = compptr->width_in_blocks * compptr->DCT_h_scaled_size;
  JSAMPROW inptr;
  JSAMPROW outptr;
  INT32 outvalue;

  JSC_ASSERT_2(0 <= compptr->component_index && compptr->component_index < MAX_COMPONENTS,
          compptr->component_index, MAX_COMPONENTS);
  h_expand = downsample->h_expand[compptr->component_index];
  v_expand = downsample->v_expand[compptr->component_index];
  numpix = h_expand * v_expand;
  numpix2 = numpix/2;

  /* Expand input data enough to let all the output samples be generated
   * by the standard loop.  Special-casing padded output would be more
   * efficient.
   */
  expand_right_edge(input_data, cinfo->max_v_samp_factor,
                    cinfo->image_width, output_cols * h_expand);

  inrow = outrow = 0;
  while (inrow < cinfo->max_v_samp_factor) {
    outptr = output_data[outrow];
    for (outcol = 0, outcol_h = 0; outcol < output_cols;
         outcol++, outcol_h += h_expand) {
      outvalue = 0;
      for (v = 0; v < v_expand; v++) {
        inptr = input_data[inrow+v] + outcol_h;
        for (h = 0; h < h_expand; h++) {
          outvalue += (INT32) GETJSAMPLE(*inptr);
          inptr++;
        }
      }
      *outptr++ = (JSAMPLE) ((outvalue + numpix2) / numpix);
    }
    inrow += v_expand;
    outrow++;
  }
}


/*
 * Downsample pixel values of a single component.
 * This version handles the special case of a full-size component,
 * without smoothing.
 */

JSC_METHODDEF(void)
fullsize_downsample (j_compress_ptr cinfo, jpeg_component_info * compptr,
                     JSAMPARRAY input_data, JSAMPARRAY output_data)
{
  /* Copy the data */
  jcopy_sample_rows(input_data, 0, output_data, 0,
                    cinfo->max_v_samp_factor, cinfo->image_width);
  /* Edge-expand */
  expand_right_edge(output_data, cinfo->max_v_samp_factor, cinfo->image_width,
                    compptr->width_in_blocks * compptr->DCT_h_scaled_size);
}


/*
 * Downsample pixel values of a single component.
 * This version handles the common case of 2:1 horizontal and 1:1 vertical,
 * without smoothing.
 *
 * A note about the "bias" calculations: when rounding fractional values to
 * integer, we do not want to always round 0.5 up to the next integer.
 * If we did that, we'd introduce a noticeable bias towards larger values.
 * Instead, this code is arranged so that 0.5 will be rounded up or down at
 * alternate pixel locations (a simple ordered dither pattern).
 */

JSC_METHODDEF(void)
h2v1_downsample (j_compress_ptr cinfo, jpeg_component_info * compptr,
                 JSAMPARRAY input_data, JSAMPARRAY output_data)
{
  JSC_ASSERT(cinfo != NULL);
  JSC_ASSERT(compptr != NULL);
  JSC_ASSERT(input_data != NULL);
  JSC_ASSERT(output_data != NULL);

  JINT inrow;
  JDIMENSION outcol;
  JDIMENSION output_cols = compptr->width_in_blocks * compptr->DCT_h_scaled_size;
  register JSAMPROW inptr;
  register JSAMPROW outptr;
  register JINT bias;

  /* Expand input data enough to let all the output samples be generated
   * by the standard loop.  Special-casing padded output would be more
   * efficient.
   */
  expand_right_edge(input_data, cinfo->max_v_samp_factor,
                    cinfo->image_width, output_cols * 2);

  for (inrow = 0; inrow < cinfo->max_v_samp_factor; inrow++) {
    outptr = output_data[inrow];
    inptr = input_data[inrow];
    bias = 0;                        /* bias = 0,1,0,1,... for successive samples */
    for (outcol = 0; outcol < output_cols; outcol++) {
      *outptr++ = (JSAMPLE) ((GETJSAMPLE(*inptr) + GETJSAMPLE(inptr[1])
                              + bias) >> 1);
      bias ^= 1;                /* 0=>1, 1=>0 */
      inptr += 2;
    }
  }
}


/*
 * Downsample pixel values of a single component.
 * This version handles the standard case of 2:1 horizontal and 2:1 vertical,
 * without smoothing.
 */

JSC_METHODDEF(void)
h2v2_downsample (j_compress_ptr cinfo, jpeg_component_info * compptr,
                 JSAMPARRAY input_data, JSAMPARRAY output_data)
{
    JSC_ASSERT(cinfo != NULL);
    JSC_ASSERT(compptr != NULL);
    JSC_ASSERT(input_data != NULL);
    JSC_ASSERT(output_data != NULL);

  JINT inrow;
  JINT outrow;
  JDIMENSION outcol;
  JDIMENSION output_cols = compptr->width_in_blocks * compptr->DCT_h_scaled_size;
  register JSAMPROW inptr0;
  register JSAMPROW inptr1;
  register JSAMPROW outptr;
  register JINT bias;

  /* Expand input data enough to let all the output samples be generated
   * by the standard loop.  Special-casing padded output would be more
   * efficient.
   */
  expand_right_edge(input_data, cinfo->max_v_samp_factor,
                    cinfo->image_width, output_cols * 2);

  inrow = outrow = 0;
  while (inrow < cinfo->max_v_samp_factor) {
    outptr = output_data[outrow];
    inptr0 = input_data[inrow];
    inptr1 = input_data[inrow+1];
    bias = 1;                        /* bias = 1,2,1,2,... for successive samples */
    for (outcol = 0; outcol < output_cols; outcol++) {
      *outptr++ = (JSAMPLE) ((GETJSAMPLE(*inptr0) + GETJSAMPLE(inptr0[1]) +
                              GETJSAMPLE(*inptr1) + GETJSAMPLE(inptr1[1])
                              + bias) >> 2);
      bias ^= 3;                /* 1=>2, 2=>1 */
      inptr0 += 2;
      inptr1 += 2;
    }
    inrow += 2;
    outrow++;
  }
}

// Abcouwer JSC 2021 - remove input smoothing / context rows

/*
 * Module initialization routine for downsampling.
 * Note that we must select a routine for each component.
 */

JSC_GLOBAL(void)
jinit_downsampler (j_compress_ptr cinfo)
{
  JSC_ASSERT(cinfo != NULL);

  my_downsampler * downsample;
  JINT ci;
  jpeg_component_info * compptr;
  JINT h_in_group;
  JINT v_in_group;
  JINT h_out_group;
  JINT v_out_group;

  downsample = (my_downsampler *)
    (*cinfo->mem->get_mem) ((j_common_ptr) cinfo, JPOOL_IMAGE,
                                SIZEOF(my_downsampler));
  cinfo->downsample = (struct jpeg_downsampler *) downsample;
  downsample->pub.start_pass = start_pass_downsample;
  downsample->pub.downsample = sep_downsample;
  // Abcouwer JSC 2021 - remove input smoothing / context rows
  // Abcouwer JSC 2021 - remove CCIR601_sampling

  /* Verify we can handle the sampling factors, and set up method pointers */
  for (ci = 0; ci < cinfo->num_components; ci++) {
    compptr = cinfo->comp_info + ci;
    /* Compute size of an "output group" for DCT scaling.  This many samples
     * are to be converted from max_h_samp_factor * max_v_samp_factor pixels.
     */
    JSC_ASSERT_1(cinfo->min_DCT_h_scaled_size > 0,
              cinfo->min_DCT_h_scaled_size);
    JSC_ASSERT_1(cinfo->min_DCT_v_scaled_size > 0,
              cinfo->min_DCT_v_scaled_size);
    h_out_group = (compptr->h_samp_factor * compptr->DCT_h_scaled_size) /
                  cinfo->min_DCT_h_scaled_size;
    v_out_group = (compptr->v_samp_factor * compptr->DCT_v_scaled_size) /
                  cinfo->min_DCT_v_scaled_size;
    h_in_group = cinfo->max_h_samp_factor;
    v_in_group = cinfo->max_v_samp_factor;
    downsample->rowgroup_height[ci] = v_out_group; /* save for use later */
    JSC_ASSERT_1(h_out_group > 0, h_out_group);
    JSC_ASSERT_1(v_out_group > 0, v_out_group);
    if (h_in_group == h_out_group && v_in_group == v_out_group) {
        // Abcouwer JSC 2021 - remove input smoothing / context rows
        downsample->methods[ci] = fullsize_downsample;
    } else if (h_in_group == h_out_group * 2 &&
               v_in_group == v_out_group) {
      downsample->methods[ci] = h2v1_downsample;
    } else if (h_in_group == h_out_group * 2 &&
               v_in_group == v_out_group * 2) {
        // Abcouwer JSC 2021 - remove input smoothing / context rows
        downsample->methods[ci] = h2v2_downsample;
    } else {
        // must divide evenly
        JSC_ASSERT_1(h_out_group > 0, h_out_group);
        JSC_ASSERT_1(v_out_group > 0, v_out_group);
        JSC_ASSERT_2((h_in_group % h_out_group) == 0,
                h_in_group, h_out_group);
        JSC_ASSERT_2((v_in_group % v_out_group) == 0,
                v_in_group, v_out_group);
        downsample->methods[ci] = int_downsample;
        downsample->h_expand[ci] = (UINT8) (h_in_group / h_out_group);
        downsample->v_expand[ci] = (UINT8) (v_in_group / v_out_group);
    }
  }

  // Abcouwer JSC 2021 - remove input smoothing / context rows
}
