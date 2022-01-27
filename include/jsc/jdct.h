/*
 * jdct.h
 *
 * Copyright (C) 1994-1996, Thomas G. Lane.
 * Modified 2002-2019 by Guido Vollbeding.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
 *
 * This include file contains common declarations for the forward and
 * inverse DCT modules.  These declarations are private to the DCT managers
 * (jcdctmgr.c, jddctmgr.c) and the individual DCT algorithms.
 * The individual DCT algorithms are kept in separate files to ease 
 * machine-dependent tuning (e.g., assembly coding).
 */

#ifndef JDCT_H
#define JDCT_H


#include "jsc_conf_private.h"

// Abcouwer JSC 2021 - remove fixed-point

/*
 * A forward DCT routine is given a pointer to an input sample array and
 * a pointer to a work area of type DCTELEM[]; the DCT is to be performed
 * in-place in that buffer.  Type DCTELEM is int for 8-bit samples, INT32
 * for 12-bit samples.  (NOTE: Floating-point DCT implementations use an
 * array of type FAST_FLOAT, instead.)
 * The input data is to be fetched from the sample array starting at a
 * specified column.  (Any row offset needed will be applied to the array
 * pointer before it is passed to the FDCT code.)
 * Note that the number of samples fetched by the FDCT routine is
 * DCT_h_scaled_size * DCT_v_scaled_size.
 * The DCT outputs are returned scaled up by a factor of 8; they therefore
 * have a range of +-8K for 8-bit data, +-128K for 12-bit data.  This
 * convention improves accuracy in integer implementations and saves some
 * work in floating-point ones.
 * Quantization of the output coefficients is done by jcdctmgr.c.
 */


typedef JMETHOD(void, float_DCT_method_ptr, (FAST_FLOAT * data,
                JSAMPARRAY sample_data,
                JDIMENSION start_col));


/*
 * An inverse DCT routine is given a pointer to the input JBLOCK and a pointer
 * to an output sample array.  The routine must dequantize the input data as
 * well as perform the IDCT; for dequantization, it uses the multiplier table
 * pointed to by compptr->dct_table.  The output data is to be placed into the
 * sample array starting at a specified column.  (Any row offset needed will
 * be applied to the array pointer before it is passed to the IDCT code.)
 * Note that the number of samples emitted by the IDCT routine is
 * DCT_h_scaled_size * DCT_v_scaled_size.
 */

/* typedef inverse_DCT_method_ptr is declared in jpegint.h */

typedef FAST_FLOAT FLOAT_MULT_TYPE; /* preferred floating type */

/*
 * Each IDCT routine is responsible for range-limiting its results and
 * converting them to unsigned form (0..MAXJSAMPLE).  The raw outputs could
 * be quite far out of range if the input data is corrupt, so a bulletproof
 * range-limiting step is required.  We use a mask-and-table-lookup method
 * to do the combined operations quickly, assuming that RANGE_CENTER
 * (defined in jpegint.h) is a power of 2.  See the comments with
 * prepare_range_limit_table (in jdmaster.c) for more info.
 */

#define RANGE_MASK  (RANGE_CENTER * 2 - 1)
#define RANGE_SUBSET  (RANGE_CENTER - CENTERJSAMPLE)

#define IDCT_range_limit(cinfo)  ((cinfo)->sample_range_limit - RANGE_SUBSET)

// Abcouwer JSC 2021 - remove short name support

/* Extern declarations for the forward and inverse DCT routines. */

JSC_EXTERN(void) jpeg_fdct_float
    JPP((FAST_FLOAT * data, JSAMPARRAY sample_data, JDIMENSION start_col));
JSC_EXTERN(void) jpeg_idct_float
    JPP((j_decompress_ptr cinfo, jpeg_component_info * compptr,
         JCOEFPTR coef_block, JSAMPARRAY output_buf, JDIMENSION output_col));

// Abcouwer JSC 2021 - remove fixed-point support

#endif
