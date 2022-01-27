/*
 * jdmaster.c
 *
 * Copyright (C) 1991-1997, Thomas G. Lane.
 * Modified 2002-2019 by Guido Vollbeding.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
 *
 * This file contains master control logic for the JPEG decompressor.
 * These routines are concerned with selecting the modules to be executed
 * and with determining the number of passes and the work to be done in each
 * pass.
 */

#include "jsc/jpegint.h"

/* Private state */

typedef struct {
  struct jpeg_decomp_master pub; /* public fields */

  JINT pass_number;		/* # of passes completed */

  boolean using_merged_upsample; /* TRUE if using merged upsample/cconvert */

  /* Saved references to initialized quantizer modules,
   * in case we need to switch modes.
   */
  struct jpeg_color_quantizer * quantizer_1pass;
  struct jpeg_color_quantizer * quantizer_2pass;
} my_decomp_master;


/*
 * Determine whether merged upsample/color conversion should be used.
 * CRUCIAL: this must match the actual capabilities of jdmerge.c!
 */

JSC_LOCAL(boolean)
use_merged_upsample (j_decompress_ptr cinfo)
{
  /* Merging is the equivalent of plain box-filter upsampling. */
  /* The following condition is only needed if fancy shall select
   * a different upsampling method.  In our current implementation
   * fancy only affects the DCT scaling, thus we can use fancy
   * upsampling and merged upsample simultaneously, in particular
   * with scaled DCT sizes larger than the default DCTSIZE.
   */

    // Abcouwer JSC 2021 - no CCIR601_sampling

    /* jdmerge.c only supports YCC=>RGB color conversion */
  // Abcouwer JSC 2021 - remove support for subtract green / color transform
  if ((cinfo->jpeg_color_space != JCS_YCbCr &&
       cinfo->jpeg_color_space != JCS_BG_YCC) ||
      cinfo->num_components != 3 ||
      cinfo->out_color_space != JCS_RGB ||
      cinfo->out_color_components != RGB_PIXELSIZE)
    return FALSE;
  /* and it only handles 2h1v or 2h2v sampling ratios */
  if (cinfo->comp_info[0].h_samp_factor != 2 ||
      cinfo->comp_info[1].h_samp_factor != 1 ||
      cinfo->comp_info[2].h_samp_factor != 1 ||
      cinfo->comp_info[0].v_samp_factor >  2 ||
      cinfo->comp_info[1].v_samp_factor != 1 ||
      cinfo->comp_info[2].v_samp_factor != 1)
    return FALSE;
  /* furthermore, it doesn't work if we've scaled the IDCTs differently */
  if (cinfo->comp_info[0].DCT_h_scaled_size != cinfo->min_DCT_h_scaled_size ||
      cinfo->comp_info[1].DCT_h_scaled_size != cinfo->min_DCT_h_scaled_size ||
      cinfo->comp_info[2].DCT_h_scaled_size != cinfo->min_DCT_h_scaled_size ||
      cinfo->comp_info[0].DCT_v_scaled_size != cinfo->min_DCT_v_scaled_size ||
      cinfo->comp_info[1].DCT_v_scaled_size != cinfo->min_DCT_v_scaled_size ||
      cinfo->comp_info[2].DCT_v_scaled_size != cinfo->min_DCT_v_scaled_size)
    return FALSE;
  /* ??? also need to test for upsample-time rescaling, when & if supported */
  return TRUE;			/* by golly, it'll work... */
}


/*
 * Compute output image dimensions and related values.
 * NOTE: this is exported for possible use by application.
 * Hence it mustn't do anything that can't be done twice.
 * Also note that it may be called before the master module is initialized!
 */

JSC_GLOBAL(void)
jpeg_calc_output_dimensions (j_decompress_ptr cinfo)
/* Do computations that are needed before master selection phase.
 * This function is used for full decompression.
 */
{
    JSC_COMPILE_ASSERT(RGB_PIXELSIZE == 3, bad_rgb_pixelsize);

    // Abcouwer ZSC 2021 - no idct scaling

  /* Prevent application from calling me at wrong times */
  JSC_ASSERT_2(cinfo->global_state == DSTATE_READY,
          cinfo->global_state, DSTATE_READY);

  /* Compute core output image dimensions and DCT scaling choices. */
  jpeg_core_output_dimensions(cinfo);

  // Abcouwer ZSC 2021 - no idct scaling

  /* Report number of components in selected colorspace. */
  /* Probably this should be in the color conversion module... */
  switch (cinfo->out_color_space) {
  case JCS_GRAYSCALE:
    cinfo->out_color_components = 1;
    break;
  case JCS_RGB:
  case JCS_BG_RGB:
      // Abcouwer JSC 2021 - only 3 component RGB
  case JCS_YCbCr:
  case JCS_BG_YCC:
    cinfo->out_color_components = 3;
    break;
  case JCS_CMYK:
  case JCS_YCCK:
    cinfo->out_color_components = 4;
    break;
  default:			/* else must be same colorspace as in file */
    cinfo->out_color_components = cinfo->num_components;
  }
  // Abcouwer ZSC 2021 - don't support colormaps
  cinfo->output_components = /*(cinfo->quantize_colors ? 1 :*/
			      cinfo->out_color_components/*)*/;

  /* See if upsampler will want to emit more than one row at a time */
  if (use_merged_upsample(cinfo)) {
    cinfo->rec_outbuf_height = cinfo->max_v_samp_factor;
  } else {
    cinfo->rec_outbuf_height = 1;
  }
}


