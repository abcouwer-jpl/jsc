/*
 * jcinit.c
 *
 * Copyright (C) 1991-1997, Thomas G. Lane.
 * Modified 2003-2017 by Guido Vollbeding.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
 *
 * This file contains initialization logic for the JPEG compressor.
 * This routine is in charge of selecting the modules to be executed and
 * making an initialization call to each one.
 *
 * Logically, this code belongs in jcmaster.c.  It's split out because
 * linking this routine implies linking the entire compression library.
 * For a transcoding-only application, we want to be able to use jcmaster.c
 * without linking in the whole library.
 */

#include "jsc/jpegint.h"

/*
 * Compute JPEG image dimensions and related values.
 * NOTE: this is exported for possible use by application.
 * Hence it mustn't do anything that can't be done twice.
 */

JSC_GLOBAL(void)
jpeg_calc_jpeg_dimensions (j_compress_ptr cinfo)
/* Do computations that are needed before master selection phase */
{
    JSC_ASSERT(cinfo != NULL);

    /* Sanity check on input image dimensions to prevent overflow in
     * following calculations.
     * We do check jpeg_width and jpeg_height in initial_setup in jcmaster.c,
     * but image_width and image_height can come from arbitrary data,
     * and we need some space for multiplication by block_size.
     */
    JSC_ASSERT_1(!((JLONG) cinfo->image_width >> 24), cinfo->image_width);
    JSC_ASSERT_1(!((JLONG) cinfo->image_height >> 24), cinfo->image_height);

    // Abcouwer JSC 2021 - remove DCT_SCALING support

    cinfo->jpeg_width = cinfo->image_width;
    cinfo->jpeg_height = cinfo->image_height;
    cinfo->min_DCT_h_scaled_size = DCTSIZE;
    cinfo->min_DCT_v_scaled_size = DCTSIZE;
}


/*
 * Master selection of compression modules.
 * This is done once at the start of processing an image.  We determine
 * which modules will be used and give them appropriate initialization calls.
 */

JSC_GLOBAL(void)
jinit_compress_master (j_compress_ptr cinfo)
{
    JSC_ASSERT(cinfo != NULL);

    JLONG samplesperrow;
    JDIMENSION jd_samplesperrow;

    /* For now, precision must match compiled-in value... */
    JSC_ASSERT_2(cinfo->data_precision == BITS_IN_JSAMPLE,
            cinfo->data_precision, BITS_IN_JSAMPLE);

    /* Sanity check on input image dimensions */
    JSC_ASSERT_1(cinfo->image_height > 0, cinfo->image_height);
    JSC_ASSERT_1(cinfo->image_width > 0, cinfo->image_width);
    JSC_ASSERT_1(cinfo->input_components > 0, cinfo->input_components);

    /* Width of an input scanline must be representable as JDIMENSION. */
    samplesperrow = (JLONG) cinfo->image_width * (JLONG) cinfo->input_components;
    jd_samplesperrow = (JDIMENSION) samplesperrow;
    JSC_ASSERT_2((JLONG) jd_samplesperrow == samplesperrow,
            jd_samplesperrow, samplesperrow);

    /* Compute JPEG image dimensions and related values. */
    jpeg_calc_jpeg_dimensions(cinfo);

    /* Initialize master control (includes parameter checking/processing) */
    jinit_c_master_control(cinfo, FALSE /* full compression */);

    /* Preprocessing */

    // Abcouwer JSC 2021 - remove raw data in
    jinit_color_converter(cinfo);
    jinit_downsampler(cinfo);
    jinit_c_prep_controller(cinfo, FALSE /* never need full buffer here */);
    /* Forward DCT */
    jinit_forward_dct(cinfo);

    // Abcouwer ZSC 2021 - only huffman encoding
    jinit_huff_encoder(cinfo);

    /* Need a full-image coefficient buffer in any multi-pass mode. */
    // Abcouwer JSC 2021 - remove optimized coding
    jinit_c_coef_controller(cinfo,
            (boolean) (cinfo->num_scans > 1));
    jinit_c_main_controller(cinfo, FALSE /* never need full buffer here */);

    jinit_marker_writer(cinfo);

    // Abcouwer JSC 2021 - remove, removed all features that use virtual arrays

    /* Write the datastream header (SOI) immediately.
     * Frame and scan headers are postponed till later.
     * This lets application insert special markers after the SOI.
     */
    (*cinfo->marker->write_file_header)(cinfo);
}
