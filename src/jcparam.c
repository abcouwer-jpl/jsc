/*
 * jcparam.c
 *
 * Copyright (C) 1991-1998, Thomas G. Lane.
 * Modified 2003-2019 by Guido Vollbeding.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
 *
 * This file contains optional default-setting code for the JPEG compressor.
 * Applications do not have to use this file, but those that don't use it
 * must know a lot more about the innards of the JPEG code.
 */

#include "jsc/jpegint.h"

// Abcouwer JSC 2021 - make function
JSC_METHODDEF(void)
jpeg_set_comp(j_compress_ptr cinfo,
        JINT index, JINT id, JINT hsamp, JINT vsamp,
        JINT quant, JINT dctbl, JINT actbl)
{
    JSC_ASSERT(cinfo != NULL);
    JSC_ASSERT(cinfo->comp_info != NULL);
    JSC_ASSERT_2(index < MAX_COMPONENTS, index, MAX_COMPONENTS);
    JSC_ASSERT_1(id >= 0, id);
    JSC_ASSERT_1(id <= 255, id);
    JSC_ASSERT_1(hsamp > 0, hsamp);
    JSC_ASSERT_2(hsamp <= MAX_SAMP_FACTOR, hsamp, MAX_SAMP_FACTOR);
    JSC_ASSERT_1(vsamp > 0, vsamp);
    JSC_ASSERT_2(vsamp <= MAX_SAMP_FACTOR, vsamp, MAX_SAMP_FACTOR);
    JSC_ASSERT_1(quant >= 0, quant);
    JSC_ASSERT_2(quant < NUM_QUANT_TBLS, quant, NUM_QUANT_TBLS);
    JSC_ASSERT_1(dctbl >= 0, dctbl);
    JSC_ASSERT_2(dctbl < NUM_HUFF_TBLS, dctbl, NUM_HUFF_TBLS);
    JSC_ASSERT_1(actbl >= 0, actbl);
    JSC_ASSERT_2(actbl < NUM_HUFF_TBLS, actbl, NUM_HUFF_TBLS);

    jpeg_component_info * compptr;
    compptr = &cinfo->comp_info[(index)];
    JSC_ASSERT(compptr != NULL);

    compptr->component_id = (id);
    compptr->h_samp_factor = (hsamp);
    compptr->v_samp_factor = (vsamp);
    compptr->quant_tbl_no = (quant);
    compptr->dc_tbl_no = (dctbl);
    compptr->ac_tbl_no = (actbl);
}


/*
 * Quantization table setup routines
 */

JSC_GLOBAL(void)
jpeg_add_quant_table (j_compress_ptr cinfo, JINT which_tbl,
		      const JUINT *basic_table,
		      JINT scale_factor, boolean force_baseline)
/* Define a quantization table equal to the basic_table times
 * a scale factor (given as a percentage).
 * If force_baseline is TRUE, the computed quantization table entries
 * are limited to 1..255 for JPEG baseline compatibility.
 */
{
    JSC_ASSERT(cinfo != NULL);

  JQUANT_TBL ** qtblptr;
  JINT i;
  JLONG temp;

  /* Safety check to ensure start_compress not called yet. */
  JSC_ASSERT_2(cinfo->global_state == CSTATE_START,
          cinfo->global_state, CSTATE_START);
  JSC_ASSERT_1(which_tbl >= 0, which_tbl);
  JSC_ASSERT_2(which_tbl < NUM_QUANT_TBLS, which_tbl, NUM_QUANT_TBLS);

  qtblptr = & cinfo->quant_tbl_ptrs[which_tbl];

  if (*qtblptr == NULL)
    *qtblptr = jpeg_get_mem_quant_table((j_common_ptr) cinfo);

  for (i = 0; i < DCTSIZE2; i++) {
    temp = ((JLONG) basic_table[i] * scale_factor + 50L) / 100L;
    /* limit the values to the valid range */
    if (temp <= 0L) temp = 1L;
    if (temp > 32767L) temp = 32767L; /* max quantizer needed for 12 bits */
    if (force_baseline && temp > 255L)
      temp = 255L;		/* limit to baseline range if requested */
    (*qtblptr)->quantval[i] = (UINT16) temp;
  }

  /* Initialize sent_table FALSE so table will be written to JPEG file. */
  (*qtblptr)->sent_table = FALSE;
}


