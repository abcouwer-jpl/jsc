/*
 * jddctmgr.c
 *
 * Copyright (C) 1994-1996, Thomas G. Lane.
 * Modified 2002-2013 by Guido Vollbeding.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
 *
 * This file contains the inverse-DCT management logic.
 * This code selects a particular IDCT implementation to be used,
 * and it performs related housekeeping chores.  No code in this file
 * is executed per IDCT step, only during output pass setup.
 *
 * Note that the IDCT routines are responsible for performing coefficient
 * dequantization as well as the IDCT proper.  This module sets up the
 * dequantization multiplier table needed by the IDCT routine.
 */

#include "jsc/jpegint.h"
#include "jsc/jdct.h"		/* Private declarations for DCT subsystem */


/*
 * The decompressor input side (jdinput.c) saves away the appropriate
 * quantization table for each component at the start of the first scan
 * involving that component.  (This is necessary in order to correctly
 * decode files that reuse Q-table slots.)
 * When we are ready to make an output pass, the saved Q-table is converted
 * to a multiplier table that will actually be used by the IDCT routine.
 * The multiplier table contents are IDCT-method-dependent.  To support
 * application changes in IDCT method between scans, we can remake the
 * multiplier tables if necessary.
 * In buffered-image mode, the first output pass may occur before any data
 * has been seen for some components, and thus before their Q-tables have
 * been saved away.  To handle this case, multiplier tables are preset
 * to zeroes; the result of the IDCT will be a neutral gray level.
 */


/* Private subobject for this module */

typedef struct {
  struct jpeg_inverse_dct pub;	/* public fields */

  /* This array contains the IDCT method code that each multiplier table
   * is currently set up for, or -1 if it's not yet set up.
   * The actual multiplier tables are pointed to by dct_table in the
   * per-component comp_info structures.
   */
//  JINT cur_method[MAX_COMPONENTS];
} my_idct_controller;


/* Allocated multiplier tables: big enough for any supported variant */
// Abcouwer JSC 2021 - one variant
typedef struct {
  FLOAT_MULT_TYPE float_array[DCTSIZE2];
} multiplier_table;


/*
 * Prepare for an output pass.
 * Here we select the proper IDCT routine for each component and build
 * a matching multiplier table.
 */

JSC_METHODDEF(void)
start_pass (j_decompress_ptr cinfo)
{
    JSC_ASSERT(cinfo != NULL);

    my_idct_controller * idct = (my_idct_controller *) cinfo->idct;
    JINT ci;
    JINT i;
    jpeg_component_info *compptr;
//    JINT method = 0;
    inverse_DCT_method_ptr method_ptr = NULL;
    JQUANT_TBL *qtbl;

    for (ci = 0; ci < cinfo->num_components; ci++) {
        compptr = cinfo->comp_info + ci;
        /* Select the proper IDCT routine for this component's scaling */
        // Abcouwer JSC 2021 - no DCT scaling support, no integer DCT
        JSC_ASSERT_2(compptr->DCT_h_scaled_size == DCTSIZE,
                compptr->DCT_h_scaled_size, DCTSIZE);
        JSC_ASSERT_2(compptr->DCT_v_scaled_size == DCTSIZE,
                compptr->DCT_v_scaled_size, DCTSIZE);
        method_ptr = jpeg_idct_float;
        idct->pub.inverse_DCT[ci] = method_ptr;
        /* Create multiplier table from quant table.
         * However, we can skip this if the component is uninteresting
         * or if we already built the table.  Also, if no quant table
         * has yet been saved for the component, we leave the
         * multiplier table all-zero; we'll be reading zeroes from the
         * coefficient controller's buffer anyway.
         */
        if (!compptr->component_needed)
            continue;
        qtbl = compptr->quant_table;
        if (qtbl == NULL) /* happens if no data yet for component */
            continue;
        /* For float AA&N IDCT method, multipliers are equal to quantization
         * coefficients scaled by scalefactor[row]*scalefactor[col], where
         *   scalefactor[0] = 1
         *   scalefactor[k] = cos(k*PI/16) * sqrt(2)    for k=1..7
         * We apply a further scale factor of 1/8.
         */
        FLOAT_MULT_TYPE *fmtbl = (FLOAT_MULT_TYPE*) compptr->dct_table;
        JINT row;
        JINT col;
        static const F64 aanscalefactor[DCTSIZE] = { 1.0, 1.387039845,
                1.306562965, 1.175875602, 1.0, 0.785694958, 0.541196100,
                0.275899379 };

        i = 0;
        for (row = 0; row < DCTSIZE; row++) {
            for (col = 0; col < DCTSIZE; col++) {
                fmtbl[i] =
                        (FLOAT_MULT_TYPE) ((F64) qtbl->quantval[i]
                                * aanscalefactor[row] * aanscalefactor[col]
                                * 0.125);
                i++;
            }
        }
    }
}


/*
 * Initialize IDCT manager.
 */

JSC_GLOBAL(void)
jinit_inverse_dct (j_decompress_ptr cinfo)
{
  my_idct_controller * idct;
  JINT ci;
  jpeg_component_info *compptr;

  idct = (my_idct_controller *)
    (*cinfo->mem->get_mem) ((j_common_ptr) cinfo, JPOOL_IMAGE,
				SIZEOF(my_idct_controller));
  cinfo->idct = &idct->pub;
  idct->pub.start_pass = start_pass;

  for (ci = 0; ci < cinfo->num_components; ci++) {
      compptr = cinfo->comp_info + ci;
    /* Allocate and pre-zero a multiplier table for each component */
    compptr->dct_table =
      (*cinfo->mem->get_mem) ((j_common_ptr) cinfo, JPOOL_IMAGE,
				  SIZEOF(multiplier_table));
    JSC_MEMZERO(compptr->dct_table, SIZEOF(multiplier_table));
  }
}
