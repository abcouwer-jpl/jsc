/*
 * jdapimin.c
 *
 * Copyright (C) 1994-1998, Thomas G. Lane.
 * Modified 2009-2013 by Guido Vollbeding.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
 *
 * This file contains application interface code for the decompression half
 * of the JPEG library.  These are the "minimum" API routines that may be
 * needed in either the normal full-decompression case or the
 * transcoding-only case.
 *
 * Most of the routines intended to be called directly by an application
 * are in this file or in jdapistd.c.  But also see jcomapi.c for routines
 * shared by compression and decompression, and jdtrans.c for the transcoding
 * case.
 */

#include "jsc/jpegint.h"


JSC_LOCAL(JINT)
jpeg_consume_input (j_decompress_ptr cinfo);

/*
 * Initialization of a JPEG decompression object.
 * The error manager must already be set up (in case memory manager fails).
 */

JSC_GLOBAL(void)
jpeg_CreateDecompress (j_decompress_ptr cinfo,
        JINT version, JSIZE structsize)
{
  JINT i;

  /* Guard against version mismatches between library and caller. */
  cinfo->mem = NULL;                /* so jpeg_destroy knows mem mgr not called */
  JSC_ASSERT_2(version == JPEG_LIB_VERSION,
          version, JPEG_LIB_VERSION);
  JSC_ASSERT_2(structsize == SIZEOF(struct jpeg_decompress_struct),
          structsize, SIZEOF(struct jpeg_decompress_struct));

  /* For debugging purposes, we zero the whole master structure.
   * But the application has already set the err pointer, and may have set
   * client_data, so we have to save and restore those fields.
   * Note: if application hasn't set client_data, tools like Purify may
   * complain here.
   */
  // Abcouwer JSC 2021 - add stat mem here
  {
    struct jpeg_static_memory * statmem = cinfo->statmem;
    void * client_data = cinfo->client_data; /* ignore Purify complaint here */
    JSC_MEMZERO(cinfo, SIZEOF(struct jpeg_decompress_struct));
    cinfo->statmem = statmem;
    cinfo->client_data = client_data;
  }
  cinfo->is_decompressor = TRUE;

  /* Initialize a memory manager instance for this object */
  jinit_memory_mgr((j_common_ptr) cinfo);

  /* Zero out pointers to permanent structures. */
  cinfo->progress = NULL;
  cinfo->src = NULL;

  for (i = 0; i < NUM_QUANT_TBLS; i++) {
    cinfo->quant_tbl_ptrs[i] = NULL;
  }

  for (i = 0; i < NUM_HUFF_TBLS; i++) {
    cinfo->dc_huff_tbl_ptrs[i] = NULL;
    cinfo->ac_huff_tbl_ptrs[i] = NULL;
  }

  /* Initialize marker processor so application can override methods
   * for COM, APPn markers before calling jpeg_read_header.
   */
  jinit_marker_reader(cinfo);

  /* And initialize the overall input controller. */
  jinit_input_controller(cinfo);

  /* OK, I'm ready */
  cinfo->global_state = DSTATE_START;
}


/*
 * Destruction of a JPEG decompression object
 */

JSC_GLOBAL(void)
jpeg_destroy_decompress (j_decompress_ptr cinfo)
{
  jpeg_destroy((j_common_ptr) cinfo); /* use common routine */
}


// Abcouwer JSC 2021 - don't allow aborting, only destroying.

/*
 * Set default decompression parameters.
 */

