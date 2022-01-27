/***********************************************************************
 * Copyright 2020, by the California Institute of Technology.
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
 * @file        jsc_test_private.h
 * @date        2020-06-05
 * @author      Neil Abcouwer
 * @brief       Definition private macros for jsc functions
 *
 * A user must provide a jsc_conf_private.h to define the following macros.
 * This file is copied for unit testing.
 */

#ifndef JSC_CONF_PRIVATE_H
#define JSC_CONF_PRIVATE_H

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "jsc_conf_global_types.h"


/* This library was written with the philosophy that assertions be used to
   check anomalous conditions. Functions assert if inputs
   indicate there is a logic error.
   See http://spinroot.com/p10/rule5.html.

   This file defines the JSC_ASSERT macros as simple c asserts, for testing.
   The google test suite for the repo uses "death tests" to test that they
   are called appropriately.

   It is the intent that user of the loco library will copy an
   appropriate JSC_conf.h to include/loco, such that asserts
   are defined appropriately for the application.

   Possible asserts:
        cstd asserts
        ROS_ASSERT
        BOOST_ASSERT
        (test) ? (void)(0)
               : send_asynchronous_safing_alert_to_other_process(), assert(0):

   Asserts could also be disabled, but this is is discouraged.
 */


#define JSC_ASSERT(test) do{if(test) {fflush(stdout);} assert(test);} while(0)
#define JSC_ASSERT_1(test, arg1) do { \
    if(!(test)) { printf("arg1=%d\n", arg1); fflush(stdout); }\
    assert(test); \
} while(0)
#define JSC_ASSERT_2(test, arg1, arg2) do { \
    if(!(test)) { printf("arg1=%d arg2=%d\n", (I32)(arg1), (I32)(arg2)); fflush(stdout); }\
    assert(test); \
} while(0)
#define JSC_ASSERT_3(test, arg1, arg2, arg3) do { \
    if(!(test)) { printf("arg1=%d arg2=%d arg3=%d\n", (I32)(arg1), (I32)(arg2), (I32)(arg3)); fflush(stdout); }\
    assert(test); \
} while(0)
#define JSC_ASSERT_4(test, arg1, arg2, arg3, arg4) assert(test)

