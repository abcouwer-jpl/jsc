/*
 * jdcolor.c
 *
 * Copyright (C) 1991-1997, Thomas G. Lane.
 * Modified 2011-2019 by Guido Vollbeding.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
 *
 * This file contains output colorspace conversion routines.
 */

#include "jsc/jpegint.h"

// this code requires 2 or more range extension bits
JSC_COMPILE_ASSERT(RANGE_BITS >= 2, lt2_range_bits);

/* Private subobject */

typedef struct {
  struct jpeg_color_deconverter pub; /* public fields */

  /* Private state for YCbCr->RGB and BG_YCC->RGB conversion */
  JINT * Cr_r_tab;                /* => table for Cr to R conversion */
  JINT * Cb_b_tab;                /* => table for Cb to B conversion */
  INT32 * Cr_g_tab;                /* => table for Cr to G conversion */
  INT32 * Cb_g_tab;                /* => table for Cb to G conversion */

  /* Private state for RGB->Y conversion */
  INT32 * rgb_y_tab;                /* => table for RGB to Y conversion */
} my_color_deconverter;

/***************  YCbCr -> RGB conversion: most common case **************/
/*************** BG_YCC -> RGB conversion: less common case **************/
/***************    RGB -> Y   conversion: less common case **************/

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
 *
 *        R = Y + K * (1 - Kr) * Cr
 *        G = Y - K * (Kb * (1 - Kb) * Cb + Kr * (1 - Kr) * Cr) / (1 - Kr - Kb)
 *        B = Y + K * (1 - Kb) * Cb
 *
 *        Y = Kr * R + (1 - Kr - Kb) * G + Kb * B
 *
 * With Kr = 0.299 and Kb = 0.114 (derived according to SMPTE RP 177-1993
 * from the 1953 FCC NTSC primaries and CIE Illuminant C), K = 2 for sYCC,
 * the conversion equations to be implemented are therefore
 *
 *        R = Y + 1.402 * Cr
 *        G = Y - 0.344136286 * Cb - 0.714136286 * Cr
 *        B = Y + 1.772 * Cb
 *
 *        Y = 0.299 * R + 0.587 * G + 0.114 * B
 *
 * where Cb and Cr represent the incoming values less CENTERJSAMPLE.
 * For bg-sYCC, with K = 4, the equations are
 *
 *        R = Y + 2.804 * Cr
 *        G = Y - 0.688272572 * Cb - 1.428272572 * Cr
 *        B = Y + 3.544 * Cb
 *
 * To avoid floating-point arithmetic, we represent the fractional constants
 * as integers scaled up by 2^16 (about 4 digits precision); we have to divide
 * the products by 2^16, with appropriate rounding, to get the correct answer.
 * Notice that Y, being an integral input, does not contribute any fraction
 * so it need not participate in the rounding.
 *
 * For even more speed, we avoid doing any multiplications in the inner loop
 * by precalculating the constants times Cb and Cr for all possible values.
 * For 8-bit JSAMPLEs this is very reasonable (only 256 entries per table);
 * for 9-bit to 12-bit samples it is still acceptable.  It's not very
 * reasonable for 16-bit samples, but if you want lossless storage you
 * shouldn't be changing colorspace anyway.
 * The Cr=>R and Cb=>B values can be rounded to integers in advance; the
 * values for the G calculation are left scaled up, since we must add them
 * together before rounding.
 */

#define SCALEBITS        16        /* speediest right-shift on some machines */
#define ONE_HALF        ((INT32) 1 << (SCALEBITS-1))
#define FIX(x)                ((INT32) ((x) * (1L<<SCALEBITS) + 0.5))

/* We allocate one big table for RGB->Y conversion and divide it up into
 * three parts, instead of doing three alloc_small requests.  This lets us
 * use a single table base address, which can be held in a register in the
 * inner loops on many machines (more than can hold all three addresses,
 * anyway).
 */

#define R_Y_OFF                0                        /* offset to R => Y section */
#define G_Y_OFF                (1*(MAXJSAMPLE+1))        /* offset to G => Y section */
#define B_Y_OFF                (2*(MAXJSAMPLE+1))        /* etc. */
#define TABLE_SIZE        (3*(MAXJSAMPLE+1))


