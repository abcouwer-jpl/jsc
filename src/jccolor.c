/*
 * jccolor.c
 *
 * Copyright (C) 1991-1996, Thomas G. Lane.
 * Modified 2011-2019 by Guido Vollbeding.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
 *
 * This file contains input colorspace conversion routines.
 */

#include "jsc/jpegint.h"

/* Private subobject */

typedef struct {
  struct jpeg_color_converter pub; /* public fields */

  /* Private state for RGB->YCC conversion */
  INT32 * rgb_ycc_tab;		/* => table for RGB to YCbCr conversion */
} my_color_converter;


/**************** RGB -> YCbCr conversion: most common case **************/

/*
 * YCbCr is defined per Recommendation ITU-R BT.601-7 (03/2011),
 * previously known as Recommendation CCIR 601-1, except that Cb and Cr
 * are normalized to the range 0..MAXJSAMPLE rather than -0.5 .. 0.5.
 * sRGB (standard RGB color space) is defined per IEC 61966-2-1:1999.
 * sYCC (standard luma-chroma-chroma color space with extended gamut)
 * is defined per IEC 61966-2-1:1999 Amendment A1:2003 Annex F.
 * bg-sRGB and bg-sYCC (big gamut standard color spaces)
 * are defined per IEC 61966-2-1:1999 Amendment A1:2003 Annex G.
 * Note that the derived conversion coefficients given in some of these
 * documents are imprecise.  The general conversion equations are
 *	Y  = Kr * R + (1 - Kr - Kb) * G + Kb * B
 *	Cb = 0.5 * (B - Y) / (1 - Kb)
 *	Cr = 0.5 * (R - Y) / (1 - Kr)
 * With Kr = 0.299 and Kb = 0.114 (derived according to SMPTE RP 177-1993
 * from the 1953 FCC NTSC primaries and CIE Illuminant C),
 * the conversion equations to be implemented are therefore
 *	Y  =  0.299 * R + 0.587 * G + 0.114 * B
 *	Cb = -0.168735892 * R - 0.331264108 * G + 0.5 * B + CENTERJSAMPLE
 *	Cr =  0.5 * R - 0.418687589 * G - 0.081312411 * B + CENTERJSAMPLE
 * Note: older versions of the IJG code used a zero offset of MAXJSAMPLE/2,
 * rather than CENTERJSAMPLE, for Cb and Cr.  This gave equal positive and
 * negative swings for Cb/Cr, but meant that grayscale values (Cb=Cr=0)
 * were not represented exactly.  Now we sacrifice exact representation of
 * maximum red and maximum blue in order to get exact grayscales.
 *
 * To avoid floating-point arithmetic, we represent the fractional constants
 * as integers scaled up by 2^16 (about 4 digits precision); we have to divide
 * the products by 2^16, with appropriate rounding, to get the correct answer.
 *
 * For even more speed, we avoid doing any multiplications in the inner loop
 * by precalculating the constants times R,G,B for all possible values.
 * For 8-bit JSAMPLEs this is very reasonable (only 256 entries per table);
 * for 9-bit to 12-bit samples it is still acceptable.  It's not very
 * reasonable for 16-bit samples, but if you want lossless storage you
 * shouldn't be changing colorspace anyway.
 * The CENTERJSAMPLE offsets and the rounding fudge-factor of 0.5 are included
 * in the tables to save adding them separately in the inner loop.
 */

#define SCALEBITS	16	/* speediest right-shift on some machines */
#define CBCR_OFFSET	((INT32) CENTERJSAMPLE << SCALEBITS)
#define ONE_HALF	((INT32) 1 << (SCALEBITS-1))
#define FIX(x)		((INT32) ((x) * (1L<<SCALEBITS) + 0.5))

/* We allocate one big table and divide it up into eight parts, instead of
 * doing eight alloc_small requests.  This lets us use a single table base
 * address, which can be held in a register in the inner loops on many
 * machines (more than can hold all eight addresses, anyway).
 */

