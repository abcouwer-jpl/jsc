/*
 * jdatadst.c
 *
 * Copyright (C) 1994-1996, Thomas G. Lane.
 * Modified 2009-2019 by Guido Vollbeding.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
 *
 * This file contains compression data destination routines for the case of
 * emitting JPEG data to memory or to a file (or any stdio stream).
 * While these routines are sufficient for most applications,
 * some will want to use a different destination manager.
 * IMPORTANT: we assume that fwrite() will correctly transcribe an array of
 * JOCTETs into 8-bit-wide elements on external storage.  If char is wider
 * than 8 bits on your machine, you may need to do some tweaking.
 */

/* this is not a core library module, so it doesn't define JPEG_INTERNALS */

#include "jsc/jpegint.h"

/* Expanded data destination object for memory output */

typedef struct {
  struct jpeg_destination_mgr pub; /* public fields */

  U8 ** outbuffer;	/* target buffer */
  JSIZE * outsize;
  // Abcouwer JSC 2021 - no dynamic mem
  JOCTET * buffer;		/* start of buffer */
  JSIZE bufsize;
} my_mem_destination_mgr;


/*
 * Initialize destination --- called by jpeg_start_compress
 * before any data is actually written.
 */

JSC_METHODDEF(void)
init_mem_destination (j_compress_ptr cinfo)
{
  /* no work necessary here */
}


/*
 * Empty the output buffer --- called whenever buffer fills up.
 *
 * In typical applications, this should write the entire output buffer
 * (ignoring the current state of next_output_byte & free_in_buffer),
 * reset the pointer & count to the start of the buffer, and return TRUE
 * indicating that the buffer has been dumped.
 *
 * In applications that need to be able to suspend compression due to output
 * overrun, a FALSE return indicates that the buffer cannot be emptied now.
 * In this situation, the compressor will return to its caller (possibly with
 * an indication that it has not accepted all the supplied scanlines).  The
 * application should resume compression after it has made more room in the
 * output buffer.  Note that there are substantial restrictions on the use of
 * suspension --- see the documentation.
 *
 * When suspending, the compressor will back up to a convenient restart point
 * (typically the start of the current MCU). next_output_byte & free_in_buffer
 * indicate where the restart point will be if the current call returns FALSE.
 * Data beyond this point will be regenerated after resumption, so do not
 * write it out when emptying the buffer externally.
 */

JSC_METHODDEF(boolean)
empty_mem_output_buffer (j_compress_ptr cinfo)
{
    // Abcouwer JSC 2021 - no dynamic memory
    JSC_ASSERT(0);
    return FALSE;
}


/*
 * Terminate destination --- called by jpeg_finish_compress
 * after all data has been written.  Usually needs to flush buffer.
 *
 * NB: *not* called by jpeg_abort or jpeg_destroy; surrounding
 * application must deal with any cleanup that should happen even
 * for error exit.
 */

JSC_METHODDEF(void)
term_mem_destination (j_compress_ptr cinfo)
{
  JSC_ASSERT(cinfo != NULL);
  my_mem_destination_mgr * dest = (my_mem_destination_mgr *) cinfo->dest;

  *dest->outbuffer = dest->buffer;
  *dest->outsize = dest->bufsize - dest->pub.free_in_buffer;
}



/*
 * Prepare for output to a memory buffer.
 * The caller may supply an own initial buffer with appropriate size.
 * Otherwise, or when the actual data output exceeds the given size,
 * the library adapts the buffer size as necessary.
 * The standard library functions malloc/free are used for allocating
 * larger memory, so the buffer is available to the application after
 * finishing compression, and then the application is responsible for
 * freeing the requested memory.
 * Note:  An initial buffer supplied by the caller is expected to be
 * managed by the application.  The library does not free such buffer
 * when allocating a larger buffer.
 */

JSC_GLOBAL(void)
jpeg_mem_dest (j_compress_ptr cinfo,
	       U8 ** outbuffer, JSIZE * outsize)
{
  JSC_ASSERT(cinfo != NULL);

  my_mem_destination_mgr * dest;

  JSC_ASSERT(outbuffer != NULL);
  JSC_ASSERT(outsize != NULL);

  /* The destination object is made permanent so that multiple JPEG images
   * can be written to the same buffer without re-executing jpeg_mem_dest.
   */
  if (cinfo->dest == NULL) {	/* first time for this JPEG object? */
    cinfo->dest = (struct jpeg_destination_mgr *) (*cinfo->mem->get_mem)
      ((j_common_ptr) cinfo, JPOOL_PERMANENT, SIZEOF(my_mem_destination_mgr));
  }

  dest = (my_mem_destination_mgr *) cinfo->dest;
  dest->pub.init_destination = init_mem_destination;
  dest->pub.empty_output_buffer = empty_mem_output_buffer;
  dest->pub.term_destination = term_mem_destination;
  dest->outbuffer = outbuffer;
  dest->outsize = outsize;

  JSC_ASSERT(*outbuffer != NULL);
  JSC_ASSERT(*outsize != 0);

  dest->pub.next_output_byte = *outbuffer;
  dest->buffer = *outbuffer;
  dest->pub.free_in_buffer = *outsize;
  dest->bufsize = *outsize;

}
