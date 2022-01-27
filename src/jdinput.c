/*
 * jdinput.c
 *
 * Copyright (C) 1991-1997, Thomas G. Lane.
 * Modified 2002-2013 by Guido Vollbeding.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
 *
 * This file contains input control logic for the JPEG decompressor.
 * These routines are concerned with controlling the decompressor's input
 * processing (marker reading and coefficient decoding).  The actual input
 * reading is done in jdmarker.c, jdhuff.c, and jdarith.c.
 */

#include "jsc/jpegint.h"

/* Private state */

typedef struct {
  struct jpeg_input_controller pub; /* public fields */

  JINT inheaders;                /* Nonzero until first SOS is reached */
} my_input_controller;


/* Forward declarations */
JSC_METHODDEF(JINT) consume_markers JPP((j_decompress_ptr cinfo));


/*
 * Routines to calculate various quantities related to the size of the image.
 */


/*
 * Compute output image dimensions and related values.
 * NOTE: this is exported for possible use by application.
 * Hence it mustn't do anything that can't be done twice.
 */

JSC_GLOBAL(void)
jpeg_core_output_dimensions (j_decompress_ptr cinfo)
/* Do computations that are needed before master selection phase.
 * This function is used for transcoding and full decompression.
 */
{

  /* Hardwire it to "no scaling" */
  cinfo->output_width = cinfo->image_width;
  cinfo->output_height = cinfo->image_height;
  /* initial_setup has already initialized DCT_scaled_size,
   * and has computed unscaled downsampled_width and downsampled_height.
   */

}


JSC_LOCAL(void)
initial_setup (j_decompress_ptr cinfo)
/* Called once, when first SOS marker is reached */
{
  JINT ci;
  jpeg_component_info *compptr;

  /* Make sure image isn't bigger than I can handle */
  JSC_ASSERT_2((JLONG) cinfo->image_height <= (JLONG) JPEG_MAX_DIMENSION,
          cinfo->image_height, JPEG_MAX_DIMENSION);
  JSC_ASSERT_2((JLONG) cinfo->image_width <= (JLONG) JPEG_MAX_DIMENSION,
          cinfo->image_width, JPEG_MAX_DIMENSION);

  /* Only 8 to 12 bits data precision are supported for DCT based JPEG */
  JSC_ASSERT_1(cinfo->data_precision >= 8,  cinfo->data_precision);
  JSC_ASSERT_1(cinfo->data_precision <= 12, cinfo->data_precision);

  /* Check that number of components won't exceed internal array sizes */
  JSC_ASSERT_2(cinfo->num_components <= MAX_COMPONENTS,
          cinfo->num_components, MAX_COMPONENTS);

  /* Compute maximum sampling factors; check factor validity */
  cinfo->max_h_samp_factor = 1;
  cinfo->max_v_samp_factor = 1;
  for (ci = 0; ci < cinfo->num_components; ci++) {
      compptr = cinfo->comp_info + ci;
      JSC_ASSERT_1(compptr->h_samp_factor > 0, compptr->h_samp_factor);
      JSC_ASSERT_2(compptr->h_samp_factor<=MAX_SAMP_FACTOR,
              compptr->h_samp_factor, MAX_SAMP_FACTOR);
      JSC_ASSERT_1(compptr->v_samp_factor > 0, compptr->v_samp_factor);
      JSC_ASSERT_2(compptr->v_samp_factor<=MAX_SAMP_FACTOR,
              compptr->v_samp_factor, MAX_SAMP_FACTOR);

    cinfo->max_h_samp_factor = JSC_MAX(cinfo->max_h_samp_factor,
                                   compptr->h_samp_factor);
    cinfo->max_v_samp_factor = JSC_MAX(cinfo->max_v_samp_factor,
                                   compptr->v_samp_factor);
  }

  /* Derive block_size, natural_order, and lim_Se */
  // Abcouwer JSC 2021 - baseline only
  JSC_ASSERT(cinfo->is_baseline);
    cinfo->block_size = DCTSIZE;
    cinfo->natural_order = jpeg_natural_order;
    cinfo->lim_Se = DCTSIZE2-1;

  /* We initialize DCT_scaled_size and min_DCT_scaled_size to block_size.
   * In the full decompressor,
   * this will be overridden by jpeg_calc_output_dimensions in jdmaster.c;
   * but in the transcoder,
   * jpeg_calc_output_dimensions is not used, so we must do it here.
   */
  cinfo->min_DCT_h_scaled_size = cinfo->block_size;
  cinfo->min_DCT_v_scaled_size = cinfo->block_size;

  /* Compute dimensions of components */
  for (ci = 0; ci < cinfo->num_components; ci++) {
    compptr = cinfo->comp_info + ci;
    compptr->DCT_h_scaled_size = cinfo->block_size;
    compptr->DCT_v_scaled_size = cinfo->block_size;
    /* Size in DCT blocks */
    compptr->width_in_blocks = (JDIMENSION)
      jdiv_round_up((JLONG) cinfo->image_width * (JLONG) compptr->h_samp_factor,
                    (JLONG) (cinfo->max_h_samp_factor * cinfo->block_size));
    compptr->height_in_blocks = (JDIMENSION)
      jdiv_round_up((JLONG) cinfo->image_height * (JLONG) compptr->v_samp_factor,
                    (JLONG) (cinfo->max_v_samp_factor * cinfo->block_size));
    /* downsampled_width and downsampled_height will also be overridden by
     * jdmaster.c if we are doing full decompression.  The transcoder library
     * doesn't use these values, but the calling application might.
     */
    /* Size in samples */
    compptr->downsampled_width = (JDIMENSION)
      jdiv_round_up((JLONG) cinfo->image_width * (JLONG) compptr->h_samp_factor,
                    (JLONG) cinfo->max_h_samp_factor);
    compptr->downsampled_height = (JDIMENSION)
      jdiv_round_up((JLONG) cinfo->image_height * (JLONG) compptr->v_samp_factor,
                    (JLONG) cinfo->max_v_samp_factor);
    /* Mark component needed, until color conversion says otherwise */
    compptr->component_needed = TRUE;
    /* Mark no quantization table yet saved for component */
    compptr->quant_table = NULL;
  }

  /* Compute number of fully interleaved MCU rows. */
  cinfo->total_iMCU_rows = (JDIMENSION)
    jdiv_round_up((JLONG) cinfo->image_height,
                  (JLONG) (cinfo->max_v_samp_factor * cinfo->block_size));

  /* Decide whether file contains multiple scans */
  // Abcouwer JSC 2021 - no progressive
  if (cinfo->comps_in_scan < cinfo->num_components /*|| cinfo->progressive_mode*/) {
    cinfo->inputctl->has_multiple_scans = TRUE;
  } else {
    cinfo->inputctl->has_multiple_scans = FALSE;
  }
}