#define R_Y_OFF		0			/* offset to R => Y section */
#define G_Y_OFF		(1*(MAXJSAMPLE+1))	/* offset to G => Y section */
#define B_Y_OFF		(2*(MAXJSAMPLE+1))	/* etc. */
#define R_CB_OFF	(3*(MAXJSAMPLE+1))
#define G_CB_OFF	(4*(MAXJSAMPLE+1))
#define B_CB_OFF	(5*(MAXJSAMPLE+1))
#define R_CR_OFF	B_CB_OFF		/* B=>Cb, R=>Cr are the same */
#define G_CR_OFF	(6*(MAXJSAMPLE+1))
#define B_CR_OFF	(7*(MAXJSAMPLE+1))
#define TABLE_SIZE	(8*(MAXJSAMPLE+1))


/*
 * Initialize for RGB->YCC colorspace conversion.
 */

JSC_METHODDEF(void)
rgb_ycc_start (j_compress_ptr cinfo)
{
  JSC_ASSERT(cinfo != NULL);
  my_color_converter * cconvert = (my_color_converter *) cinfo->cconvert;
  JSC_ASSERT(cconvert != NULL);
  INT32 * rgb_ycc_tab;
  INT32 i;

  /* Allocate and fill in the conversion tables. */
  cconvert->rgb_ycc_tab = rgb_ycc_tab = (INT32 *)
    (*cinfo->mem->get_mem) ((j_common_ptr) cinfo, JPOOL_IMAGE,
				TABLE_SIZE * SIZEOF(INT32));

  for (i = 0; i <= MAXJSAMPLE; i++) {
    rgb_ycc_tab[i+R_Y_OFF] = FIX(0.299) * i;
    rgb_ycc_tab[i+G_Y_OFF] = FIX(0.587) * i;
    rgb_ycc_tab[i+B_Y_OFF] = FIX(0.114) * i   + ONE_HALF;
    rgb_ycc_tab[i+R_CB_OFF] = (- FIX(0.168735892)) * i;
    rgb_ycc_tab[i+G_CB_OFF] = (- FIX(0.331264108)) * i;
    /* We use a rounding fudge-factor of 0.5-epsilon for Cb and Cr.
     * This ensures that the maximum output will round to MAXJSAMPLE
     * not MAXJSAMPLE+1, and thus that we don't have to range-limit.
     */
    rgb_ycc_tab[i+B_CB_OFF] = FIX(0.5) * i    + CBCR_OFFSET + ONE_HALF-1;
/*  B=>Cb and R=>Cr tables are the same
    rgb_ycc_tab[i+R_CR_OFF] = FIX(0.5) * i    + CBCR_OFFSET + ONE_HALF-1;
*/
    rgb_ycc_tab[i+G_CR_OFF] = (- FIX(0.418687589)) * i;
    rgb_ycc_tab[i+B_CR_OFF] = (- FIX(0.081312411)) * i;
  }
}


/*
 * Convert some rows of samples to the JPEG colorspace.
 *
 * Note that we change from the application's interleaved-pixel format
 * to our internal noninterleaved, one-plane-per-component format.  The
 * input buffer is therefore three times as wide as the output buffer.
 *
 * A starting row offset is provided only for the output buffer.  The
 * caller can easily adjust the passed input_buf value to accommodate
 * any row offset required on that side.
 */