/*
 * Initialize tables for YCbCr->RGB and BG_YCC->RGB colorspace conversion.
 */

JSC_LOCAL(void)
build_ycc_rgb_table (j_decompress_ptr cinfo)
/* Normal case, sYCC */
{
  JSC_ASSERT(cinfo != NULL);

  my_color_deconverter * cconvert = (my_color_deconverter *) cinfo->cconvert;
  JINT i;
  INT32 x;
  SHIFT_TEMPS

  cconvert->Cr_r_tab = (JINT *) (*cinfo->mem->get_mem)
    ((j_common_ptr) cinfo, JPOOL_IMAGE, (MAXJSAMPLE+1) * SIZEOF(JINT));
  cconvert->Cb_b_tab = (JINT *) (*cinfo->mem->get_mem)
    ((j_common_ptr) cinfo, JPOOL_IMAGE, (MAXJSAMPLE+1) * SIZEOF(JINT));
  cconvert->Cr_g_tab = (INT32 *) (*cinfo->mem->get_mem)
    ((j_common_ptr) cinfo, JPOOL_IMAGE, (MAXJSAMPLE+1) * SIZEOF(INT32));
  cconvert->Cb_g_tab = (INT32 *) (*cinfo->mem->get_mem)
    ((j_common_ptr) cinfo, JPOOL_IMAGE, (MAXJSAMPLE+1) * SIZEOF(INT32));

  for (i = 0, x = -CENTERJSAMPLE; i <= MAXJSAMPLE; i++, x++) {
    /* i is the actual input pixel value, in the range 0..MAXJSAMPLE */
    /* The Cb or Cr value we are thinking of is x = i - CENTERJSAMPLE */
    /* Cr=>R value is nearest int to 1.402 * x */
    cconvert->Cr_r_tab[i] = (JINT) DESCALE(FIX(1.402) * x, SCALEBITS);
    /* Cb=>B value is nearest int to 1.772 * x */
    cconvert->Cb_b_tab[i] = (JINT) DESCALE(FIX(1.772) * x, SCALEBITS);
    /* Cr=>G value is scaled-up -0.714136286 * x */
    cconvert->Cr_g_tab[i] = (- FIX(0.714136286)) * x;
    /* Cb=>G value is scaled-up -0.344136286 * x */
    /* We also add in ONE_HALF so that need not do it in inner loop */
    cconvert->Cb_g_tab[i] = (- FIX(0.344136286)) * x + ONE_HALF;
  }
}


JSC_LOCAL(void)
build_bg_ycc_rgb_table (j_decompress_ptr cinfo)
/* Wide gamut case, bg-sYCC */
{
  JSC_ASSERT(cinfo != NULL);

  my_color_deconverter * cconvert = (my_color_deconverter *) cinfo->cconvert;
  JINT i;
  INT32 x;
  SHIFT_TEMPS

  cconvert->Cr_r_tab = (JINT *) (*cinfo->mem->get_mem)
    ((j_common_ptr) cinfo, JPOOL_IMAGE, (MAXJSAMPLE+1) * SIZEOF(JINT));
  cconvert->Cb_b_tab = (JINT *) (*cinfo->mem->get_mem)
    ((j_common_ptr) cinfo, JPOOL_IMAGE, (MAXJSAMPLE+1) * SIZEOF(JINT));
  cconvert->Cr_g_tab = (INT32 *) (*cinfo->mem->get_mem)
    ((j_common_ptr) cinfo, JPOOL_IMAGE, (MAXJSAMPLE+1) * SIZEOF(INT32));
  cconvert->Cb_g_tab = (INT32 *) (*cinfo->mem->get_mem)
    ((j_common_ptr) cinfo, JPOOL_IMAGE, (MAXJSAMPLE+1) * SIZEOF(INT32));

  for (i = 0, x = -CENTERJSAMPLE; i <= MAXJSAMPLE; i++, x++) {
    /* i is the actual input pixel value, in the range 0..MAXJSAMPLE */
    /* The Cb or Cr value we are thinking of is x = i - CENTERJSAMPLE */
    /* Cr=>R value is nearest int to 2.804 * x */
    cconvert->Cr_r_tab[i] = (JINT) DESCALE(FIX(2.804) * x, SCALEBITS);
    /* Cb=>B value is nearest int to 3.544 * x */
    cconvert->Cb_b_tab[i] = (JINT) DESCALE(FIX(3.544) * x, SCALEBITS);
    /* Cr=>G value is scaled-up -1.428272572 * x */
    cconvert->Cr_g_tab[i] = (- FIX(1.428272572)) * x;
    /* Cb=>G value is scaled-up -0.688272572 * x */
    /* We also add in ONE_HALF so that need not do it in inner loop */
    cconvert->Cb_g_tab[i] = (- FIX(0.688272572)) * x + ONE_HALF;
  }
}