/* These are the sample quantization tables given in JPEG spec section K.1.
 * The spec says that the values given produce "good" quality, and
 * when divided by 2, "very good" quality.
 */
static const JUINT std_luminance_quant_tbl[DCTSIZE2] = {
  16,  11,  10,  16,  24,  40,  51,  61,
  12,  12,  14,  19,  26,  58,  60,  55,
  14,  13,  16,  24,  40,  57,  69,  56,
  14,  17,  22,  29,  51,  87,  80,  62,
  18,  22,  37,  56,  68, 109, 103,  77,
  24,  35,  55,  64,  81, 104, 113,  92,
  49,  64,  78,  87, 103, 121, 120, 101,
  72,  92,  95,  98, 112, 100, 103,  99
};
static const JUINT std_chrominance_quant_tbl[DCTSIZE2] = {
  17,  18,  24,  47,  99,  99,  99,  99,
  18,  21,  26,  66,  99,  99,  99,  99,
  24,  26,  56,  99,  99,  99,  99,  99,
  47,  66,  99,  99,  99,  99,  99,  99,
  99,  99,  99,  99,  99,  99,  99,  99,
  99,  99,  99,  99,  99,  99,  99,  99,
  99,  99,  99,  99,  99,  99,  99,  99,
  99,  99,  99,  99,  99,  99,  99,  99
};

// Abcouwer JSC 2021 - remove knobs

JSC_GLOBAL(void)
jpeg_set_linear_quality (j_compress_ptr cinfo, JINT scale_factor,
			 boolean force_baseline)
/* Set or change the 'quality' (quantization) setting, using default tables
 * and a straight percentage-scaling quality scale.  In most cases it's better
 * to use jpeg_set_quality (below); this entry point is provided for
 * applications that insist on a linear percentage scaling.
 */
{
  /* Set up two quantization tables using the specified scaling */
  jpeg_add_quant_table(cinfo, 0, std_luminance_quant_tbl,
		       scale_factor, force_baseline);
  jpeg_add_quant_table(cinfo, 1, std_chrominance_quant_tbl,
		       scale_factor, force_baseline);
}


JSC_GLOBAL(JINT)
jpeg_quality_scaling (JINT quality)
/* Convert a user-specified quality rating to a percentage scaling factor
 * for an underlying quantization table, using our recommended scaling curve.
 * The input 'quality' factor should be 0 (terrible) to 100 (very good).
 */
{
  /* Safety limit on quality factor.  Convert 0 to 1 to avoid zero divide. */
  if (quality <= 0) quality = 1;
  if (quality > 100) quality = 100;

  /* The basic table is used as-is (scaling 100) for a quality of 50.
   * Qualities 50..100 are converted to scaling percentage 200 - 2*Q;
   * note that at Q=100 the scaling is 0, which will cause jpeg_add_quant_table
   * to make all the table entries 1 (hence, minimum quantization loss).
   * Qualities 1..50 are converted to scaling percentage 5000/Q.
   */
  if (quality < 50)
    quality = 5000 / quality;
  else
    quality = 200 - quality*2;

  return quality;
}


JSC_GLOBAL(void)
jpeg_set_quality (j_compress_ptr cinfo, JINT quality, boolean force_baseline)
/* Set or change the 'quality' (quantization) setting, using default tables.
 * This is the standard quality-adjusting entry point for typical user
 * interfaces; only those who want detailed control over quantization tables
 * would use the preceding routines directly.
 */
{
  JSC_ASSERT(cinfo != NULL);

  /* Convert user 0-100 rating to percentage scaling */
  quality = jpeg_quality_scaling(quality);

  /* Set up standard quality tables */
  jpeg_set_linear_quality(cinfo, quality, force_baseline);
}