JSC_LOCAL(void)
per_scan_setup (j_decompress_ptr cinfo)
/* Do computations that are needed before processing a JPEG scan */
/* cinfo->comps_in_scan and cinfo->cur_comp_info[] were set from SOS marker */
{
  JINT ci;
  JINT mcublks;
  JINT tmp;
  jpeg_component_info *compptr;
  
  if (cinfo->comps_in_scan == 1) {
    
    /* Noninterleaved (single-component) scan */
    compptr = cinfo->cur_comp_info[0];
    
    /* Overall image size in MCUs */
    cinfo->MCUs_per_row = compptr->width_in_blocks;
    cinfo->MCU_rows_in_scan = compptr->height_in_blocks;
    
    /* For noninterleaved scan, always one block per MCU */
    compptr->MCU_width = 1;
    compptr->MCU_height = 1;
    compptr->MCU_blocks = 1;
    compptr->MCU_sample_width = compptr->DCT_h_scaled_size;
    compptr->last_col_width = 1;
    /* For noninterleaved scans, it is convenient to define last_row_height
     * as the number of block rows present in the last iMCU row.
     */
    tmp = (JINT) (compptr->height_in_blocks % compptr->v_samp_factor);
    if (tmp == 0) {
        tmp = compptr->v_samp_factor;
    }
    compptr->last_row_height = tmp;
    
    /* Prepare array describing MCU composition */
    cinfo->blocks_in_MCU = 1;
    cinfo->MCU_membership[0] = 0;
    
  } else {
    
    /* Interleaved (multi-component) scan */
    JSC_ASSERT_1(cinfo->comps_in_scan > 0, cinfo->comps_in_scan);
    JSC_ASSERT_2(cinfo->comps_in_scan <= MAX_COMPS_IN_SCAN,
            cinfo->comps_in_scan, MAX_COMPS_IN_SCAN);
    
    /* Overall image size in MCUs */
    cinfo->MCUs_per_row = (JDIMENSION)
      jdiv_round_up((JLONG) cinfo->image_width,
                    (JLONG) (cinfo->max_h_samp_factor * cinfo->block_size));
    cinfo->MCU_rows_in_scan = (JDIMENSION)
      jdiv_round_up((JLONG) cinfo->image_height,
                    (JLONG) (cinfo->max_v_samp_factor * cinfo->block_size));
    
    cinfo->blocks_in_MCU = 0;
    
    for (ci = 0; ci < cinfo->comps_in_scan; ci++) {
      compptr = cinfo->cur_comp_info[ci];
      /* Sampling factors give # of blocks of component in each MCU */
      compptr->MCU_width = compptr->h_samp_factor;
      compptr->MCU_height = compptr->v_samp_factor;
      compptr->MCU_blocks = compptr->MCU_width * compptr->MCU_height;
      compptr->MCU_sample_width = compptr->MCU_width * compptr->DCT_h_scaled_size;
      /* Figure number of non-dummy blocks in last MCU column & row */
      tmp = (JINT) (compptr->width_in_blocks % compptr->MCU_width);
      if (tmp == 0) {
          tmp = compptr->MCU_width;
      }
      compptr->last_col_width = tmp;
      tmp = (JINT) (compptr->height_in_blocks % compptr->MCU_height);
      if (tmp == 0) {
          tmp = compptr->MCU_height;
      }
      compptr->last_row_height = tmp;
      /* Prepare array describing MCU composition */
      mcublks = compptr->MCU_blocks;
      JSC_ASSERT_3(cinfo->blocks_in_MCU + mcublks <= D_MAX_BLOCKS_IN_MCU,
              cinfo->blocks_in_MCU, mcublks, D_MAX_BLOCKS_IN_MCU);
      while (mcublks > 0) {
        mcublks--;
        cinfo->MCU_membership[cinfo->blocks_in_MCU] = ci;
        cinfo->blocks_in_MCU++;
      }
    }
    
  }
}