JSC_METHODDEF(void)
rgb_ycc_convert (j_compress_ptr cinfo,
		 JSAMPARRAY input_buf, JSAMPIMAGE output_buf,
		 JDIMENSION output_row, JINT num_rows)
{
  JSC_ASSERT(cinfo != NULL);
  my_color_converter * cconvert = (my_color_converter *) cinfo->cconvert;
  JSC_ASSERT(cconvert != NULL);
  register JINT r;
  register JINT g;
  register JINT b;
  register INT32 * ctab = cconvert->rgb_ycc_tab;
  register JSAMPROW inptr;
  register JSAMPROW outptr0, outptr1, outptr2;
  register JDIMENSION col;
  JDIMENSION num_cols = cinfo->image_width;

  while (num_rows > 0) {
    num_rows--;
    inptr = *input_buf++;
    outptr0 = output_buf[0][output_row];
    outptr1 = output_buf[1][output_row];
    outptr2 = output_buf[2][output_row];
    output_row++;
    for (col = 0; col < num_cols; col++) {
      r = GETJSAMPLE(inptr[RGB_RED]);
      g = GETJSAMPLE(inptr[RGB_GREEN]);
      b = GETJSAMPLE(inptr[RGB_BLUE]);
      inptr += RGB_PIXELSIZE;
      /* If the inputs are 0..MAXJSAMPLE, the outputs of these equations
       * must be too; we do not need an explicit range-limiting operation.
       * Hence the value being shifted is never negative, and we don't
       * need the general RIGHT_SHIFT macro.
       */
      /* Y */
      outptr0[col] = (JSAMPLE)
		((ctab[r+R_Y_OFF] + ctab[g+G_Y_OFF] + ctab[b+B_Y_OFF])
		 >> SCALEBITS);
      /* Cb */
      outptr1[col] = (JSAMPLE)
		((ctab[r+R_CB_OFF] + ctab[g+G_CB_OFF] + ctab[b+B_CB_OFF])
		 >> SCALEBITS);
      /* Cr */
      outptr2[col] = (JSAMPLE)
		((ctab[r+R_CR_OFF] + ctab[g+G_CR_OFF] + ctab[b+B_CR_OFF])
		 >> SCALEBITS);
    }
  }
}


/**************** Cases other than RGB -> YCbCr **************/


/*
 * Convert some rows of samples to the JPEG colorspace.
 * This version handles RGB->grayscale conversion, which is the same
 * as the RGB->Y portion of RGB->YCbCr.
 * We assume rgb_ycc_start has been called (we only use the Y tables).
 */

JSC_METHODDEF(void)
rgb_gray_convert (j_compress_ptr cinfo,
		  JSAMPARRAY input_buf, JSAMPIMAGE output_buf,
		  JDIMENSION output_row, JINT num_rows)
{
  JSC_ASSERT(cinfo != NULL);
  my_color_converter * cconvert = (my_color_converter *) cinfo->cconvert;
  JSC_ASSERT(cconvert != NULL);
  register JINT r;
  register JINT g;
  register JINT b;
  register INT32 * ctab = cconvert->rgb_ycc_tab;
  register JSAMPROW inptr;
  register JSAMPROW outptr;
  register JDIMENSION col;
  JDIMENSION num_cols = cinfo->image_width;

  while (num_rows > 0) {
    num_rows--;
    inptr = *input_buf++;
    outptr = output_buf[0][output_row++];
    for (col = 0; col < num_cols; col++) {
      r = GETJSAMPLE(inptr[RGB_RED]);
      g = GETJSAMPLE(inptr[RGB_GREEN]);
      b = GETJSAMPLE(inptr[RGB_BLUE]);
      inptr += RGB_PIXELSIZE;
      /* Y */
      outptr[col] = (JSAMPLE)
		((ctab[r+R_Y_OFF] + ctab[g+G_Y_OFF] + ctab[b+B_Y_OFF])
		 >> SCALEBITS);
    }
  }
}


/*
 * Convert some rows of samples to the JPEG colorspace.
 * This version handles Adobe-style CMYK->YCCK conversion,
 * where we convert R=1-C, G=1-M, and B=1-Y to YCbCr using the
 * same conversion as above, while passing K (black) unchanged.
 * We assume rgb_ycc_start has been called.
 */