/*
 * Several decompression processes need to range-limit values to the range
 * 0..MAXJSAMPLE; the input value may fall somewhat outside this range
 * due to noise introduced by quantization, roundoff error, etc.  These
 * processes are inner loops and need to be as fast as possible.  On most
 * machines, particularly CPUs with pipelines or instruction prefetch,
 * a (subscript-check-less) C table lookup
 *		x = sample_range_limit[x];
 * is faster than explicit tests
 *		if (x < 0)  x = 0;
 *		else if (x > MAXJSAMPLE)  x = MAXJSAMPLE;
 * These processes all use a common table prepared by the routine below.
 *
 * For most steps we can mathematically guarantee that the initial value
 * of x is within 2*(MAXJSAMPLE+1) of the legal range, so a table running
 * from -2*(MAXJSAMPLE+1) to 3*MAXJSAMPLE+2 is sufficient.  But for the
 * initial limiting step (just after the IDCT), a wildly out-of-range value
 * is possible if the input data is corrupt.  To avoid any chance of indexing
 * off the end of memory and getting a bad-pointer trap, we perform the
 * post-IDCT limiting thus:
 *		x = (sample_range_limit - SUBSET)[(x + CENTER) & MASK];
 * where MASK is 2 bits wider than legal sample data, ie 10 bits for 8-bit
 * samples.  Under normal circumstances this is more than enough range and
 * a correct output will be generated; with bogus input data the mask will
 * cause wraparound, and we will safely generate a bogus-but-in-range output.
 * For the post-IDCT step, we want to convert the data from signed to unsigned
 * representation by adding CENTERJSAMPLE at the same time that we limit it.
 * This is accomplished with SUBSET = CENTER - CENTERJSAMPLE.
 *
 * Note that the table is allocated in near data space on PCs; it's small
 * enough and used often enough to justify this.
 */

JSC_LOCAL(void)
prepare_range_limit_table (j_decompress_ptr cinfo)
/* Allocate and fill in the sample_range_limit table */
{
  JSAMPLE * table;
  JINT i;

  table = (JSAMPLE *) (*cinfo->mem->get_mem) ((j_common_ptr) cinfo,
    JPOOL_IMAGE, (RANGE_CENTER * 2 + MAXJSAMPLE + 1) * SIZEOF(JSAMPLE));
  /* First segment of range limit table: limit[x] = 0 for x < 0 */
  JSC_MEMZERO(table, RANGE_CENTER * SIZEOF(JSAMPLE));
  table += RANGE_CENTER;	/* allow negative subscripts of table */
  cinfo->sample_range_limit = table;
  /* Main part of range limit table: limit[x] = x */
  for (i = 0; i <= MAXJSAMPLE; i++) {
    table[i] = (JSAMPLE) i;
  }
  /* End of range limit table: limit[x] = MAXJSAMPLE for x > MAXJSAMPLE */
  for (; i <=  MAXJSAMPLE + RANGE_CENTER; i++) {
    table[i] = MAXJSAMPLE;
  }
}


/*
 * Master selection of decompression modules.
 * This is done once at jpeg_start_decompress time.  We determine
 * which modules will be used and give them appropriate initialization calls.
 * We also initialize the decompressor input side to begin consuming data.
 *
 * Since jpeg_read_header has finished, we know what is in the SOF
 * and (first) SOS markers.  We also have all the application parameter
 * settings.
 */

