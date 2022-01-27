/***********************************************************************
 * Copyright 2021, by the California Institute of Technology.
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
 * @file        jscmemstatic.c
 * @date        2021-08-04
 * @author      Neil Abcouwer
 * @brief       Functions for doling out memory from a single buffer.
 *
 */

#include "jsc/jpegint.h"
#include "jsc/jmemsys.h"		/* import the system-dependent declarations */

JSC_GLOBAL(JSIZE) jpeg_get_mem_size(j_common_ptr cinfo)
{
    return cinfo->statmem->buffer_size_bytes;
}


JSC_GLOBAL(void *) jpeg_get_mem(j_common_ptr cinfo, JSIZE sizeofobject) {
    JSC_ASSERT(cinfo != NULL);
    JSC_ASSERT(cinfo->statmem != NULL);

    void *mem = NULL;
    if (cinfo->statmem->bytes_free >= sizeofobject) {
        mem = (void*) ((U8*) cinfo->statmem->buffer
                + cinfo->statmem->bytes_used);
        cinfo->statmem->bytes_used += sizeofobject;
        cinfo->statmem->bytes_free -= sizeofobject;
    }
    return mem;
}

// Init static memory
JSC_GLOBAL(struct jpeg_static_memory *) jpeg_give_static_mem(
        struct jpeg_static_memory *statmem, void *buffer,
        JSIZE buffer_size_bytes) {
    JSC_ASSERT(statmem != NULL);
    JSC_ASSERT(buffer != NULL);
    JSC_ASSERT(buffer_size_bytes > 0);;

    statmem->buffer = buffer;
    statmem->buffer_size_bytes = buffer_size_bytes;
    statmem->bytes_used = 0;
    statmem->bytes_free = buffer_size_bytes;

    return statmem;
}
