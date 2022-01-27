/*
 * jcdctmgr.c
 *
 * Copyright (C) 1994-1996, Thomas G. Lane.
 * Modified 2003-2013 by Guido Vollbeding.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
 *
 * This file contains the forward-DCT management logic.
 * This code selects a particular DCT implementation to be used,
 * and it performs related housekeeping chores including coefficient
 * quantization.
 */

#include "jsc/jpegint.h"
#include "jsc/jdct.h"                /* Private declarations for DCT subsystem */


/* Private subobject for this module */

typedef struct {
  struct jpeg_forward_dct pub;        /* public fields */
  float_DCT_method_ptr do_float_dct[MAX_COMPONENTS];
} my_fdct_controller;


/* The allocated post-DCT divisor tables -- big enough for any
 * supported variant and not identical to the quant table entries,
 * because of scaling (especially for an unnormalized DCT) --
 * are pointed to by dct_table in the per-component comp_info
 * structures.  Each table is given in normal array order.
 */
// Abcouwer JSC 2021 - only need float array
typedef struct {
  FAST_FLOAT float_array[DCTSIZE2];
} divisor_table;

// Abcouwer JSC 2021 - remove integer DCT - float only

JSC_METHODDEF(void)
forward_DCT_float (j_compress_ptr cinfo, jpeg_component_info * compptr,
                   JSAMPARRAY sample_data, JBLOCKROW coef_blocks,
                   JDIMENSION start_row, JDIMENSION start_col,
                   JDIMENSION num_blocks)
/* This version is used for floating-point DCT implementations. */
{
    /* This routine is heavily used, so it's worth coding it tightly. */
    my_fdct_controller * fdct = (my_fdct_controller *) cinfo->fdct;
    float_DCT_method_ptr do_dct = fdct->do_float_dct[compptr->component_index];
    FAST_FLOAT *divisors = (FAST_FLOAT*) compptr->dct_table;
    FAST_FLOAT workspace[DCTSIZE2]; /* work area for FDCT subroutine */
    JDIMENSION bi;

    sample_data += start_row; /* fold in the vertical offset once */

    for (bi = 0; bi < num_blocks;
            bi++, start_col += compptr->DCT_h_scaled_size) {
        /* Perform the DCT */
        (*do_dct)(workspace, sample_data, start_col);

        /* Quantize/descale the coefficients, and store into coef_blocks[] */
        {
            register FAST_FLOAT temp;
            register JINT i;
            register JCOEFPTR output_ptr = coef_blocks[bi];

            for (i = 0; i < DCTSIZE2; i++) {
                /* Apply the quantization and scaling factor */
                temp = workspace[i] * divisors[i];
                /* Round to nearest integer.
                 * Since C does not specify the direction of rounding for negative
                 * quotients, we have to force the dividend positive for portability.
                 * The maximum coefficient size is +-16K (for 12-bit data), so this
                 * code should work for either 16-bit or 32-bit ints.
                 */
                output_ptr[i] = (JCOEF) ((JINT) (temp + (FAST_FLOAT) 16384.5)
                        - 16384);
            }
        }
    }
}



/*
 * Initialize for a processing pass.
 * Verify that all referenced Q-tables are present, and set up
 * the divisor table for each one.
 * In the current implementation, DCT of all components is done during
 * the first pass, even if only some components will be output in the
 * first scan.  Hence all components should be examined here.
 */

JSC_METHODDEF(void)
start_pass_fdctmgr (j_compress_ptr cinfo)
{
    JSC_ASSERT(cinfo != NULL);
    my_fdct_controller * fdct = (my_fdct_controller *) cinfo->fdct;
    JINT ci;
    JINT qtblno;
    JINT i;
    jpeg_component_info *compptr;
    JQUANT_TBL *qtbl;

    for (ci = 0; ci < cinfo->num_components; ci++) {
        compptr = cinfo->comp_info + ci;
        /* Select the proper DCT routine for this component's scaling */
        // Abcouwer JSC 2021 - no DCT scaling support, no integer DCT
        JSC_ASSERT_2(compptr->DCT_h_scaled_size == DCTSIZE,
                compptr->DCT_h_scaled_size, DCTSIZE);
        JSC_ASSERT_2(compptr->DCT_v_scaled_size == DCTSIZE,
                compptr->DCT_v_scaled_size, DCTSIZE);
        fdct->do_float_dct[ci] = jpeg_fdct_float;
        qtblno = compptr->quant_tbl_no;
        /* Make sure specified quantization table is present */
        JSC_ASSERT_1(qtblno >= 0, qtblno);
        JSC_ASSERT_2(qtblno < NUM_QUANT_TBLS, qtblno, NUM_QUANT_TBLS);
        JSC_ASSERT(cinfo->quant_tbl_ptrs[qtblno] != NULL);
        qtbl = cinfo->quant_tbl_ptrs[qtblno];
        /* Create divisor table from quant table */

        /* For float AA&N IDCT method, divisors are equal to quantization
         * coefficients scaled by scalefactor[row]*scalefactor[col], where
         *   scalefactor[0] = 1
         *   scalefactor[k] = cos(k*PI/16) * sqrt(2)    for k=1..7
         * We apply a further scale factor of 8.
         * What's actually stored is 1/divisor so that the inner loop can
         * use a multiplication rather than a division.
         */
        FAST_FLOAT *fdtbl = (FAST_FLOAT*) compptr->dct_table;
        JINT row;
        JINT col;
        static const F64 aanscalefactor[DCTSIZE] = {
          1.0, 1.387039845, 1.306562965, 1.175875602,
          1.0, 0.785694958, 0.541196100, 0.275899379
        };

        i = 0;
        for (row = 0; row < DCTSIZE; row++) {
            for (col = 0; col < DCTSIZE; col++) {
                fdtbl[i] =
                        (FAST_FLOAT) (1.0
                                / ((F64) qtbl->quantval[i]
                                        * aanscalefactor[row]
                                        * aanscalefactor[col]
                                        * (compptr->component_needed ?
                                                16.0 : 8.0)));
                i++;
            }
        }

        fdct->pub.forward_DCT[ci] = forward_DCT_float;

    }
}


/*
 * Initialize FDCT manager.
 */

JSC_GLOBAL(void)
jinit_forward_dct (j_compress_ptr cinfo)
{
    JSC_ASSERT(cinfo != NULL);

    my_fdct_controller * fdct;
    JINT ci;
    jpeg_component_info *compptr;

    fdct = (my_fdct_controller *) (*cinfo->mem->get_mem)((j_common_ptr) cinfo,
            JPOOL_IMAGE, SIZEOF(my_fdct_controller));
    cinfo->fdct = &fdct->pub;
    fdct->pub.start_pass = start_pass_fdctmgr;

    for (ci = 0; ci < cinfo->num_components; ci++) {
        compptr = cinfo->comp_info + ci;
        /* Allocate a divisor table for each component */
        compptr->dct_table = (*cinfo->mem->get_mem)((j_common_ptr) cinfo,
                JPOOL_IMAGE, SIZEOF(divisor_table));
    }
}