JSC_METHODDEF(void)
cmyk_ycck_convert (j_compress_ptr cinfo,
		   JSAMPARRAY input_buf, JSAMPIMAGE output_buf,
		   JDIMENSION output_row, JINT num_rows)
{
  JSC_ASSERT(cinfo != NULL);
  my_color_converter * cconvert = (my_color_converter *) cinfo->cconvert;
  JSC_ASSERT(cconvert != NULL);
  register JINT r;
  register JINT g;
  register JINT b;
  register INT32 * ctab = cconvert->rgb_ycc_tab;
  register JSAMPROW inptr;
  register JSAMPROW outptr0, outptr1, outptr2, outptr3;
  register JDIMENSION col;
  JDIMENSION num_cols = cinfo->image_width;

  while (num_rows > 0) {
    num_rows--;
    inptr = *input_buf++;
    outptr0 = output_buf[0][output_row];
    outptr1 = output_buf[1][output_row];
    outptr2 = output_buf[2][output_row];
    outptr3 = output_buf[3][output_row];
    output_row++;
    for (col = 0; col < num_cols; col++) {
      r = MAXJSAMPLE - GETJSAMPLE(inptr[0]);
      g = MAXJSAMPLE - GETJSAMPLE(inptr[1]);
      b = MAXJSAMPLE - GETJSAMPLE(inptr[2]);
      /* K passes through as-is */
      outptr3[col] = inptr[3];	/* don't need GETJSAMPLE here */
      inptr += 4;
      /* If the inputs are 0..MAXJSAMPLE, the outputs of these equations
       * must be too; we do not need an explicit range-limiting operation.
       * Hence the value being shifted is never negative, and we don't
       * need the general RIGHT_SHIFT macro.
       */
      /* Y */
      outptr0[col] = (JSAMPLE)
		((ctab[r+R_Y_OFF] + ctab[g+G_Y_OFF] + ctab[b+B_Y_OFF])
		 >> SCALEBITS);
      /* Cb */
      outptr1[col] = (JSAMPLE)
		((ctab[r+R_CB_OFF] + ctab[g+G_CB_OFF] + ctab[b+B_CB_OFF])
		 >> SCALEBITS);
      /* Cr */
      outptr2[col] = (JSAMPLE)
		((ctab[r+R_CR_OFF] + ctab[g+G_CR_OFF] + ctab[b+B_CR_OFF])
		 >> SCALEBITS);
    }
  }
}

// Abcouwer JSC 2021 - remove support for subtract green / color transform

/*
 * Convert some rows of samples to the JPEG colorspace.
 * This version handles grayscale output with no conversion.
 * The source can be either plain grayscale or YCC (since Y == gray).
 */

JSC_METHODDEF(void)
grayscale_convert (j_compress_ptr cinfo,
		   JSAMPARRAY input_buf, JSAMPIMAGE output_buf,
		   JDIMENSION output_row, JINT num_rows)
{
  JSC_ASSERT(cinfo != NULL);
  register JSAMPROW inptr;
  register JSAMPROW outptr;
  register JDIMENSION count;
  register JINT instride = cinfo->input_components;
  JDIMENSION num_cols = cinfo->image_width;

  while (num_rows > 0) {
    num_rows--;
    inptr = *input_buf++;
    outptr = output_buf[0][output_row++];
    for (count = num_cols; count > 0; count--) {
      *outptr++ = *inptr;	/* don't need GETJSAMPLE() here */
      inptr += instride;
    }
  }
}


/*
 * Convert some rows of samples to the JPEG colorspace.
 * No colorspace conversion, but change from interleaved
 * to separate-planes representation.
 */

JSC_METHODDEF(void)
rgb_convert (j_compress_ptr cinfo,
	     JSAMPARRAY input_buf, JSAMPIMAGE output_buf,
	     JDIMENSION output_row, JINT num_rows)
{
  JSC_ASSERT(cinfo != NULL);

  register JSAMPROW inptr;
  register JSAMPROW outptr0, outptr1, outptr2;
  register JDIMENSION col;
  JDIMENSION num_cols = cinfo->image_width;

  while (num_rows > 0) {
    num_rows--;
    inptr = *input_buf++;
    outptr0 = output_buf[0][output_row];
    outptr1 = output_buf[1][output_row];
    outptr2 = output_buf[2][output_row];
    output_row++;
    for (col = 0; col < num_cols; col++) {
      /* We can dispense with GETJSAMPLE() here */
      outptr0[col] = inptr[RGB_RED];
      outptr1[col] = inptr[RGB_GREEN];
      outptr2[col] = inptr[RGB_BLUE];
      inptr += RGB_PIXELSIZE;
    }
  }
}


