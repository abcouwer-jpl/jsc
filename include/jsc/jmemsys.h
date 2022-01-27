/*
 * jmemsys.h
 *
 * Copyright (C) 1992-1997, Thomas G. Lane.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
 *
 * This include file defines the interface between the system-independent
 * and system-dependent portions of the JPEG memory manager.  No other
 * modules need include it.  (The system-independent portion is jmemmgr.c;
 * there are several different versions of the system-dependent portion.)
 *
 * This file works as-is for the system-dependent memory managers supplied
 * in the IJG distribution.  You may need to modify it if you write a
 * custom memory manager.  If system-dependent changes are needed in
 * this file, the best method is to #ifdef them based on a configuration
 * symbol supplied in jconfig.h, as we have done with USE_MSDOS_MEMMGR
 * and USE_MAC_MEMMGR.
 */

// Abcouwer JSC 2021 - remove functions not needed for getting mem from buffer
#ifndef JMEMSYS_H
#define JMEMSYS_H

#include "jsc_conf_private.h"

JSC_EXTERN(JSIZE) jpeg_get_mem_size(j_common_ptr cinfo);
JSC_EXTERN(void *) jpeg_get_mem JPP((j_common_ptr cinfo, JSIZE sizeofobject));

#endif