JSC_LOCAL(void)
default_decompress_parms (j_decompress_ptr cinfo)
{
  JINT cid0;
  JINT cid1;
  JINT cid2;

  /* Guess the input colorspace, and set output colorspace accordingly. */
  /* Note application may override our guesses. */
  switch (cinfo->num_components) {
  case 1:
    cinfo->jpeg_color_space = JCS_GRAYSCALE;
    cinfo->out_color_space = JCS_GRAYSCALE;
    break;
    
  case 3:
    cid0 = cinfo->comp_info[0].component_id;
    cid1 = cinfo->comp_info[1].component_id;
    cid2 = cinfo->comp_info[2].component_id;

    /* First try to guess from the component IDs */
    if      (cid0 == 0x01 && cid1 == 0x02 && cid2 == 0x03)
      cinfo->jpeg_color_space = JCS_YCbCr;
    else if (cid0 == 0x01 && cid1 == 0x22 && cid2 == 0x23)
      cinfo->jpeg_color_space = JCS_BG_YCC;
    else if (cid0 == 0x52 && cid1 == 0x47 && cid2 == 0x42)
      cinfo->jpeg_color_space = JCS_RGB;        /* ASCII 'R', 'G', 'B' */
    else if (cid0 == 0x72 && cid1 == 0x67 && cid2 == 0x62)
      cinfo->jpeg_color_space = JCS_BG_RGB;        /* ASCII 'r', 'g', 'b' */
    else if (cinfo->saw_JFIF_marker)
      cinfo->jpeg_color_space = JCS_YCbCr;        /* assume it's YCbCr */
    else if (cinfo->saw_Adobe_marker) {
      switch (cinfo->Adobe_transform) {
      case 0:
        cinfo->jpeg_color_space = JCS_RGB;
        break;
      case 1:
        cinfo->jpeg_color_space = JCS_YCbCr;
        break;
      default:
        JSC_WARN_1(JWRN_ADOBE_XFORM,
                "Unknown Adobe color transform code %d",
                cinfo->Adobe_transform);
        cinfo->jpeg_color_space = JCS_YCbCr;        /* assume it's YCbCr */
        break;
      }
    } else {
      JSC_TRACE_3(cinfo->trace_level, 1, JTRC_UNKNOWN_IDS,
              "Unrecognized component IDs %d %d %d, assuming YCbCr",
              cid0, cid1, cid2);
      cinfo->jpeg_color_space = JCS_YCbCr;        /* assume it's YCbCr */
    }
    /* Always guess RGB is proper output colorspace. */
    cinfo->out_color_space = JCS_RGB;
    break;
    
  case 4:
    if (cinfo->saw_Adobe_marker) {
      switch (cinfo->Adobe_transform) {
      case 0:
        cinfo->jpeg_color_space = JCS_CMYK;
        break;
      case 2:
        cinfo->jpeg_color_space = JCS_YCCK;
        break;
      default:
          JSC_WARN_1(JWRN_ADOBE_XFORM,
                  "Unknown Adobe color transform code %d",
                  cinfo->Adobe_transform);
        cinfo->jpeg_color_space = JCS_YCCK;        /* assume it's YCCK */
        break;
      }
    } else {
      /* No special markers, assume straight CMYK. */
      cinfo->jpeg_color_space = JCS_CMYK;
    }
    cinfo->out_color_space = JCS_CMYK;
    break;
    
  default:
    cinfo->jpeg_color_space = JCS_UNKNOWN;
    cinfo->out_color_space = JCS_UNKNOWN;
    break;
  }

  /* Set defaults for other decompression parameters. */
  cinfo->scale_num = cinfo->block_size;                /* 1:1 scaling */
  cinfo->scale_denom = cinfo->block_size;
  cinfo->output_gamma = 1.0;
  cinfo->buffered_image = FALSE;
  cinfo->raw_data_out = FALSE;
  // Abcouwer JSC 2021 - no dct_method member - only float dct supported
  cinfo->do_fancy_upsampling = TRUE;
  cinfo->do_block_smoothing = TRUE;
  // Abcouwer ZSC 2021 - don't support colormaps, multiple passes
}

// Abcouwer JSC 2021 - move checks that happen during the scan into read_header
// so decompression can gracefully exit

JSC_LOCAL(boolean)
check_no_fractional_sampling(j_decompress_ptr cinfo)
{
    boolean sampling_ok = TRUE;

    // from jinit_upsampler()
    JINT ci;
    jpeg_component_info *compptr;
    JINT h_in_group;
    JINT v_in_group;
    JINT h_out_group;
    JINT v_out_group;

    JSC_ASSERT_1(cinfo->min_DCT_h_scaled_size > 0,
            cinfo->min_DCT_h_scaled_size);
    JSC_ASSERT_1(cinfo->min_DCT_v_scaled_size > 0,
            cinfo->min_DCT_v_scaled_size);

    for (ci = 0; ci < cinfo->num_components; ci++) {
        compptr = cinfo->comp_info + ci;
        /* Compute size of an "input group" after IDCT scaling.  This many samples
         * are to be converted to max_h_samp_factor * max_v_samp_factor pixels.
         */
        h_in_group = (compptr->h_samp_factor * compptr->DCT_h_scaled_size)
                / cinfo->min_DCT_h_scaled_size;
        v_in_group = (compptr->v_samp_factor * compptr->DCT_v_scaled_size)
                / cinfo->min_DCT_v_scaled_size;
        h_out_group = cinfo->max_h_samp_factor;
        v_out_group = cinfo->max_v_samp_factor;
        // if component is needed, and not fullsized or 2h1v or 2h2v,
        // and out group does not divide evenly by in group, reurn false
        if (compptr->component_needed) {
            JSC_ASSERT_1(h_in_group > 0, h_in_group);
            JSC_ASSERT_1(v_in_group > 0, v_in_group);
            if ((h_out_group % h_in_group) != 0
                    || (v_out_group % v_in_group) != 0) {
                JSC_WARN(JERR_FRACT_SAMPLE_NOTIMPL,
                        "Fractional sampling not implemented yet");
                sampling_ok = FALSE;
                break;
            }
        }
    }

    return sampling_ok;
}

