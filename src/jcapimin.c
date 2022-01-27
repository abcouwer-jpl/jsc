/*
 * jcapimin.c
 *
 * Copyright (C) 1994-1998, Thomas G. Lane.
 * Modified 2003-2010 by Guido Vollbeding.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
 *
 * This file contains application interface code for the compression half
 * of the JPEG library.  These are the "minimum" API routines that may be
 * needed in either the normal full-compression case or the transcoding-only
 * case.
 *
 * Most of the routines intended to be called directly by an application
 * are in this file or in jcapistd.c.  But also see jcparam.c for
 * parameter-setup helper routines, jcomapi.c for routines shared by
 * compression and decompression, and jctrans.c for the transcoding case.
 */

#include "jsc/jpegint.h"

/*
 * Initialization of a JPEG compression object.
 * The error manager must already be set up (in case memory manager fails).
 */

JSC_GLOBAL(void)
jpeg_CreateCompress (j_compress_ptr cinfo,
        JINT version, JSIZE structsize)
{
    JSC_ASSERT(cinfo != NULL);

  JINT i;

  /* Guard against version mismatches between library and caller. */
  cinfo->mem = NULL;		/* so jpeg_destroy knows mem mgr not called */
  JSC_ASSERT_2(version == JPEG_LIB_VERSION,
          version, JPEG_LIB_VERSION);
  JSC_ASSERT_2(structsize == SIZEOF(struct jpeg_compress_struct),
          structsize, SIZEOF(struct jpeg_compress_struct));

  /* For debugging purposes, we zero the whole master structure.
   * But the application has already set the err pointer, and may have set
   * client_data, so we have to save and restore those fields.
   * Note: if application hasn't set client_data, tools like Purify may
   * complain here.
   */
   // Abcouwer JSC 2021 - add stat mem here
  {
    JINT trace_level = cinfo->trace_level;
    struct jpeg_static_memory * statmem = cinfo->statmem;
    void * client_data = cinfo->client_data; /* ignore Purify complaint here */
    JSC_MEMZERO(cinfo, SIZEOF(struct jpeg_compress_struct));
    cinfo->trace_level = trace_level;
    cinfo->statmem = statmem;
    cinfo->client_data = client_data;
  }
  cinfo->is_decompressor = FALSE;

  /* Initialize a memory manager instance for this object */
  jinit_memory_mgr((j_common_ptr) cinfo);

  /* Zero out pointers to permanent structures. */
  cinfo->progress = NULL;
  cinfo->dest = NULL;

  cinfo->comp_info = NULL;

  for (i = 0; i < NUM_QUANT_TBLS; i++) {
    cinfo->quant_tbl_ptrs[i] = NULL;
    cinfo->q_scale_factor[i] = 100;
  }

  for (i = 0; i < NUM_HUFF_TBLS; i++) {
    cinfo->dc_huff_tbl_ptrs[i] = NULL;
    cinfo->ac_huff_tbl_ptrs[i] = NULL;
  }

  /* Must do it here for emit_dqt in case jpeg_write_tables is used */
  cinfo->block_size = DCTSIZE;
  cinfo->natural_order = jpeg_natural_order;
  cinfo->lim_Se = DCTSIZE2-1;

  cinfo->script_space = NULL;

  cinfo->input_gamma = 1.0;	/* in case application forgets */

  /* OK, I'm ready */
  cinfo->global_state = CSTATE_START;
}


/*
 * Destruction of a JPEG compression object
 */

JSC_GLOBAL(void)
jpeg_destroy_compress (j_compress_ptr cinfo)
{
  jpeg_destroy((j_common_ptr) cinfo); /* use common routine */
}


// Abcouwer JSC 2021 - don't allow aborting, only destroying.

/*
 * Forcibly suppress or un-suppress all quantization and Huffman tables.
 * Marks all currently defined tables as already written (if suppress)
 * or not written (if !suppress).  This will control whether they get emitted
 * by a subsequent jpeg_start_compress call.
 *
 * This routine is exported for use by applications that want to produce
 * abbreviated JPEG datastreams.  It logically belongs in jcparam.c, but
 * since it is called by jpeg_start_compress, we put it here --- otherwise
 * jcparam.o would be linked whether the application used it or not.
 */

