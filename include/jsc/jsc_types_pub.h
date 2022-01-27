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
 * @file        jsc_types_pub.h
 * @date        2021-08-06
 * @author      Neil Abcouwer
 * @brief       Public Types for JSC
 */

#ifndef JSC_TYPES_PUB_H
#define JSC_TYPES_PUB_H

#include "jsc_conf_global_types.h"
#include "jpeglib.h"

/*
 * Macro for estimating the minimum working memory required
 * Memory requirement is proportional to number of components
 * (likely 1 for gray, 3 for color) and width of an image.
 * Macro allows sizing of buffers based on constants
 */
#define JSC_WORKING_MEM_SIZE(n_comps, width_pix) \
    ((n_comps) * ((width_pix) * 16 + 7000) + 2000)

/// An image with basic data
typedef struct {
    JDIMENSION height;          // image height, pixels
    JDIMENSION width;           // image width, pixels

    JINT n_components;          // # of color components in image
    J_COLOR_SPACE color_space;  // image color space

    JSAMPLE * data;           // pointer to the beginning of image data

    /* image data is expected in the order (in this example, 3 components):
     * data[0] : row 0, column 0, component 0,
     * data[1] : row 0, column 0, component 1,
     * data[2] : row 0, column 0, component 2,
     * data[3] : row 0, column 1, component 0,
     * ...
     * data[width*n_components-1] : row 0, column width-1, component 2,
     * data[width*n_components]   : row 1, column 0, component 0,
     * data[width*n_components+1] : row 0, column 0, component 1,
     * ...
     * etc, up to [height*width*components-1]
     *
     */

} JscImage;

/// A sized buffer type
typedef struct {
    JSIZE size_bytes;   // size of the buffer in bytes
    JSIZE n_bytes_used; // number of bytes used
    U8* data;           // pointer to the data buffer
} JscBuf;


#endif /* JSC_TYPES_PUB_H */