JSC_LOCAL(boolean)
check_no_null_quant_tables(j_decompress_ptr cinfo)
{
    // from latch_quant_tables()
    JINT ci;
    jpeg_component_info *compptr;
    JINT qtblno;
    boolean qtbls_ok = TRUE;

    for (ci = 0; ci < cinfo->comps_in_scan; ci++) {
      compptr = cinfo->cur_comp_info[ci];
      /* No work if we already saved Q-table for this component */
      if (compptr->quant_table != NULL) {
        continue;
      }
      /* Make sure specified quantization table is present */
      qtblno = compptr->quant_tbl_no;
      if (qtblno < 0 || qtblno >= NUM_QUANT_TBLS
              || cinfo->quant_tbl_ptrs[qtblno] == NULL) {
          JSC_WARN_1(JERR_NO_QUANT_TABLE,
                  "Quantization table 0x%02x was not defined", qtblno);
          qtbls_ok = FALSE;
          break;
      }
    }

    return qtbls_ok;
}

/*
 * Decompression startup: read start of JPEG datastream to see what's there.
 * Need only initialize JPEG object and supply a data source before calling.
 *
 * This routine will read as far as the first SOS marker (ie, actual start of
 * compressed data), and will save all tables and parameters in the JPEG
 * object.  It will also initialize the decompression parameters to default
 * values, and finally return JPEG_HEADER_OK.  On return, the application may
 * adjust the decompression parameters and then call jpeg_start_decompress.
 * (Or, if the application only wanted to determine the image parameters,
 * the data need not be decompressed.  In that case, call jpeg_abort or
 * jpeg_destroy to release any temporary space.)
 * If an abbreviated (tables only) datastream is presented, the routine will
 * return JPEG_HEADER_TABLES_ONLY upon reaching EOI.  The application may then
 * re-use the JPEG object to read the abbreviated image datastream(s).
 * It is unnecessary (but OK) to call jpeg_abort in this case.
 * The JPEG_SUSPENDED return code only occurs if the data source module
 * requests suspension of the decompressor.  In this case the application
 * should load more source data and then re-call jpeg_read_header to resume
 * processing.
 * If a non-suspending data source is used and require_image is TRUE, then the
 * return code need not be inspected since only JPEG_HEADER_OK is possible.
 *
 * This routine is now just a front end to jpeg_consume_input, with some
 * extra error checking.
 */

JSC_GLOBAL(JINT)
jpeg_read_header (j_decompress_ptr cinfo, boolean require_image)
{
  JINT retcode;

  JSC_ASSERT_3(cinfo->global_state == DSTATE_START
          || cinfo->global_state == DSTATE_INHEADER,
          cinfo->global_state, DSTATE_START, DSTATE_INHEADER);

  retcode = jpeg_consume_input(cinfo);

  switch (retcode) {
  case JPEG_REACHED_SOS:
    // Abcouwer JSC 2021 - do checks that would be hard to exit gracefully later
    if(check_no_fractional_sampling(cinfo)
            && check_no_null_quant_tables(cinfo)) {
      retcode = JPEG_HEADER_OK;
    } else {
      retcode = JPEG_SUSPENDED;
    }
    break;
  case JPEG_REACHED_EOI:
    /* Complain if application wanted an image */
    if(require_image) {
        JSC_WARN(JERR_NO_IMAGE, "JPEG datastream contains no image");
        return JPEG_SUSPENDED;
    }
    /* Reset to start state; it would be safer to require the application to
     * call jpeg_abort, but we can't change it now for compatibility reasons.
     * A side effect is to free any temporary memory (there shouldn't be any).
     */
    jpeg_abort((j_common_ptr) cinfo); /* sets state = DSTATE_START */
    retcode = JPEG_HEADER_TABLES_ONLY;
    break;
  // only remaining legit code is JPEG_SUSPENDED
  default:
    JSC_ASSERT_2(retcode == JPEG_SUSPENDED, retcode, JPEG_SUSPENDED);
    /* no work */
    break;
  }

  return retcode;
}