/*
 * Convert some rows of samples to the output colorspace.
 *
 * Note that we change from noninterleaved, one-plane-per-component format
 * to interleaved-pixel format.  The output buffer is therefore three times
 * as wide as the input buffer.
 *
 * A starting row offset is provided only for the input buffer.  The caller
 * can easily adjust the passed output_buf value to accommodate any row
 * offset required on that side.
 */

JSC_METHODDEF(void)
ycc_rgb_convert (j_decompress_ptr cinfo,
                 JSAMPIMAGE input_buf, JDIMENSION input_row,
                 JSAMPARRAY output_buf, JINT num_rows)
{
  JSC_ASSERT(cinfo != NULL);

  my_color_deconverter * cconvert = (my_color_deconverter *) cinfo->cconvert;
  register JINT y;
  register JINT cb;
  register JINT cr;
  register JSAMPROW outptr;
  register JSAMPROW inptr0;
  register JSAMPROW inptr1;
  register JSAMPROW inptr2;
  register JDIMENSION col;
  JDIMENSION num_cols = cinfo->output_width;
  /* copy these pointers into registers if possible */
  register JSAMPLE * range_limit = cinfo->sample_range_limit;
  register JINT * Crrtab = cconvert->Cr_r_tab;
  register JINT * Cbbtab = cconvert->Cb_b_tab;
  register INT32 * Crgtab = cconvert->Cr_g_tab;
  register INT32 * Cbgtab = cconvert->Cb_g_tab;
  SHIFT_TEMPS

  while (num_rows > 0) {
    num_rows--;
    inptr0 = input_buf[0][input_row];
    inptr1 = input_buf[1][input_row];
    inptr2 = input_buf[2][input_row];
    input_row++;
    outptr = *output_buf++;
    for (col = 0; col < num_cols; col++) {
      y  = GETJSAMPLE(inptr0[col]);
      cb = GETJSAMPLE(inptr1[col]);
      cr = GETJSAMPLE(inptr2[col]);
      /* Range-limiting is essential due to noise introduced by DCT losses,
       * for extended gamut (sYCC) and wide gamut (bg-sYCC) encodings.
       */
      outptr[RGB_RED]   = range_limit[y + Crrtab[cr]];
      outptr[RGB_GREEN] = range_limit[y +
                              ((JINT) RIGHT_SHIFT(Cbgtab[cb] + Crgtab[cr],
                                                 SCALEBITS))];
      outptr[RGB_BLUE]  = range_limit[y + Cbbtab[cb]];
      outptr += RGB_PIXELSIZE;
    }
  }
}


/**************** Cases other than YCC -> RGB ****************/


/*
 * Initialize for RGB->grayscale colorspace conversion.
 */

JSC_LOCAL(void)
build_rgb_y_table (j_decompress_ptr cinfo)
{
  JSC_ASSERT(cinfo != NULL);

  my_color_deconverter * cconvert = (my_color_deconverter *) cinfo->cconvert;
  INT32 * rgb_y_tab;
  INT32 i;

  /* Allocate and fill in the conversion tables. */
  cconvert->rgb_y_tab = rgb_y_tab = (INT32 *) (*cinfo->mem->get_mem)
    ((j_common_ptr) cinfo, JPOOL_IMAGE, TABLE_SIZE * SIZEOF(INT32));

  for (i = 0; i <= MAXJSAMPLE; i++) {
    rgb_y_tab[i+R_Y_OFF] = FIX(0.299) * i;
    rgb_y_tab[i+G_Y_OFF] = FIX(0.587) * i;
    rgb_y_tab[i+B_Y_OFF] = FIX(0.114) * i + ONE_HALF;
  }
}


/*
 * Convert RGB to grayscale.
 */