/*
 * Reset standard Huffman tables
 */

JSC_LOCAL(void)
std_huff_tables (j_compress_ptr cinfo)
{
  if (cinfo->dc_huff_tbl_ptrs[0] != NULL)
    (void) jpeg_std_huff_table((j_common_ptr) cinfo, TRUE, 0);

  if (cinfo->ac_huff_tbl_ptrs[0] != NULL)
    (void) jpeg_std_huff_table((j_common_ptr) cinfo, FALSE, 0);

  if (cinfo->dc_huff_tbl_ptrs[1] != NULL)
    (void) jpeg_std_huff_table((j_common_ptr) cinfo, TRUE, 1);

  if (cinfo->ac_huff_tbl_ptrs[1] != NULL)
    (void) jpeg_std_huff_table((j_common_ptr) cinfo, FALSE, 1);
}


/*
 * Default parameter setup for compression.
 *
 * Applications that don't choose to use this routine must do their
 * own setup of all these parameters.  Alternately, you can call this
 * to establish defaults and then alter parameters selectively.  This
 * is the recommended approach since, if we add any new parameters,
 * your code will still work (they'll be set to reasonable defaults).
 */

JSC_GLOBAL(void)
jpeg_set_defaults (j_compress_ptr cinfo)
{
  JSC_ASSERT(cinfo != NULL);

  JINT i;

  /* Safety check to ensure start_compress not called yet. */
  JSC_ASSERT_2(cinfo->global_state == CSTATE_START,
          cinfo->global_state, CSTATE_START);

  /* Allocate comp_info array large enough for maximum component count.
   * Array is made permanent in case application wants to compress
   * multiple images at same param settings.
   */
  if (cinfo->comp_info == NULL)
    cinfo->comp_info = (jpeg_component_info *)
      (*cinfo->mem->get_mem) ((j_common_ptr) cinfo, JPOOL_PERMANENT,
				  MAX_COMPONENTS * SIZEOF(jpeg_component_info));

  /* Initialize everything not dependent on the color space */

  cinfo->scale_num = 1;		/* 1:1 scaling */
  cinfo->scale_denom = 1;
  cinfo->data_precision = BITS_IN_JSAMPLE;
  /* Set up two quantization tables using default quality of 75 */
  jpeg_set_quality(cinfo, 75, TRUE);
  /* Reset standard Huffman tables */
  std_huff_tables(cinfo);

  /* Initialize default arithmetic coding conditioning */
  for (i = 0; i < NUM_ARITH_TBLS; i++) {
    cinfo->arith_dc_L[i] = 0;
    cinfo->arith_dc_U[i] = 1;
    cinfo->arith_ac_K[i] = 5;
  }

  /* Default is no multiple-scan output */
  cinfo->scan_info = NULL;
  cinfo->num_scans = 0;

  // Abcouwer JSC 2021 - remove raw data in
  // Abcouwer JSC 2021 - remove arithmetic encoding
  // Abcouwer JSC 2021 - remove optimized coding
  // Abcouwer JSC 2021 - remove CCIR601_sampling

  /* By default, apply fancy downsampling */
  cinfo->do_fancy_downsampling = TRUE;

  /* No input smoothing */
  cinfo->smoothing_factor = 0;

  // Abcouwer JSC 2021 - no dct_method member - only float dct supported

  /* No restart markers */
  cinfo->restart_interval = 0;
  cinfo->restart_in_rows = 0;

  /* Fill in default JFIF marker parameters.  Note that whether the marker
   * will actually be written is determined by jpeg_set_colorspace.
   *
   * By default, the library emits JFIF version code 1.01.
   * An application that wants to emit JFIF 1.02 extension markers should set
   * JFIF_minor_version to 2.  We could probably get away with just defaulting
   * to 1.02, but there may still be some decoders in use that will complain
   * about that; saying 1.01 should minimize compatibility problems.
   *
   * For wide gamut colorspaces (BG_RGB and BG_YCC), the major version will be
   * overridden by jpeg_set_colorspace and set to 2.
   */
  cinfo->JFIF_major_version = 1; /* Default JFIF version = 1.01 */
  cinfo->JFIF_minor_version = 1;
  cinfo->density_unit = 0;	/* Pixel size is unknown by default */
  cinfo->X_density = 1;		/* Pixel aspect ratio is square by default */
  cinfo->Y_density = 1;

  // Abcouwer JSC 2021 - remove support for subtract green / color transform

  /* Choose JPEG colorspace based on input space, set defaults accordingly */

  jpeg_default_colorspace(cinfo);
}


