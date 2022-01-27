/***********************************************************************
 * Copyright 2020 by the California Institute of Technology
 * ALL RIGHTS RESERVED. United States Government Sponsorship acknowledged.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * @file        jsc_cfs_global_types.h
 * @date        2021-01-27
 * @author      Neil Abcouwer
 * @brief       Definition of global types for testing jsc
 *
 * loco_conf_types_pub.h defines public types used by public jsc functions.
 * and follows the common guideline, as expressed in MISRA
 * directive 4.6, "typedefs that indicate size and signedness should be used
 * in place of the basic numerical types".
 *
 * jsc_types_pub.h includes jsc_conf_global_types.h, which must define
 * these types.  For the purposes of working in the NASA core FSW framework
 * (cFS), test/jsc_cfs_global_types.h is copied to
 * include/jsc/jsc_conf_global_types.h.
 * It includes common_types.h from cFS' osal, https://github.com/nasa/osal
 *
 */

#ifndef JSC_CONF_GLOBAL_TYPES_H
#define JSC_CONF_GLOBAL_TYPES_H

#include "common_types.h" // defines NULL and sized types

#define JSC_COMPILE_ASSERT(test, msg) CompileTimeAssert(test, msg)

/* These macros are used in all function definitions and extern declarations.
 * You could modify them if you need to change function linkage conventions;
 * in particular, you'll need to do that to make the library a Windows DLL.
 * Another application is to make all functions global for use with debuggers
 * or code profilers that require it.
 */

/* a function called through method pointers: */
#define JSC_METHODDEF(type)     static type

/* This macro is used to declare a "method", that is, a function pointer.
 * We want to supply prototype parameters if the compiler can cope.
 * Note that the arglist parameter must be parenthesized!
 * Again, you can customize this if you need special linkage keywords.
 */
// Abcouwer JSC 2021 - assume we have prototypes
#define JMETHOD(type,methodname,arglist)  type (*methodname) arglist

/* a reference to a GLOBAL function: */
#define JSC_EXTERN(type)        extern type

// fixed sizes
typedef int8 I8;
typedef int16 I16;
typedef int32 I32;
typedef uint8 U8;
typedef uint16 U16;
typedef uint32 U32;
typedef float  F32;
typedef double  F64;

#define U32_MAX (U32)(4294967295U)

JSC_COMPILE_ASSERT(sizeof(I8)  == 1,  I8BadSize);
JSC_COMPILE_ASSERT(sizeof(I16) == 2, I16BadSize);
JSC_COMPILE_ASSERT(sizeof(I32) == 4, I32BadSize);
JSC_COMPILE_ASSERT(sizeof(U8)  == 1,  U8BadSize);
JSC_COMPILE_ASSERT(sizeof(U16) == 2, U16BadSize);
JSC_COMPILE_ASSERT(sizeof(U32) == 4, U32BadSize);
JSC_COMPILE_ASSERT(sizeof(F32) == 4, F32BadSize);
JSC_COMPILE_ASSERT(sizeof(F64) == 8, F64BadSize);

// macros that allow replacement of int, unsigned int, and long with fixed size
typedef I32 JINT;
typedef U32 JUINT;
typedef I32 JLONG;

JSC_COMPILE_ASSERT(sizeof(JINT)  >= 2, JINTBadSize);
JSC_COMPILE_ASSERT(sizeof(JUINT) >= 2, JUINTBadSize);
JSC_COMPILE_ASSERT(sizeof(JLONG) >= 4, JLONGBadSize);

// fixed-size replacement for size_T
typedef U32 JSIZE;

#define JSIZE_MAX U32_MAX

JSC_COMPILE_ASSERT(sizeof(JSIZE) >= sizeof(JLONG), JSIZEBadSize);


// define JPEG's sized types (from jmorecfg) here

/*
 * Basic data types.
 * You may need to change these if you have a machine with unusual data
 * type sizes; for example, "char" not 8 bits, "short" not 16 bits,
 * or "long" not 32 bits.  We don't care whether "int" is 16 or 32 bits,
 * but it had better be at least 16.
 */