/*
 * Save away a copy of the Q-table referenced by each component present
 * in the current scan, unless already saved during a prior scan.
 *
 * In a multiple-scan JPEG file, the encoder could assign different components
 * the same Q-table slot number, but change table definitions between scans
 * so that each component uses a different Q-table.  (The IJG encoder is not
 * currently capable of doing this, but other encoders might.)  Since we want
 * to be able to dequantize all the components at the end of the file, this
 * means that we have to save away the table actually used for each component.
 * We do this by copying the table at the start of the first scan containing
 * the component.
 * The JPEG spec prohibits the encoder from changing the contents of a Q-table
 * slot between scans of a component using that slot.  If the encoder does so
 * anyway, this decoder will simply use the Q-table values that were current
 * at the start of the first scan for the component.
 *
 * The decompressor output side looks only at the saved quant tables,
 * not at the current Q-table slots.
 */

JSC_LOCAL(void)
latch_quant_tables (j_decompress_ptr cinfo)
{
  JINT ci;
  JINT qtblno;
  jpeg_component_info *compptr;
  JQUANT_TBL * qtbl;

  for (ci = 0; ci < cinfo->comps_in_scan; ci++) {
    compptr = cinfo->cur_comp_info[ci];
    /* No work if we already saved Q-table for this component */
    if (compptr->quant_table != NULL) {
      continue;
    }
    /* Make sure specified quantization table is present */
    qtblno = compptr->quant_tbl_no;
    JSC_ASSERT_1(qtblno >= 0, qtblno);
    JSC_ASSERT_2(qtblno < NUM_QUANT_TBLS, qtblno, NUM_QUANT_TBLS);
    JSC_ASSERT_1(cinfo->quant_tbl_ptrs[qtblno] != NULL, qtblno);

    /* OK, save away the quantization table */
    qtbl = (JQUANT_TBL *)
      (*cinfo->mem->get_mem) ((j_common_ptr) cinfo, JPOOL_IMAGE,
                                  SIZEOF(JQUANT_TBL));
    JSC_MEMCOPY(qtbl, cinfo->quant_tbl_ptrs[qtblno], SIZEOF(JQUANT_TBL));
    compptr->quant_table = qtbl;
  }
}


/*
 * Initialize the input modules to read a scan of compressed data.
 * The first call to this is done by jdmaster.c after initializing
 * the entire decompressor (during jpeg_start_decompress).
 * Subsequent calls come from consume_markers, below.
 */

JSC_METHODDEF(void)
start_input_pass (j_decompress_ptr cinfo)
{
  per_scan_setup(cinfo);
  latch_quant_tables(cinfo);
  (*cinfo->entropy->start_pass) (cinfo);
  (*cinfo->coef->start_input_pass) (cinfo);
  cinfo->inputctl->consume_input = cinfo->coef->consume_data;
}


/*
 * Finish up after inputting a compressed-data scan.
 * This is called by the coefficient controller after it's read all
 * the expected data of the scan.
 */

JSC_METHODDEF(void)
finish_input_pass (j_decompress_ptr cinfo)
{
  (*cinfo->entropy->finish_pass) (cinfo);
  cinfo->inputctl->consume_input = consume_markers;
}