// if your framework has messaging/logging infrastructure, replace here,
// or define as nothing to disable
#define JSC_WARN(id, fmt) \
    printf("WARNING " #id " "fmt"\n")
#define JSC_WARN_1(id, fmt, arg1) \
    printf("WARNING " #id " "fmt"\n", arg1)
#define JSC_WARN_2(id, fmt, arg1, arg2) \
    printf("WARNING " #id " "fmt"\n", arg1, arg2)
#define JSC_WARN_3(id, fmt, arg1, arg2, arg3) \
    printf("WARNING " #id " "fmt"\n", arg1, arg2, arg3)
#define JSC_WARN_4(id, fmt, arg1, arg2, arg3, arg4) \
    printf("WARNING " #id " "fmt"\n", arg1, arg2, arg3, arg4)
#define JSC_WARN_5(id, fmt, arg1, arg2, arg3, arg4, arg5) \
    printf("WARNING " #id " "fmt"\n", arg1, arg2, arg3, arg4, arg5)
#define JSC_WARN_6(id, fmt, arg1, arg2, arg3, arg4, arg5, arg6) \
    printf("WARNING " #id " "fmt"\n", arg1, arg2, arg3, arg4, arg5, arg6)
#define JSC_WARN_7(id, fmt, arg1, arg2, arg3, arg4, arg5, arg6, arg7) \
    printf("WARNING " #id " "fmt"\n", arg1, arg2, arg3, arg4, arg5, arg6, arg7)

// trace messages
#define JSC_TRACE(variable, threshold, id, fmt) \
    { if ((variable) >= (threshold)) { \
            printf("TRACE " #id " "fmt"\n"); \
        } \
    }
#define JSC_TRACE_1(variable, threshold, id, fmt, arg1) \
    { if ((variable) >= (threshold)) { \
            printf("TRACE " #id " "fmt"\n", arg1); \
        } \
    }
#define JSC_TRACE_2(variable, threshold, id, fmt, arg1, arg2) \
    { if ((variable) >= (threshold)) { \
            printf("TRACE " #id " "fmt"\n", arg1, arg2); \
        } \
    }
#define JSC_TRACE_3(variable, threshold, id, fmt, arg1, arg2, arg3) \
    { if ((variable) >= (threshold)) { \
            printf("TRACE " #id " "fmt"\n", arg1, arg2, arg3); \
        } \
    }
#define JSC_TRACE_4(variable, threshold, id, fmt, arg1, arg2, arg3, arg4) \
    { if ((variable) >= (threshold)) { \
            printf("TRACE " #id " "fmt"\n", arg1, arg2, arg3, arg4); \
        } \
    }
#define JSC_TRACE_5(variable, threshold, id, fmt, arg1, arg2, arg3, arg4, arg5) \
    { if ((variable) >= (threshold)) { \
            printf("TRACE " #id " "fmt"\n", arg1, arg2, arg3, arg4, arg5); \
        } \
    }
#define JSC_TRACE_8(variable, threshold, id, fmt, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8) \
    { if ((variable) >= (threshold)) { \
            printf("TRACE " #id " "fmt"\n", arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8); \
        } \
    }

// The number of elements in an array.
#define JSC_NUM_ARRAY_ELEMENTS(a)  (sizeof(a)/sizeof((a)[0]))

#define JSC_INLINE __inline__
/* These macros are used in all function definitions and extern declarations.
 * You could modify them if you need to change function linkage conventions;
 * in particular, you'll need to do that to make the library a Windows DLL.
 * Another application is to make all functions global for use with debuggers
 * or code profilers that require it.
 */

/* a function used only in its module: */
#define JSC_LOCAL(type)     static type
/* a function referenced thru EXTERNs: */
#define JSC_GLOBAL(type)        type

// Abcouwer JSC 2021 - remove noreturn_t

// Need memeory copy functions

#define JSC_MEMZERO(target,size)    memset((void *)(target), 0, (size))
#define JSC_MEMCOPY(dest,src,size)  memcpy((void *)(dest), (const void *)(src), (size))
// define with far pointers if needed
#define JSC_FMEMZERO(target,size)   JSC_MEMZERO((target),(size))
#define JSC_FMEMCOPY(dest,src,size) JSC_MEMCOPY((dest),(src),(size))

/*
 * In ANSI C, and indeed any rational implementation, size_t is also the
 * type returned by sizeof().  However, it seems there are some irrational
 * implementations out there, in which sizeof() returns an int even though
 * size_t is defined as long or unsigned long.  To ensure consistent results
 * we always use this SIZEOF() macro in place of using sizeof() directly.
 */
#define SIZEOF(object)  (sizeof(object))

// 8 bits in a sample
#define BITS_IN_JSAMPLE  8

/*
 * Maximum number of components (color channels) allowed in JPEG image.
 * To meet the letter of the JPEG spec, set this to 255.  However, darn
 * few applications need more than 4 channels (maybe 5 for CMYK + alpha
 * mask).  We recommend 10 as a reasonable compromise; use 4 if you are
 * really short on memory.  (Each allowed component costs a hundred or so
 * bytes of storage, whether actually used in an image or not.)
 */

#define MAX_COMPONENTS  10  /* maximum number of image components */

/*
 * Ordering of RGB data in scanlines passed to or from the application.
 * If your application wants to deal with data in the order B,G,R, just
 * change these macros.  You can also deal with formats such as R,G,B,X
 * (one extra byte per pixel) by changing RGB_PIXELSIZE.  Note that changing
 * the offsets will also change the order in which colormap data is organized.
 * RESTRICTIONS:
 * 1. The sample applications cjpeg,djpeg do NOT support modified RGB formats.
 * 2. The color quantizer modules will not behave desirably if RGB_PIXELSIZE
 *    is not 3 (they don't understand about dummy color components!).  So you
 *    can't use color quantization if you change that value.
 */

#define RGB_RED     0   /* Offset of Red in an RGB scanline element */
#define RGB_GREEN   1   /* Offset of Green */
#define RGB_BLUE    2   /* Offset of Blue */
#define RGB_PIXELSIZE   3   /* JSAMPLEs per RGB scanline element */

/* FAST_FLOAT should be either float or double, whichever is done faster
 * by your compiler.  (Note that this type is only used in the floating point
 * DCT routines, so it only matters if you've defined DCT_FLOAT_SUPPORTED.)
 * Typically, float is faster in ANSI C compilers, while double is faster in
 * pre-ANSI compilers (because they insist on converting to double anyway).
 * The code below therefore chooses float if we have ANSI-style prototypes.
 */

#define FAST_FLOAT  F64



#endif /* JSC_CONF_PRIVATE_H */