JSC_GLOBAL(void)
jpeg_suppress_tables (j_compress_ptr cinfo, boolean suppress)
{
    JSC_ASSERT(cinfo != NULL);
    JSC_ASSERT_1(suppress == TRUE || suppress == FALSE, suppress);

    JINT i;
    JQUANT_TBL *qtbl;
    JHUFF_TBL *htbl;

    for (i = 0; i < NUM_QUANT_TBLS; i++) {
        qtbl = cinfo->quant_tbl_ptrs[i];
        if (qtbl != NULL) {
            qtbl->sent_table = suppress;
        }
    }

    for (i = 0; i < NUM_HUFF_TBLS; i++) {
        htbl = cinfo->dc_huff_tbl_ptrs[i];
        if (htbl != NULL) {
            htbl->sent_table = suppress;
        }
        htbl = cinfo->ac_huff_tbl_ptrs[i];
        if (htbl != NULL) {
            htbl->sent_table = suppress;
        }
    }
}


/*
 * Finish JPEG compression.
 *
 * If a multipass operating mode was selected, this may do a great deal of
 * work including most of the actual output.
 */

JSC_GLOBAL(void)
jpeg_finish_compress (j_compress_ptr cinfo)
{
    JSC_ASSERT(cinfo != NULL);

    if (cinfo->global_state == CSTATE_SCANNING
            || cinfo->global_state == CSTATE_RAW_OK) {
        /* Terminate first pass */
        JSC_ASSERT_2(cinfo->next_scanline >= cinfo->image_height,
                cinfo->next_scanline, cinfo->image_height);
        (*cinfo->master->finish_pass)(cinfo);
    } else if (cinfo->global_state == CSTATE_WRCOEFS) {
        // do nothing
    } else {
        JSC_ASSERT_1(0, cinfo->global_state);
    }

    /* Perform any remaining passes */
    // Abcouwer JSC 2021 - only single pass should be allowed
    JSC_ASSERT(cinfo->master->is_last_pass);

    /* Write EOI, do final cleanup */
    (*cinfo->marker->write_file_trailer)(cinfo);
    (*cinfo->dest->term_destination)(cinfo);
    /* We can use jpeg_abort to release memory and reset global_state */
    jpeg_abort((j_common_ptr) cinfo);
}


/*
 * Write a special marker.
 * This is only recommended for writing COM or APPn markers.
 * Must be called after jpeg_start_compress() and before
 * first call to jpeg_write_scanlines() or jpeg_write_raw_data().
 */

JSC_GLOBAL(void)
jpeg_write_marker (j_compress_ptr cinfo, JINT marker,
		   const JOCTET *dataptr, JUINT datalen)
{
  JSC_ASSERT(cinfo != NULL);

  JSC_ASSERT_1(cinfo->next_scanline == 0, cinfo->next_scanline);
  JSC_ASSERT_1(cinfo->global_state == CSTATE_SCANNING
          || cinfo->global_state == CSTATE_RAW_OK
          || cinfo->global_state == CSTATE_WRCOEFS,
          cinfo->global_state);

  JMETHOD(void, write_marker_byte, (j_compress_ptr info, JINT val));

  JSC_ASSERT(cinfo->marker != NULL);

  (*cinfo->marker->write_marker_header) (cinfo, marker, datalen);
  write_marker_byte = cinfo->marker->write_marker_byte;	/* copy for speed */

  JSC_ASSERT(dataptr != NULL);
  while (datalen) {
    datalen--;
    (*write_marker_byte) (cinfo, *dataptr);
    dataptr++;
  }
}

/* Same, but piecemeal. */

JSC_GLOBAL(void)
jpeg_write_m_header (j_compress_ptr cinfo, JINT marker, JUINT datalen)
{
  JSC_ASSERT(cinfo != NULL);

  JSC_ASSERT_1(cinfo->next_scanline == 0, cinfo->next_scanline);
  JSC_ASSERT_1(cinfo->global_state == CSTATE_SCANNING
          || cinfo->global_state == CSTATE_RAW_OK
          || cinfo->global_state == CSTATE_WRCOEFS,
          cinfo->global_state);

  JSC_ASSERT(cinfo->marker != NULL);
  (*cinfo->marker->write_marker_header) (cinfo, marker, datalen);
}

JSC_GLOBAL(void)
jpeg_write_m_byte (j_compress_ptr cinfo, JINT val)
{
  JSC_ASSERT(cinfo != NULL);
  JSC_ASSERT(cinfo->marker != NULL);

  (*cinfo->marker->write_marker_byte) (cinfo, val);
}

// Abcouwer JSC 2021 - remove write_tables