/*
 * Read JPEG markers before, between, or after compressed-data scans.
 * Change state as necessary when a new scan is reached.
 * Return value is JPEG_SUSPENDED, JPEG_REACHED_SOS, or JPEG_REACHED_EOI.
 *
 * The consume_input method pointer points either here or to the
 * coefficient controller's consume_data routine, depending on whether
 * we are reading a compressed data segment or inter-segment markers.
 *
 * Note: This function should NOT return a pseudo SOS marker (with zero
 * component number) to the caller.  A pseudo marker received by
 * read_markers is processed and then skipped for other markers.
 */

JSC_METHODDEF(JINT)
consume_markers (j_decompress_ptr cinfo)
{
  my_input_controller * inputctl = (my_input_controller *) cinfo->inputctl;
  JINT val;

  if (inputctl->pub.eoi_reached) { /* After hitting EOI, read no further */
    return JPEG_REACHED_EOI;
  }

  // Abcouwer JSC 2021 - add loop limit
  JINT i;
  JINT loop_limit = 1000;
  for (i = 0; i < loop_limit; i++) { /* Loop to pass pseudo SOS marker */
    val = (*cinfo->marker->read_markers) (cinfo);

    switch (val) {
    case JPEG_REACHED_SOS:        /* Found SOS */
      if (inputctl->inheaders) { /* 1st SOS */
        if (inputctl->inheaders == 1) {
          initial_setup(cinfo);
        }
        if (cinfo->comps_in_scan == 0) { /* pseudo SOS marker */
          inputctl->inheaders = 2;
          break;
        }
        inputctl->inheaders = 0;
        /* Note: start_input_pass must be called by jdmaster.c
         * before any more input can be consumed.  jdapimin.c is
         * responsible for enforcing this sequencing.
         */
      } else {                        /* 2nd or later SOS marker */
        JSC_ASSERT(inputctl->pub.has_multiple_scans);
        if (cinfo->comps_in_scan == 0) { /* unexpected pseudo SOS marker */
          break;
        }
        start_input_pass(cinfo);
      }
      return val;
    case JPEG_REACHED_EOI:        /* Found EOI */
      inputctl->pub.eoi_reached = TRUE;
      if (inputctl->inheaders) { /* Tables-only datastream, apparently */
          if(cinfo->marker->saw_SOF) {
              JSC_WARN(JERR_SOF_NO_SOS,
                      "Invalid JPEG file structure: missing SOS marker");
              return JPEG_SUSPENDED;
          }
        JSC_ASSERT(!cinfo->marker->saw_SOF);
      } else if (cinfo->output_scan_number > cinfo->input_scan_number) {
          cinfo->output_scan_number = cinfo->input_scan_number;
          /* Prevent infinite loop in coef ctlr's decompress_data routine
           * if user set output_scan_number larger than number of scans.
           */
      } else {
          // do nothing
      }

      return val;
    case JPEG_SUSPENDED:
      return val;
    default:
      return val;
    }
  }
  // should never get here
  JSC_ASSERT_2(0, i, loop_limit);
}


/*
 * Reset state to begin a fresh datastream.
 */

JSC_METHODDEF(void)
reset_input_controller (j_decompress_ptr cinfo)
{
  my_input_controller * inputctl = (my_input_controller *) cinfo->inputctl;

  inputctl->pub.consume_input = consume_markers;
  inputctl->pub.has_multiple_scans = FALSE; /* "unknown" would be better */
  inputctl->pub.eoi_reached = FALSE;
  inputctl->inheaders = 1;
  /* Reset other modules */
  // Abcouwer JSC 2021 - no error mgr
//  (*cinfo->err->reset_error_mgr) ((j_common_ptr) cinfo);
  (*cinfo->marker->reset_marker_reader) (cinfo);
  /* Reset progression state -- would be cleaner if entropy decoder did this */
  cinfo->coef_bits = NULL;
}


/*
 * Initialize the input controller module.
 * This is called only once, when the decompression object is created.
 */

JSC_GLOBAL(void)
jinit_input_controller (j_decompress_ptr cinfo)
{
  my_input_controller * inputctl;

  /* Create subobject in permanent pool */
  inputctl = (my_input_controller *)
    (*cinfo->mem->get_mem) ((j_common_ptr) cinfo, JPOOL_PERMANENT,
                                SIZEOF(my_input_controller));
  cinfo->inputctl = &inputctl->pub;
  /* Initialize method pointers */
  inputctl->pub.consume_input = consume_markers;
  inputctl->pub.reset_input_controller = reset_input_controller;
  inputctl->pub.start_input_pass = start_input_pass;
  inputctl->pub.finish_input_pass = finish_input_pass;
  /* Initialize state: can't use reset_input_controller since we don't
   * want to try to reset other modules yet.
   */
  inputctl->pub.has_multiple_scans = FALSE; /* "unknown" would be better */
  inputctl->pub.eoi_reached = FALSE;
  inputctl->inheaders = 1;
}