JSC_METHODDEF(void)
rgb_gray_convert (j_decompress_ptr cinfo,
                  JSAMPIMAGE input_buf, JDIMENSION input_row,
                  JSAMPARRAY output_buf, JINT num_rows)
{
  my_color_deconverter * cconvert = (my_color_deconverter *) cinfo->cconvert;
  register JINT r;
  register JINT g;
  register JINT b;
  register INT32 * ctab = cconvert->rgb_y_tab;
  register JSAMPROW outptr;
  register JSAMPROW inptr0, inptr1, inptr2;
  register JDIMENSION col;
  JDIMENSION num_cols = cinfo->output_width;

  while (num_rows > 0) {
    num_rows--;
    inptr0 = input_buf[0][input_row];
    inptr1 = input_buf[1][input_row];
    inptr2 = input_buf[2][input_row];
    input_row++;
    outptr = *output_buf++;
    for (col = 0; col < num_cols; col++) {
      r = GETJSAMPLE(inptr0[col]);
      g = GETJSAMPLE(inptr1[col]);
      b = GETJSAMPLE(inptr2[col]);
      /* Y */
      outptr[col] = (JSAMPLE)
                ((ctab[r+R_Y_OFF] + ctab[g+G_Y_OFF] + ctab[b+B_Y_OFF])
                 >> SCALEBITS);
    }
  }
}

// Abcouwer JSC 2021 - remove support for subtract green / color transform

/*
 * Convert some rows of samples to the output colorspace.
 * No colorspace change, but conversion from separate-planes
 * to interleaved representation.
 */

JSC_METHODDEF(void)
rgb_convert (j_decompress_ptr cinfo,
             JSAMPIMAGE input_buf, JDIMENSION input_row,
             JSAMPARRAY output_buf, JINT num_rows)
{
  register JSAMPROW outptr;
  register JSAMPROW inptr0, inptr1, inptr2;
  register JDIMENSION col;
  JDIMENSION num_cols = cinfo->output_width;

  while (num_rows > 0) {
    num_rows--;
    inptr0 = input_buf[0][input_row];
    inptr1 = input_buf[1][input_row];
    inptr2 = input_buf[2][input_row];
    input_row++;
    outptr = *output_buf++;
    for (col = 0; col < num_cols; col++) {
      /* We can dispense with GETJSAMPLE() here */
      outptr[RGB_RED]   = inptr0[col];
      outptr[RGB_GREEN] = inptr1[col];
      outptr[RGB_BLUE]  = inptr2[col];
      outptr += RGB_PIXELSIZE;
    }
  }
}


/*
 * Color conversion for no colorspace change: just copy the data,
 * converting from separate-planes to interleaved representation.
 * We assume out_color_components == num_components.
 */

JSC_METHODDEF(void)
null_convert (j_decompress_ptr cinfo,
              JSAMPIMAGE input_buf, JDIMENSION input_row,
              JSAMPARRAY output_buf, JINT num_rows)
{
  register JSAMPROW outptr;
  register JSAMPROW inptr;
  register JDIMENSION count;
  register JINT num_comps = cinfo->num_components;
  JDIMENSION num_cols = cinfo->output_width;
  JINT ci;

  while (num_rows > 0) {
    num_rows--;
    /* It seems fastest to make a separate pass for each component. */
    for (ci = 0; ci < num_comps; ci++) {
      inptr = input_buf[ci][input_row];
      outptr = output_buf[0] + ci;
      for (count = num_cols; count > 0; count--) {
        *outptr = *inptr++;        /* don't need GETJSAMPLE() here */
        outptr += num_comps;
      }
    }
    input_row++;
    output_buf++;
  }
}


/*
 * Color conversion for grayscale: just copy the data.
 * This also works for YCC -> grayscale conversion, in which
 * we just copy the Y (luminance) component and ignore chrominance.
 */

JSC_METHODDEF(void)
grayscale_convert (j_decompress_ptr cinfo,
                   JSAMPIMAGE input_buf, JDIMENSION input_row,
                   JSAMPARRAY output_buf, JINT num_rows)
{
  jcopy_sample_rows(input_buf[0], (JINT) input_row, output_buf, 0,
                    num_rows, cinfo->output_width);
}


