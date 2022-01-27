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
 * @file        jsc_pub.h
 * @date        2021-08-06
 * @author      Neil Abcouwer
 * @brief       Public functions for JSC
 */

#ifndef JSC_PUB_H
#define JSC_PUB_H

#include <jsc/jsc_types_pub.h>

// ensure symbols are not mangled by C++
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Compress an image with JPEG with one function
 *
 * Adds a moderate amount of restart markers for error containment,
 * which don't add much overhead. Use jsc_compress_rst() to control
 * more exactly.
 *
 * @param image - Image to be compressed.
 * @param output_mem - Space where compressed image will be stored
 *          output_mem->n_bytes_used will be size of data
 *          written to output buffer
 *          Providing the same size buffer as the input image should ensure
 *          successful compression for quality values <95 and
 *          compressible images.
 * @param working_mem - Space used for data structures
 *          Should have been sized according to JSC_WORKING_MEM_SIZE() macro
 * @param quality - Image quality, 1-100.
 *          Values higher than 95 not recommended, can expand image.
 *          Higher quality means lower compression ratios.
 *
 * @return 0 if image was compressed, -1 if image could not fit in output buffer
 */
JSC_EXTERN(JINT) jsc_compress(
        const JscImage *image,
        JscBuf *output_mem,
        JscBuf *working_mem,
        JINT quality);

/**
 * @brief Compress an image with JPEG with one function, specifying restarts
 *
 * Same as jsc_compress(), but breaks the image into a certain number of
 * sections that will decompress independently. Errors with those sections will
 * not propagate outside the section.
 *
 * @param image - Image to be compressed.
 * @param output_mem - Space where compressed image will be stored
 *          output_mem->n_bytes_used will be size of data
 *          written to output buffer
 *          Providing the same size buffer as the input image should ensure
 *          successful compression for quality values <95 and
 *          compressible images.
 * @param working_mem - Space used for data structures
 *          Should have been sized according to JSC_WORKING_MEM_SIZE() macro
 * @param quality - Image quality, 1-100.
 *          Values higher than 95 not recommended, can expand image.
 *          Higher quality means lower compression ratios.
 * @param n_restart_sections - Number of independent sections
 *          More sections means one error corrupts less of the image,
 *          but reduces compression ratios ( by just a bit)
 *          Providing 1 or less means no restart sections
 *
 * @return 0 if image was compressed, -1 if image could not fit in output buffer
 */
JSC_EXTERN(JINT) jsc_compress_rst(
        const JscImage *image,
        JscBuf *output_mem,
        JscBuf *working_mem,
        JINT quality,
        JINT n_restart_sections);

#ifdef __cplusplus
}
#endif

#endif /* JSC_PUB_H */