/*
 * Convert some rows of samples to the JPEG colorspace.
 * This version handles multi-component colorspaces without conversion.
 * We assume input_components == num_components.
 */

JSC_METHODDEF(void)
null_convert (j_compress_ptr cinfo,
	      JSAMPARRAY input_buf, JSAMPIMAGE output_buf,
	      JDIMENSION output_row, JINT num_rows)
{
  JSC_ASSERT(cinfo != NULL);
  register JSAMPROW inptr;
  register JSAMPROW outptr;
  register JDIMENSION count;
  register JINT num_comps = cinfo->num_components;
  JDIMENSION num_cols = cinfo->image_width;
  JINT ci;

  while (num_rows > 0) {
    num_rows--;
    /* It seems fastest to make a separate pass for each component. */
    for (ci = 0; ci < num_comps; ci++) {
      inptr = input_buf[0] + ci;
      outptr = output_buf[ci][output_row];
      for (count = num_cols; count > 0; count--) {
	*outptr++ = *inptr;	/* don't need GETJSAMPLE() here */
	inptr += num_comps;
      }
    }
    input_buf++;
    output_row++;
  }
}


/*
 * Empty method for start_pass.
 */

JSC_METHODDEF(void)
null_method (j_compress_ptr cinfo)
{
    JSC_ASSERT(cinfo != NULL);
  /* no work needed */
}


/*
 * Module initialization routine for input colorspace conversion.
 */