/* Representation of a single sample (pixel element value).
 * We frequently allocate large arrays of these, so it's important to keep
 * them small.  But if you have memory to burn and access to char or short
 * arrays is very slow on your hardware, you might want to change these.
 */
/* JSAMPLE should be the smallest type that will hold the values 0..255.
 * You can use a signed char by having GETJSAMPLE mask it with 0xFF.
 */
#define BITS_IN_JSAMPLE  8
typedef U8 JSAMPLE;
#define MAXJSAMPLE  255
#define CENTERJSAMPLE   128

#define GETJSAMPLE(value)  ((JINT) (value))

/*
 * Maximum number of components (color channels) allowed in JPEG image.
 * To meet the letter of the JPEG spec, set this to 255.  However, darn
 * few applications need more than 4 channels (maybe 5 for CMYK + alpha
 * mask).  We recommend 10 as a reasonable compromise; use 4 if you are
 * really short on memory.  (Each allowed component costs a hundred or so
 * bytes of storage, whether actually used in an image or not.)
 */

#define MAX_COMPONENTS  10  /* maximum number of image components */

/* Representation of a DCT frequency coefficient.
 * This should be a signed value of at least 16 bits; "short" is usually OK.
 * Again, we allocate large arrays of these, but you can change to int
 * if you have memory to burn and "short" is really slow.
 */
typedef I16 JCOEF;

/* Compressed datastreams are represented as arrays of JOCTET.
 * These must be EXACTLY 8 bits wide, at least once they are written to
 * external storage.  Note that when using the stdio data source/destination
 * managers, this is also the data type passed to fread/fwrite.
 */
typedef U8 JOCTET;

#define GETJOCTET(value)  (value)


/* These typedefs are used for various table entries and so forth.
 * They must be at least as wide as specified; but making them too big
 * won't cost a huge amount of memory, so we don't provide special
 * extraction code like we did for JSAMPLE.  (In other words, these
 * typedefs live at a different point on the speed/space tradeoff curve.)
 */

/* UINT8 must hold at least the values 0..255. */
typedef U8 UINT8;
/* UINT16 must hold at least the values 0..65535. */
typedef U16 UINT16;
/* INT16 must hold at least the values -32768..32767. */
typedef I16 INT16;
/* INT32 must hold at least signed 32-bit values. */
typedef I32 INT32;

/* Datatype used for image dimensions.  The JPEG standard only supports
 * images up to 64K*64K due to 16-bit fields in SOF markers.  Therefore
 * "unsigned int" is sufficient on all machines.  However, if you need to
 * handle larger images and you don't mind deviating from the spec, you
 * can change this datatype.
 */
typedef U32 JDIMENSION;

#define JPEG_MAX_DIMENSION  65500L  /* a tad under 64K to prevent overflows */

///*
// * On a few systems, type boolean and/or its values FALSE, TRUE may appear
// * in standard header files.  Or you may have conflicts with application-
// * specific header files that you want to include together with these files.
// * Defining HAVE_BOOLEAN before including jpeglib.h should make it work.
// */
//#ifndef HAVE_BOOLEAN
//#if defined FALSE || defined TRUE || defined QGLOBAL_H
///* Qt3 defines FALSE and TRUE as "const" variables in qglobal.h */
//typedef int boolean;
//#ifndef FALSE           /* in case these macros already exist */
//#define FALSE   0       /* values of boolean */
//#endif
//#ifndef TRUE
//#define TRUE    1
//#endif
//#else
//typedef enum {
//    FALSE = 0,
//    TRUE = 1
//} boolean;
//#endif
//#endif
//
//#if 0
//
////! \brief Define Bool and its values TRUE and FLASE.
//typedef enum boolean_t {
//   FALSE = 0,  /*!< Falsity truth constant */
//   TRUE  = 1    /*!< Truth truth constant */
//} Bool;
//#endif



#endif /* LOCO_CONF_GLOBAL_TYPES_H */
