/*
 * jmemmgr.c
 *
 * Copyright (C) 1991-1997, Thomas G. Lane.
 * Modified 2011-2019 by Guido Vollbeding.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
 *
 * This file contains the JPEG system-independent memory management
 * routines.  This code is usable across a wide variety of machines; most
 * of the system dependencies have been isolated in a separate file.
 * The major functions provided here are:
 *   * pool-based allocation and freeing of memory;
 *   * policy decisions about how to divide available memory among the
 *     virtual arrays;
 *   * control logic for swapping virtual arrays between main memory and
 *     backing storage.
 * The separate system-dependent file provides the actual backing-storage
 * access code, and it contains the policy decision about how much total
 * main memory to use.
 * This file is system-dependent in the sense that some of its functions
 * are unnecessary in some systems.  For example, if there is enough virtual
 * memory so that backing storage will never be used, much of the virtual
 * array control logic could be removed.  (Of course, if you have that much
 * memory then you shouldn't care about a little bit of unused code...)
 */

// Abcouwer JSC 2021 - remove unneeded logic for static memory
#include "jsc/jpegint.h"
#include "jsc/jmemsys.h"                /* import the system-dependent declarations */

#define ALIGN_TYPE  F64

/*
 * Here is the full definition of a memory manager object.
 */

typedef struct {
    struct jpeg_memory_mgr pub; /* public fields */

    /* This counts total space obtained from jpeg_get_small/large */
    JSIZE total_space_used;

} my_memory_mgr;


JSC_METHODDEF(JSIZE) get_mem_size_std(j_common_ptr cinfo)
{
    return jpeg_get_mem_size(cinfo);
}

JSC_METHODDEF(void *) get_mem_std(j_common_ptr cinfo, JINT pool_id,
        JSIZE sizeofobject) {
    JSC_ASSERT_2(sizeofobject <= JSIZE_MAX, sizeofobject, JSIZE_MAX);

    /* Round up the requested size to a multiple of SIZEOF(ALIGN_TYPE) */
    JSIZE sizeofobject_adjust = sizeofobject;
    JSIZE odd_bytes = sizeofobject % SIZEOF(ALIGN_TYPE);
    if (odd_bytes > 0) {
        sizeofobject_adjust += SIZEOF(ALIGN_TYPE) - odd_bytes;
        // assert no overflow
        JSC_ASSERT_2(sizeofobject_adjust > sizeofobject,
                sizeofobject_adjust, sizeofobject);
    }

    void *ptr = jpeg_get_mem(cinfo, sizeofobject_adjust);
    // assert we didn't run out - user should have used WORKING_MEM_SIZE()
    // to check they were providing enough memory
    JSC_ASSERT(ptr != NULL);
    return ptr;
}

/*
 * Creation of 2-D sample arrays.
 * The pointers are in near heap, the samples themselves in FAR heap.
 *
 * To minimize allocation overhead and to allow I/O of large contiguous
 * blocks, we allocate the sample rows in groups of as many rows as possible
 * without exceeding MAX_ALLOC_CHUNK total bytes per allocation request.
 * NB: the virtual array control routines, later in this file, know about
 * this chunking of rows.  The rowsperchunk value is left in the mem manager
 * object so that it can be saved away if this sarray is the workspace for
 * a virtual array.
 */

JSC_METHODDEF(JSAMPARRAY) get_sarray_std(j_common_ptr cinfo, JINT pool_id,
        JDIMENSION samplesperrow, JDIMENSION numrows)
/* Allocate a 2-D sample array */
{
    JSAMPARRAY result;
    JSAMPROW workspace;
    JDIMENSION rowsperchunk;
    JDIMENSION currow;
    JDIMENSION i;

    // sanity check. rows should be no more than
    // DCT size * max sampling factor (8*4)
    JSC_ASSERT_3(numrows <= DCTSIZE * MAX_SAMP_FACTOR, numrows, DCTSIZE,
            MAX_SAMP_FACTOR);

    // Abcouwer JSC 2021 - remove MAX_ALLOC_CHUNK
    rowsperchunk = numrows;

    /* Get space for row pointers (small object) */
    result = (JSAMPARRAY) get_mem_std(cinfo, pool_id,
            (JSIZE) numrows * SIZEOF(JSAMPROW));

    /* Get the rows themselves (large objects) */
    currow = 0;
    while (currow < numrows) {
        rowsperchunk = JSC_MIN(rowsperchunk, numrows - currow);
        workspace = (JSAMPROW) get_mem_std(cinfo, pool_id,
                (JSIZE) rowsperchunk * (JSIZE) samplesperrow * SIZEOF(JSAMPLE));
        for (i = rowsperchunk; i > 0; i--) {
            result[currow] = workspace;
            currow++;
            workspace += samplesperrow;
        }
    }

    return result;
}

/*
 * Memory manager initialization.
 * When this is called, only the error manager pointer is valid in cinfo!
 */

JSC_GLOBAL(void) jinit_memory_mgr(j_common_ptr cinfo) {
    JSC_ASSERT(cinfo != NULL);

    my_memory_mgr *mem;

    cinfo->mem = NULL; /* for safety if init fails */

    /* Check for configuration errors.
     * SIZEOF(ALIGN_TYPE) should be a power of 2; otherwise, it probably
     * doesn't reflect any real hardware alignment requirement.
     * The test is a little tricky: for X>0, X and X-1 have no one-bits
     * in common if and only if X is a power of 2, ie has only one one-bit.
     */
    JSC_COMPILE_ASSERT((SIZEOF(ALIGN_TYPE) & (SIZEOF(ALIGN_TYPE)-1)) == 0,
            bad_align);

    /* Attempt to allocate memory manager's control block */
    mem = (my_memory_mgr*) jpeg_get_mem(cinfo, SIZEOF(my_memory_mgr));

    JSC_ASSERT(mem != NULL);

    mem->pub.get_mem_size = get_mem_size_std;
    mem->pub.get_mem = get_mem_std;
    mem->pub.get_sarray = get_sarray_std;
    mem->total_space_used = SIZEOF(my_memory_mgr);

    /* Declare ourselves open for business */
    cinfo->mem = &mem->pub;
}