/*
 * Consume data in advance of what the decompressor requires.
 * This can be called at any time once the decompressor object has
 * been created and a data source has been set up.
 *
 * This routine is essentially a state machine that handles a couple
 * of critical state-transition actions, namely initial setup and
 * transition from header scanning to ready-for-start_decompress.
 * All the actual input is done via the input controller's consume_input
 * method.
 */

JSC_LOCAL(JINT)
jpeg_consume_input (j_decompress_ptr cinfo)
{
  JINT retcode = JPEG_SUSPENDED;

  switch (cinfo->global_state) {
  case DSTATE_START:
    /* Start-of-datastream actions: reset appropriate modules */
    (*cinfo->inputctl->reset_input_controller) (cinfo);
    /* Initialize application's data source module */
    (*cinfo->src->init_source) (cinfo);
    cinfo->global_state = DSTATE_INHEADER;
    /*FALLTHROUGH*/
  case DSTATE_INHEADER:
    retcode = (*cinfo->inputctl->consume_input) (cinfo);
    if (retcode == JPEG_REACHED_SOS) { /* Found SOS, prepare to decompress */
      /* Set up default parameters based on header data */
      default_decompress_parms(cinfo);
      /* Set global state: ready for start_decompress */
      cinfo->global_state = DSTATE_READY;
    }
    break;
    // Abcouwer JSC 2021 - only called from jpeg_read_header()
    //in START or IN_HEADER state
  default:
    JSC_ASSERT_1(0, cinfo->global_state);
  }
  return retcode;
}


/*
 * Have we finished reading the input file?
 */

JSC_GLOBAL(boolean)
jpeg_input_complete (j_decompress_ptr cinfo)
{
  /* Check for valid jpeg object */
  JSC_ASSERT_3(cinfo->global_state >= DSTATE_START
          && cinfo->global_state <= DSTATE_STOPPING,
          cinfo->global_state, DSTATE_START, DSTATE_STOPPING);
  return cinfo->inputctl->eoi_reached;
}


/*
 * Is there more than one scan?
 */

JSC_GLOBAL(boolean)
jpeg_has_multiple_scans (j_decompress_ptr cinfo)
{
    /* Only valid after jpeg_read_header completes */
    JSC_ASSERT_3(cinfo->global_state >= DSTATE_READY
            && cinfo->global_state <= DSTATE_STOPPING,
            cinfo->global_state, DSTATE_READY, DSTATE_STOPPING);
    return cinfo->inputctl->has_multiple_scans;
}


/*
 * Finish JPEG decompression.
 *
 * This will normally just verify the file trailer and release temp storage.
 *
 * Returns FALSE if suspended.  The return value need be inspected only if
 * a suspending data source is used.
 */

JSC_GLOBAL(boolean)
jpeg_finish_decompress (j_decompress_ptr cinfo)
{
  if ((cinfo->global_state == DSTATE_SCANNING ||
       cinfo->global_state == DSTATE_RAW_OK) && ! cinfo->buffered_image) {
    /* Terminate final pass of non-buffered mode */
    // Assert not too little data
    JSC_ASSERT_2(cinfo->output_scanline >= cinfo->output_height,
            cinfo->output_scanline, cinfo->output_height);
    (*cinfo->master->finish_output_pass) (cinfo);
    cinfo->global_state = DSTATE_STOPPING;
  } else if (cinfo->global_state == DSTATE_BUFIMAGE) {
    /* Finishing after a buffered-image operation */
    cinfo->global_state = DSTATE_STOPPING;
  } else {
      /* STOPPING = repeat call after a suspension, anything else is error */
      JSC_ASSERT_2(cinfo->global_state == DSTATE_STOPPING,
              cinfo->global_state, DSTATE_STOPPING);
  }

  /* Read until EOI */
  while (! cinfo->inputctl->eoi_reached) {
    if ((*cinfo->inputctl->consume_input) (cinfo) == JPEG_SUSPENDED)
      return FALSE;                /* Suspend, come back later */
  }
  /* Do final cleanup */
  (*cinfo->src->term_source) (cinfo);
  /* We can use jpeg_abort to release memory and reset global_state */
  jpeg_abort((j_common_ptr) cinfo);
  return TRUE;
}