/*
 * Convert grayscale to RGB: just duplicate the graylevel three times.
 * This is provided to support applications that don't want to cope
 * with grayscale as a separate case.
 */

JSC_METHODDEF(void)
gray_rgb_convert (j_decompress_ptr cinfo,
                  JSAMPIMAGE input_buf, JDIMENSION input_row,
                  JSAMPARRAY output_buf, JINT num_rows)
{
  register JSAMPROW outptr;
  register JSAMPROW inptr;
  register JDIMENSION col;
  JDIMENSION num_cols = cinfo->output_width;

  while (num_rows > 0) {
    num_rows--;
    inptr = input_buf[0][input_row++];
    outptr = *output_buf++;
    for (col = 0; col < num_cols; col++) {
      /* We can dispense with GETJSAMPLE() here */
      outptr[RGB_RED] = outptr[RGB_GREEN] = outptr[RGB_BLUE] = inptr[col];
      outptr += RGB_PIXELSIZE;
    }
  }
}


/*
 * Convert some rows of samples to the output colorspace.
 * This version handles Adobe-style YCCK->CMYK conversion,
 * where we convert YCbCr to R=1-C, G=1-M, and B=1-Y using the
 * same conversion as above, while passing K (black) unchanged.
 * We assume build_ycc_rgb_table has been called.
 */

JSC_METHODDEF(void)
ycck_cmyk_convert (j_decompress_ptr cinfo,
                   JSAMPIMAGE input_buf, JDIMENSION input_row,
                   JSAMPARRAY output_buf, JINT num_rows)
{
  my_color_deconverter * cconvert = (my_color_deconverter *) cinfo->cconvert;
  register JINT y;
  register JINT cb;
  register JINT cr;
  register JSAMPROW outptr;
  register JSAMPROW inptr0;
  register JSAMPROW inptr1;
  register JSAMPROW inptr2;
  register JSAMPROW inptr3;
  register JDIMENSION col;
  JDIMENSION num_cols = cinfo->output_width;
  /* copy these pointers into registers if possible */
  register JSAMPLE * range_limit = cinfo->sample_range_limit;
  register JINT * Crrtab = cconvert->Cr_r_tab;
  register JINT * Cbbtab = cconvert->Cb_b_tab;
  register INT32 * Crgtab = cconvert->Cr_g_tab;
  register INT32 * Cbgtab = cconvert->Cb_g_tab;
  SHIFT_TEMPS

  while (num_rows > 0) {
    num_rows--;
    inptr0 = input_buf[0][input_row];
    inptr1 = input_buf[1][input_row];
    inptr2 = input_buf[2][input_row];
    inptr3 = input_buf[3][input_row];
    input_row++;
    outptr = *output_buf++;
    for (col = 0; col < num_cols; col++) {
      y  = GETJSAMPLE(inptr0[col]);
      cb = GETJSAMPLE(inptr1[col]);
      cr = GETJSAMPLE(inptr2[col]);
      /* Range-limiting is essential due to noise introduced by DCT losses,
       * and for extended gamut encodings (sYCC).
       */
      outptr[0] = range_limit[MAXJSAMPLE - (y + Crrtab[cr])];        /* red */
      outptr[1] = range_limit[MAXJSAMPLE - (y +                        /* green */
                              ((JINT) RIGHT_SHIFT(Cbgtab[cb] + Crgtab[cr],
                                                 SCALEBITS)))];
      outptr[2] = range_limit[MAXJSAMPLE - (y + Cbbtab[cb])];        /* blue */
      /* K passes through unchanged */
      outptr[3] = inptr3[col];        /* don't need GETJSAMPLE here */
      outptr += 4;
    }
  }
}


/*
 * Empty method for start_pass.
 */

JSC_METHODDEF(void)
start_pass_dcolor (j_decompress_ptr cinfo)
{
  /* no work needed */
}


/*
 * Module initialization routine for output colorspace conversion.
 */