/*
 * Select an appropriate JPEG colorspace for in_color_space.
 */

JSC_GLOBAL(void)
jpeg_default_colorspace (j_compress_ptr cinfo)
{
  JSC_ASSERT(cinfo != NULL);

  switch (cinfo->in_color_space) {
  case JCS_UNKNOWN:
    jpeg_set_colorspace(cinfo, JCS_UNKNOWN);
    break;
  case JCS_GRAYSCALE:
    jpeg_set_colorspace(cinfo, JCS_GRAYSCALE);
    break;
  case JCS_RGB:
    jpeg_set_colorspace(cinfo, JCS_YCbCr);
    break;
  case JCS_YCbCr:
    jpeg_set_colorspace(cinfo, JCS_YCbCr);
    break;
  case JCS_CMYK:
    jpeg_set_colorspace(cinfo, JCS_CMYK); /* By default, no translation */
    break;
  case JCS_YCCK:
    jpeg_set_colorspace(cinfo, JCS_YCCK);
    break;
  case JCS_BG_RGB:
    /* No translation for now -- conversion to BG_YCC not yet supportet */
    jpeg_set_colorspace(cinfo, JCS_BG_RGB);
    break;
  case JCS_BG_YCC:
    jpeg_set_colorspace(cinfo, JCS_BG_YCC);
    break;
  default:
    JSC_ASSERT_1(0, cinfo->in_color_space);
  }
}


/*
 * Set the JPEG colorspace, and choose colorspace-dependent default values.
 */

