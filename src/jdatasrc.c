/*
 * jdatasrc.c
 *
 * Copyright (C) 1994-1996, Thomas G. Lane.
 * Modified 2009-2019 by Guido Vollbeding.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
 *
 * This file contains decompression data source routines for the case of
 * reading JPEG data from memory or from a file (or any stdio stream).
 * While these routines are sufficient for most applications,
 * some will want to use a different source manager.
 * IMPORTANT: we assume that fread() will correctly transcribe an array of
 * JOCTETs from 8-bit-wide elements on external storage.  If char is wider
 * than 8 bits on your machine, you may need to do some tweaking.
 */

// Abcouwer JSC 2021 - remove stdio

#include "jsc/jpegint.h"


JSC_METHODDEF(void)
init_mem_source (j_decompress_ptr cinfo)
{
  /* no work necessary here */
}


JSC_METHODDEF(boolean)
fill_mem_input_buffer (j_decompress_ptr cinfo)
{
    // Abcouwer JSC 2021 - mem input is all given at once.
    // should be no need to refill
    return FALSE;
}


/*
 * Skip data --- used to skip over a potentially large amount of
 * uninteresting data (such as an APPn marker).
 *
 * Writers of suspendable-input applications must note that skip_input_data
 * is not granted the right to give a suspension return.  If the skip extends
 * beyond the data currently in the buffer, the buffer can be marked empty so
 * that the next read will cause a fill_input_buffer call that can suspend.
 * Arranging for additional bytes to be discarded before reloading the input
 * buffer is the application writer's problem.
 */

JSC_METHODDEF(void)
skip_input_data_std (j_decompress_ptr cinfo, JLONG num_bytes)
{
  struct jpeg_source_mgr * src = cinfo->src;
  JSIZE nbytes;

  /* Just a dumb implementation for now.  Could use fseek() except
   * it doesn't work on pipes.  Not clear that being smart is worth
   * any trouble anyway --- large skips are infrequent.
   */
  if (num_bytes > 0) {
    nbytes = (JSIZE) num_bytes;
    while (nbytes > src->bytes_in_buffer) {
      nbytes -= src->bytes_in_buffer;
      (void) (*src->fill_input_buffer) (cinfo);
      /* note we assume that fill_input_buffer will never return FALSE,
       * so suspension need not be handled.
       */
    }
    src->next_input_byte += nbytes;
    src->bytes_in_buffer -= nbytes;
  }
}


/*
 * An additional method that can be provided by data source modules is the
 * resync_to_restart method for error recovery in the presence of RST markers.
 * For the moment, this source module just uses the default resync method
 * provided by the JPEG library.  That method assumes that no backtracking
 * is possible.
 */


/*
 * Terminate source --- called by jpeg_finish_decompress
 * after all data has been read.  Often a no-op.
 *
 * NB: *not* called by jpeg_abort or jpeg_destroy; surrounding
 * application must deal with any cleanup that should happen even
 * for error exit.
 */

JSC_METHODDEF(void)
term_source_std (j_decompress_ptr cinfo)
{
  /* no work necessary here */
}

/*
 * Prepare for input from a supplied memory buffer.
 * The buffer must contain the whole JPEG data.
 */

JSC_GLOBAL(void)
jpeg_mem_src (j_decompress_ptr cinfo,
	      const U8 * inbuffer, JSIZE insize)
{
  struct jpeg_source_mgr * src;

  /* Treat empty input as fatal error */
  JSC_ASSERT(inbuffer != NULL);
  JSC_ASSERT(insize != 0);

  /* The source object is made permanent so that a series of JPEG images
   * can be read from the same buffer by calling jpeg_mem_src only before
   * the first one.
   */
  if (cinfo->src == NULL) {	/* first time for this JPEG object? */
    cinfo->src = (struct jpeg_source_mgr *) (*cinfo->mem->get_mem)
      ((j_common_ptr) cinfo, JPOOL_PERMANENT, SIZEOF(struct jpeg_source_mgr));
  }

  src = cinfo->src;
  src->init_source = init_mem_source;
  src->fill_input_buffer = fill_mem_input_buffer;
  src->skip_input_data = skip_input_data_std;
  src->resync_to_restart = jpeg_resync_to_restart; /* use default method */
  src->term_source = term_source_std;
  src->bytes_in_buffer = insize;
  src->next_input_byte = (const JOCTET *) inbuffer;
}