JSC_GLOBAL(void)
jinit_color_deconverter (j_decompress_ptr cinfo)
{
  my_color_deconverter * cconvert;
  JINT ci;

  cconvert = (my_color_deconverter *) (*cinfo->mem->get_mem)
    ((j_common_ptr) cinfo, JPOOL_IMAGE, SIZEOF(my_color_deconverter));
  cinfo->cconvert = &cconvert->pub;
  cconvert->pub.start_pass = start_pass_dcolor;

  /* Make sure num_components agrees with jpeg_color_space */
  switch (cinfo->jpeg_color_space) {
  case JCS_GRAYSCALE:
    JSC_ASSERT_1(cinfo->num_components == 1, cinfo->num_components);
    break;

  case JCS_RGB:
  case JCS_YCbCr:
  case JCS_BG_RGB:
  case JCS_BG_YCC:
    JSC_ASSERT_1(cinfo->num_components == 3, cinfo->num_components);
    break;

  case JCS_CMYK:
  case JCS_YCCK:
    JSC_ASSERT_1(cinfo->num_components == 4, cinfo->num_components);
    break;

  default:                        /* JCS_UNKNOWN can be anything */
    JSC_ASSERT_1(cinfo->num_components > 0, cinfo->num_components);
  }

  // Abcouwer JSC 2021 - remove support for subtract green / color transform

  /* Set out_color_components and conversion method based on requested space.
   * Also clear the component_needed flags for any unused components,
   * so that earlier pipeline stages can avoid useless computation.
   */

  switch (cinfo->out_color_space) {
  case JCS_GRAYSCALE:
    cinfo->out_color_components = 1;
    switch (cinfo->jpeg_color_space) {
    case JCS_GRAYSCALE:
    case JCS_YCbCr:
    case JCS_BG_YCC:
      cconvert->pub.color_convert = grayscale_convert;
      /* For color->grayscale conversion, only the Y (0) component is needed */
      for (ci = 1; ci < cinfo->num_components; ci++) {
        cinfo->comp_info[ci].component_needed = FALSE;
      }
      break;
    case JCS_RGB:
      // Abcouwer JSC 2021 - remove support for subtract green / color transform
      cconvert->pub.color_convert = rgb_gray_convert;
      build_rgb_y_table(cinfo);
      break;
    default:
      // no conversion
      JSC_ASSERT_1(0,cinfo->jpeg_color_space);
    }
    break;

  case JCS_RGB:
    cinfo->out_color_components = RGB_PIXELSIZE;
    switch (cinfo->jpeg_color_space) {
    case JCS_GRAYSCALE:
      cconvert->pub.color_convert = gray_rgb_convert;
      break;
    case JCS_YCbCr:
      cconvert->pub.color_convert = ycc_rgb_convert;
      build_ycc_rgb_table(cinfo);
      break;
    case JCS_BG_YCC:
      cconvert->pub.color_convert = ycc_rgb_convert;
      build_bg_ycc_rgb_table(cinfo);
      break;
    case JCS_RGB:
      // Abcouwer JSC 2021 - remove support for subtract green / color transform
      cconvert->pub.color_convert = rgb_convert;
      break;
    default:
      // no conversion
      JSC_ASSERT_1(0,cinfo->jpeg_color_space);
    }
    break;

  case JCS_BG_RGB:
    cinfo->out_color_components = RGB_PIXELSIZE;
    // only supported conversion
    JSC_ASSERT_2(cinfo->jpeg_color_space == JCS_BG_RGB,
            cinfo->jpeg_color_space, JCS_BG_RGB);
    // Abcouwer JSC 2021 - remove support for subtract green / color transform
    cconvert->pub.color_convert = rgb_convert;
    break;

  case JCS_CMYK:
    cinfo->out_color_components = 4;
    switch (cinfo->jpeg_color_space) {
    case JCS_YCCK:
      cconvert->pub.color_convert = ycck_cmyk_convert;
      build_ycc_rgb_table(cinfo);
      break;
    case JCS_CMYK:
      cconvert->pub.color_convert = null_convert;
      break;
    default:
      // no conversion
      JSC_ASSERT_1(0,cinfo->jpeg_color_space);
    }
    break;

  default:                /* permit null conversion to same output space */
    JSC_ASSERT_2(cinfo->out_color_space == cinfo->jpeg_color_space,
            cinfo->out_color_space, cinfo->jpeg_color_space);
    cinfo->out_color_components = cinfo->num_components;
    cconvert->pub.color_convert = null_convert;
  }

  // Abcouwer ZSC 2021 - don't support colormaps
  cinfo->output_components = cinfo->out_color_components;
}