JSC_GLOBAL(void)
jpeg_set_colorspace (j_compress_ptr cinfo, J_COLOR_SPACE colorspace)
{
  JSC_ASSERT(cinfo != NULL);

  JINT ci;

  // Abcouwer JSC 2021 - move SET_COMP def out of fn

  /* Safety check to ensure start_compress not called yet. */
  JSC_ASSERT_2(cinfo->global_state == CSTATE_START,
          cinfo->global_state, CSTATE_START);

  /* For all colorspaces, we use Q and Huff tables 0 for luminance components,
   * tables 1 for chrominance components.
   */

  cinfo->write_JFIF_header = FALSE; /* No marker for non-JFIF colorspaces */
  cinfo->write_Adobe_marker = FALSE; /* write no Adobe marker by default */

  switch (colorspace) {
  case JCS_UNKNOWN:
    cinfo->num_components = cinfo->input_components;
    JSC_ASSERT_1(cinfo->num_components >= 1, cinfo->num_components);
    JSC_ASSERT_2(cinfo->num_components <= MAX_COMPONENTS,
            cinfo->num_components, MAX_COMPONENTS);
    for (ci = 0; ci < cinfo->num_components; ci++) {
        jpeg_set_comp(cinfo,ci, ci, 1,1, 0, 0,0);
    }
    break;
  case JCS_GRAYSCALE:
    cinfo->write_JFIF_header = TRUE; /* Write a JFIF marker */
    cinfo->num_components = 1;
    /* JFIF specifies component ID 1 */
    jpeg_set_comp(cinfo, 0, 0x01, 1,1, 0, 0,0);
    break;
  case JCS_RGB:
    cinfo->write_Adobe_marker = TRUE; /* write Adobe marker to flag RGB */
    cinfo->num_components = 3;
    // Abcouwer JSC 2021 - remove support for subtract green / color transform
    jpeg_set_comp(cinfo, 0, 0x52 /* 'R' */, 1,1, 0, 0, 0);
    jpeg_set_comp(cinfo, 1, 0x47 /* 'G' */, 1,1, 0, 0, 0);
    jpeg_set_comp(cinfo, 2, 0x42 /* 'B' */, 1,1, 0, 0, 0);
    break;
  case JCS_YCbCr:
    cinfo->write_JFIF_header = TRUE; /* Write a JFIF marker */
    cinfo->num_components = 3;
    /* JFIF specifies component IDs 1,2,3 */
    /* We default to 2x2 subsamples of chrominance */
    jpeg_set_comp(cinfo, 0, 0x01, 2,2, 0, 0,0);
    jpeg_set_comp(cinfo, 1, 0x02, 1,1, 1, 1,1);
    jpeg_set_comp(cinfo, 2, 0x03, 1,1, 1, 1,1);
    break;
  case JCS_CMYK:
    cinfo->write_Adobe_marker = TRUE; /* write Adobe marker to flag CMYK */
    cinfo->num_components = 4;
    jpeg_set_comp(cinfo, 0, 0x43 /* 'C' */, 1,1, 0, 0,0);
    jpeg_set_comp(cinfo, 1, 0x4D /* 'M' */, 1,1, 0, 0,0);
    jpeg_set_comp(cinfo, 2, 0x59 /* 'Y' */, 1,1, 0, 0,0);
    jpeg_set_comp(cinfo, 3, 0x4B /* 'K' */, 1,1, 0, 0,0);
    break;
  case JCS_YCCK:
    cinfo->write_Adobe_marker = TRUE; /* write Adobe marker to flag YCCK */
    cinfo->num_components = 4;
    jpeg_set_comp(cinfo, 0, 0x01, 2,2, 0, 0,0);
    jpeg_set_comp(cinfo, 1, 0x02, 1,1, 1, 1,1);
    jpeg_set_comp(cinfo, 2, 0x03, 1,1, 1, 1,1);
    jpeg_set_comp(cinfo, 3, 0x04, 2,2, 0, 0,0);
    break;
  case JCS_BG_RGB:
    cinfo->write_JFIF_header = TRUE; /* Write a JFIF marker */
    cinfo->JFIF_major_version = 2;   /* Set JFIF major version = 2 */
    cinfo->num_components = 3;
    /* Add offset 0x20 to the normal R/G/B component IDs */
    jpeg_set_comp(cinfo, 0, 0x72 /* 'r' */, 1,1, 0, 0, 0);
    jpeg_set_comp(cinfo, 1, 0x67 /* 'g' */, 1,1, 0, 0, 0);
    jpeg_set_comp(cinfo, 2, 0x62 /* 'b' */, 1,1, 0, 0, 0);
    break;
  case JCS_BG_YCC:
    cinfo->write_JFIF_header = TRUE; /* Write a JFIF marker */
    cinfo->JFIF_major_version = 2;   /* Set JFIF major version = 2 */
    cinfo->num_components = 3;
    /* Add offset 0x20 to the normal Cb/Cr component IDs */
    /* We default to 2x2 subsamples of chrominance */
    jpeg_set_comp(cinfo, 0, 0x01, 2,2, 0, 0,0);
    jpeg_set_comp(cinfo, 1, 0x22, 1,1, 1, 1,1);
    jpeg_set_comp(cinfo, 2, 0x23, 1,1, 1, 1,1);
    break;
  default:
    JSC_ASSERT_1(0, colorspace);
  }

  cinfo->jpeg_color_space = colorspace;
}

// Abcouwer JSC 2021 - remove progressive encoding