JSC_LOCAL(void)
master_selection (j_decompress_ptr cinfo)
{
  my_decomp_master * master = (my_decomp_master *) cinfo->master;
  boolean use_c_buffer;
  JLONG samplesperrow;
  JDIMENSION jd_samplesperrow;

  /* For now, precision must match compiled-in value... */
  JSC_ASSERT_2(cinfo->data_precision == BITS_IN_JSAMPLE,
          cinfo->data_precision, BITS_IN_JSAMPLE);

  /* Initialize dimensions and other stuff */
  jpeg_calc_output_dimensions(cinfo);
  prepare_range_limit_table(cinfo);

  /* Sanity check on image dimensions */
  JSC_ASSERT_1(cinfo->output_height > 0, cinfo->output_height);
  JSC_ASSERT_1(cinfo->output_width > 0,  cinfo->output_height);
  JSC_ASSERT_1(cinfo->out_color_components > 0, cinfo->out_color_components);

  /* Width of an output scanline must be representable as JDIMENSION. */
  samplesperrow = (JLONG) cinfo->output_width * (JLONG) cinfo->out_color_components;
  jd_samplesperrow = (JDIMENSION) samplesperrow;
  JSC_ASSERT_2((JLONG) jd_samplesperrow == samplesperrow,
          jd_samplesperrow, samplesperrow);

  /* Initialize my private state */
  master->pass_number = 0;
  master->using_merged_upsample = use_merged_upsample(cinfo);

  /* Color quantizer selection */
  master->quantizer_1pass = NULL;
  master->quantizer_2pass = NULL;

  // Abcouwer ZSC 2021 - don't support colormaps

  /* Post-processing: in particular, color conversion first */
  if (! cinfo->raw_data_out) {
    if (master->using_merged_upsample) {
      jinit_merged_upsampler(cinfo); /* does color conversion too */
    } else {
      jinit_color_deconverter(cinfo);
      jinit_upsampler(cinfo);
    }
    jinit_d_post_controller(cinfo);
  }
  /* Inverse DCT */
  jinit_inverse_dct(cinfo);

  // Abcouwer ZSC 2021 - only huffman encoding
  jinit_huff_decoder(cinfo);

  /* Initialize principal buffer controllers. */
  use_c_buffer = cinfo->inputctl->has_multiple_scans || cinfo->buffered_image;
  jinit_d_coef_controller(cinfo, use_c_buffer);

  if (! cinfo->raw_data_out) {
    jinit_d_main_controller(cinfo, FALSE /* never need full buffer here */);
  }

  // Abcouwer ZSC 2021 - remove virtual arrays

  /* Initialize input side of decompressor to consume first scan. */
  (*cinfo->inputctl->start_input_pass) (cinfo);

  // Abcouwer ZSC 2021 - remove multiscan

}


/*
 * Per-pass setup.
 * This is called at the beginning of each output pass.  We determine which
 * modules will be active during this pass and give them appropriate
 * start_pass calls.  We also set is_dummy_pass to indicate whether this
 * is a "real" output pass or a dummy pass for color quantization.
 * (In the latter case, jdapistd.c will crank the pass to completion.)
 */

JSC_METHODDEF(void)
prepare_for_output_pass (j_decompress_ptr cinfo)
{
    my_decomp_master *master = (my_decomp_master*) cinfo->master;

    // Abcouwer ZSC 2021 - don't support 2-pass
    JSC_ASSERT(!master->pub.is_dummy_pass);

    // Abcouwer ZSC 2021 - don't support colormaps
    (*cinfo->idct->start_pass)(cinfo);
    (*cinfo->coef->start_output_pass)(cinfo);
    if (!cinfo->raw_data_out) {
        if (!master->using_merged_upsample) {
            (*cinfo->cconvert->start_pass)(cinfo);
        }
        (*cinfo->upsample->start_pass)(cinfo);
        // Abcouwer ZSC 2021 - don't support colormaps
        (*cinfo->post->start_pass)(cinfo,
                (master->pub.is_dummy_pass ? JBUF_SAVE_AND_PASS
                                           : JBUF_PASS_THRU));
        (*cinfo->main->start_pass)(cinfo, JBUF_PASS_THRU);
    }

    /* Set up progress monitor's pass info if present */
    if (cinfo->progress != NULL) {
        cinfo->progress->completed_passes = master->pass_number;
        cinfo->progress->total_passes = master->pass_number + 1;
        /* In buffered-image mode, we assume one more output pass if EOI not
         * yet reached, but no more passes if EOI has been reached.
         */
        if (cinfo->buffered_image && !cinfo->inputctl->eoi_reached) {
            cinfo->progress->total_passes++;
        }
    }
}


/*
 * Finish up at end of an output pass.
 */

JSC_METHODDEF(void)
finish_output_pass (j_decompress_ptr cinfo)
{
  my_decomp_master * master = (my_decomp_master *) cinfo->master;
  // Abcouwer ZSC 2021 - don't support colormaps
  master->pass_number++;
}

// Abcouwer ZSC 2021 - don't support multiscan

/*
 * Initialize master decompression control and select active modules.
 * This is performed at the start of jpeg_start_decompress.
 */

JSC_GLOBAL(void)
jinit_master_decompress (j_decompress_ptr cinfo)
{
  my_decomp_master * master;

  master = (my_decomp_master *) (*cinfo->mem->get_mem)
    ((j_common_ptr) cinfo, JPOOL_IMAGE, SIZEOF(my_decomp_master));
  cinfo->master = &master->pub;
  master->pub.prepare_for_output_pass = prepare_for_output_pass;
  master->pub.finish_output_pass = finish_output_pass;

  master->pub.is_dummy_pass = FALSE;

  master_selection(cinfo);
}