JSC_GLOBAL(void)
jinit_color_converter (j_compress_ptr cinfo)
{
    JSC_ASSERT(cinfo != NULL);
    // Abcouwer JSC 2021 - don't support RGB pixel sizes other than 3
    JSC_COMPILE_ASSERT(RGB_PIXELSIZE == 3, bad_rgb_pixelsize);

  my_color_converter * cconvert;

  cconvert = (my_color_converter *) (*cinfo->mem->get_mem)
    ((j_common_ptr) cinfo, JPOOL_IMAGE, SIZEOF(my_color_converter));
  JSC_ASSERT(cconvert != NULL);
  cinfo->cconvert = &cconvert->pub;
  /* set start_pass to null method until we find out differently */
  cconvert->pub.start_pass = null_method;

  /* Make sure input_components agrees with in_color_space */
  switch (cinfo->in_color_space) {
  case JCS_GRAYSCALE:
    JSC_ASSERT_1(cinfo->input_components == 1, cinfo->input_components);
    break;

  case JCS_RGB:
  case JCS_BG_RGB:
      // Abcouwer JSC 2021 - don't support RGB pixel sizes other than 3
  case JCS_YCbCr:
  case JCS_BG_YCC:
      JSC_ASSERT_1(cinfo->input_components == 3, cinfo->input_components);
    break;

  case JCS_CMYK:
  case JCS_YCCK:
      JSC_ASSERT_1(cinfo->input_components == 4, cinfo->input_components);
    break;

  default:			/* JCS_UNKNOWN can be anything */
    JSC_ASSERT_1(cinfo->input_components > 0, cinfo->input_components);
  }

  // Abcouwer JSC 2021 - remove support for subtract green / color transform

  /* Check num_components, set conversion method based on requested space */
  switch (cinfo->jpeg_color_space) {
  case JCS_GRAYSCALE:
    JSC_ASSERT_1(cinfo->num_components == 1, cinfo->num_components);
    switch (cinfo->in_color_space) {
    case JCS_GRAYSCALE:
    case JCS_YCbCr:
    case JCS_BG_YCC:
      cconvert->pub.color_convert = grayscale_convert;
      break;
    case JCS_RGB:
      cconvert->pub.start_pass = rgb_ycc_start;
      cconvert->pub.color_convert = rgb_gray_convert;
      break;
    default:
        JSC_ASSERT_1(0, cinfo->in_color_space);
    }
    break;

  case JCS_RGB:
  case JCS_BG_RGB:
      JSC_ASSERT_1(cinfo->num_components == 3, cinfo->num_components);
      JSC_ASSERT_2(cinfo->in_color_space == cinfo->jpeg_color_space,
              cinfo->in_color_space, cinfo->jpeg_color_space);
      // Abcouwer JSC 2021 - remove support for subtract green / color transform
      cconvert->pub.color_convert = rgb_convert;
    break;

  case JCS_YCbCr:
    JSC_ASSERT_1(cinfo->num_components == 3, cinfo->num_components);
    switch (cinfo->in_color_space) {
    case JCS_RGB:
      cconvert->pub.start_pass = rgb_ycc_start;
      cconvert->pub.color_convert = rgb_ycc_convert;
      break;
    case JCS_YCbCr:
      cconvert->pub.color_convert = null_convert;
      break;
    default:
        JSC_ASSERT_1(0, cinfo->in_color_space);
    }
    break;

  case JCS_BG_YCC:
    JSC_ASSERT_1(cinfo->num_components == 3, cinfo->num_components);
    switch (cinfo->in_color_space) {
    case JCS_RGB:
      /* For conversion from normal RGB input to BG_YCC representation,
       * the Cb/Cr values are first computed as usual, and then
       * quantized further after DCT processing by a factor of
       * 2 in reference to the nominal quantization factor.
       */
      /* need quantization scale by factor of 2 after DCT */
      cinfo->comp_info[1].component_needed = TRUE;
      cinfo->comp_info[2].component_needed = TRUE;
      /* compute normal YCC first */
      cconvert->pub.start_pass = rgb_ycc_start;
      cconvert->pub.color_convert = rgb_ycc_convert;
      break;
    case JCS_YCbCr:
      /* need quantization scale by factor of 2 after DCT */
      cinfo->comp_info[1].component_needed = TRUE;
      cinfo->comp_info[2].component_needed = TRUE;
      cconvert->pub.color_convert = null_convert;
      break;
    case JCS_BG_YCC:
      /* Pass through for BG_YCC input */
      cconvert->pub.color_convert = null_convert;
      break;
    default:
        JSC_ASSERT_1(0, cinfo->in_color_space);
    }
    break;

  case JCS_CMYK:
    JSC_ASSERT_1(cinfo->num_components == 4, cinfo->num_components);
    JSC_ASSERT_2(cinfo->in_color_space == JCS_CMYK,
                 cinfo->in_color_space, JCS_CMYK);
    cconvert->pub.color_convert = null_convert;
    break;

  case JCS_YCCK:
    JSC_ASSERT_1(cinfo->num_components == 4, cinfo->num_components);
    switch (cinfo->in_color_space) {
    case JCS_CMYK:
      cconvert->pub.start_pass = rgb_ycc_start;
      cconvert->pub.color_convert = cmyk_ycck_convert;
      break;
    case JCS_YCCK:
      cconvert->pub.color_convert = null_convert;
      break;
    default:
        JSC_ASSERT_1(0, cinfo->in_color_space);
    }
    break;

  default:			/* allow null conversion of JCS_UNKNOWN */
    JSC_ASSERT_2(cinfo->jpeg_color_space == cinfo->in_color_space,
              cinfo->jpeg_color_space, cinfo->in_color_space);
    JSC_ASSERT_2(cinfo->num_components == cinfo->input_components,
              cinfo->num_components, cinfo->input_components);
    cconvert->pub.color_convert = null_convert;
  }
}
