/*
 * jutils.c
 *
 * Copyright (C) 1991-1996, Thomas G. Lane.
 * Modified 2009-2019 by Guido Vollbeding.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
 *
 * This file contains tables and miscellaneous utility routines needed
 * for both compression and decompression.
 * Note we prefix all global names with "j" to minimize conflicts with
 * a surrounding application.
 */

#include "jsc/jpegint.h"

/*
 * jpeg_natural_order[i] is the natural-order position of the i'th element
 * of zigzag order.
 *
 * When reading corrupted data, the Huffman decoders could attempt
 * to reference an entry beyond the end of this array (if the decoded
 * zero run length reaches past the end of the block).  To prevent
 * wild stores without adding an inner-loop test, we put some extra
 * "63"s after the real entries.  This will cause the extra coefficient
 * to be stored in location 63 of the block, not somewhere random.
 * The worst case would be a run-length of 15, which means we need 16
 * fake entries.
 */

const JINT jpeg_natural_order[DCTSIZE2+16] = {
  0,  1,  8, 16,  9,  2,  3, 10,
 17, 24, 32, 25, 18, 11,  4,  5,
 12, 19, 26, 33, 40, 48, 41, 34,
 27, 20, 13,  6,  7, 14, 21, 28,
 35, 42, 49, 56, 57, 50, 43, 36,
 29, 22, 15, 23, 30, 37, 44, 51,
 58, 59, 52, 45, 38, 31, 39, 46,
 53, 60, 61, 54, 47, 55, 62, 63,
 63, 63, 63, 63, 63, 63, 63, 63, /* extra entries for safety in decoder */
 63, 63, 63, 63, 63, 63, 63, 63
};

// Abcouwer JSC 2021 - only default orders

/*
 * Arithmetic utilities
 */

JSC_GLOBAL(JLONG)
jdiv_round_up (JLONG a, JLONG b)
/* Compute a/b rounded up to next integer, ie, ceil(a/b) */
/* Assumes a >= 0, b > 0 */
{
  JSC_ASSERT_1(a >= 0, a);
  JSC_ASSERT_1(b > 0, b);
  return (a + b - 1L) / b;
}


JSC_GLOBAL(JLONG)
jround_up (JLONG a, JLONG b)
/* Compute a rounded up to next multiple of b, ie, ceil(a/b)*b */
/* Assumes a >= 0, b > 0 */
{
  JSC_ASSERT_1(a >= 0, a);
  JSC_ASSERT_1(b > 0, b);

  a += b - 1L;
  return a - (a % b);
}

//// Abcouwer JSC 2021 - remove support for far pointers
//#define JSC_FMEMCOPY(dest,src,size) JSC_MEMCOPY((dest),(src),(size))




JSC_GLOBAL(void)
jcopy_sample_rows (JSAMPARRAY input_array, JINT source_row,
		   JSAMPARRAY output_array, JINT dest_row,
		   JINT num_rows, JDIMENSION num_cols)
/* Copy some rows of samples from one place to another.
 * num_rows rows are copied from input_array[source_row++]
 * to output_array[dest_row++]; these areas may overlap for duplication.
 * The source and destination arrays must be at least as wide as num_cols.
 */
{
  register JSAMPROW inptr, outptr;
  register JSIZE count = (JSIZE) num_cols * SIZEOF(JSAMPLE);
  register JINT row;

  input_array += source_row;
  output_array += dest_row;

  for (row = num_rows; row > 0; row--) {
    inptr = *input_array++;
    outptr = *output_array++;
    JSC_FMEMCOPY(outptr, inptr, count);
  }
}

// Abcouwer JSC 2021 - remove jcopy_block_row
