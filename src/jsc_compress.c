/***********************************************************************
 * Copyright 2021 by the California Institute of Technology
 * ALL RIGHTS RESERVED. United States Government Sponsorship acknowledged.
 *
 * Any commercial use must be negotiated with the Office of
 * Technology Transfer at the California Institute of Technology.
 *
 * This software may be subject to U.S. export control laws.
 * By accepting this software, the user agrees to comply with all
 * applicable U.S. export laws and regulations. User has the
 * responsibility to obtain export licenses, or other export
 * authority as may be required before exporting such information
 * to foreign countries or providing access to foreign persons.
 *
 * @file        jsc_compress.c
 * @date        2021-08-06
 * @author      Neil Abcouwer
 * @brief       Compression function for JSC
 */

#include <jsc/jsc_conf_private.h>
#include <jsc/jsc_pub.h>
#include <jsc/jpeglib.h>
#include <jsc/jpegint.h>

JSC_GLOBAL(JINT)
jsc_compress(
        const JscImage *image,
        JscBuf *output_mem,
        JscBuf *working_mem,
        JINT quality)
{
    // use a reasonable number of restarts. they don't add much overhead
    JSC_ASSERT(image != NULL);
    JINT n_restart_sections = image->height / DCTSIZE2;

    return jsc_compress_rst(image, output_mem, working_mem,
            quality, n_restart_sections);
}

// divide image into independent sections
JSC_LOCAL(void)
set_restart_sections(j_compress_ptr cinfo, JINT n_restart_sections)
{
    if(n_restart_sections > 1) {
        // find largest vertical sampling factor
        JSC_ASSERT_1(cinfo->num_components > 0,cinfo->num_components);
        JSC_ASSERT_2(cinfo->num_components <= MAX_COMPS_IN_SCAN,
                cinfo->num_components, MAX_COMPS_IN_SCAN);
        JSC_ASSERT(cinfo->comp_info != NULL);
        jpeg_component_info *compptr;
        JINT max_v_samp_factor = 1;

        for (JINT ci = 0; ci < cinfo->num_components; ci++) {
            compptr = cinfo->comp_info + ci;
            JSC_ASSERT_1(compptr->v_samp_factor > 0, compptr->v_samp_factor);
            JSC_ASSERT_2(compptr->v_samp_factor <= MAX_SAMP_FACTOR,
                    compptr->v_samp_factor, MAX_SAMP_FACTOR);
            max_v_samp_factor = JSC_MAX(max_v_samp_factor,
                                         compptr->v_samp_factor);
        }

        // determine image height in MCU blocks
        JDIMENSION MCU_rows_in_scan = (JDIMENSION)
          jdiv_round_up((JLONG) cinfo->image_width,
                        (JLONG) (max_v_samp_factor * cinfo->block_size));

        // rows per restart = (rows/scan) / (restars/scan)
        cinfo->restart_in_rows = (JUINT)jdiv_round_up(
                (JLONG) MCU_rows_in_scan, (JLONG) n_restart_sections);
        JSC_ASSERT_1(cinfo->restart_in_rows > 0, cinfo->restart_in_rows);
    }
}

JSC_GLOBAL(JINT)
jsc_compress_rst(
        const JscImage *image,
        JscBuf *output_mem,
        JscBuf *working_mem,
        JINT quality,
        JINT n_restart_sections)
{
    JSC_ASSERT(image != NULL);
    JSC_ASSERT(output_mem != NULL);
    JSC_ASSERT(working_mem != NULL);

    // check working memory size
    JSC_ASSERT_3(working_mem->size_bytes
            >= JSC_WORKING_MEM_SIZE(image->n_components, image->width),
            working_mem->size_bytes,image->n_components, image->width);

    struct jpeg_compress_struct cinfo;
    struct jpeg_static_memory statmem;
    JSAMPROW row_pointer[1];      /* pointer to JSAMPLE row[s] */
    I32 row_stride;               /* physical row width in image buffer */

    /* Step 1: allocate and initialize JPEG compression object */
    cinfo.statmem = jpeg_give_static_mem(&statmem,
            working_mem->data, working_mem->size_bytes);
    jpeg_create_compress(&cinfo);

    /* Step 2: specify data destination */
    output_mem->n_bytes_used = output_mem->size_bytes;
    jpeg_mem_dest(&cinfo, &output_mem->data, &output_mem->n_bytes_used);

    /* Step 3: set parameters for compression */
    cinfo.image_width = image->width;  /* image width and height, in pixels */
    cinfo.image_height = image->height;
    cinfo.input_components = image->n_components;   /* # of color components per pixel */
    cinfo.in_color_space = image->color_space;       /* colorspace of input image */

    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE /* limit to baseline-JPEG values */);

    // set restarts
    set_restart_sections(&cinfo, n_restart_sections);

    /* Step 4: Start compressor */
    jpeg_start_compress(&cinfo, TRUE);

    // write special marker
    U8 utext[4] = {'J','S','C', 0};
    jpeg_write_marker(&cinfo, JPEG_COM, utext, 4);

    /* Step 5: while (scan lines remain to be written) */
    /*           jpeg_write_scanlines(...); */
    row_stride = cinfo.image_width * cinfo.input_components;
    while (cinfo.next_scanline < cinfo.image_height) {
        row_pointer[0] = &image->data[cinfo.next_scanline * row_stride];
        JDIMENSION rows_written =  jpeg_write_scanlines(&cinfo, row_pointer, 1);
        if(rows_written != 1) {
            JSC_WARN(JERR_JSC_WRITE_LINE_FAIL,
                    "Failed to write scanline, output buffer may be filled.");
            working_mem->n_bytes_used = cinfo.statmem->bytes_used;
            return -1;
        }
    }

    /* Step 6: Finish compression */
    jpeg_finish_compress(&cinfo);
    working_mem->n_bytes_used = cinfo.statmem->bytes_used;
    // jpeg writes to output_mem->n_bytes_used, pointer given previously

    /* Step 7: release JPEG compression object */
    jpeg_destroy_compress(&cinfo);

    return 0;
}


