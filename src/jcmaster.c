/*
 * jcmaster.c
 *
 * Copyright (C) 1991-1997, Thomas G. Lane.
 * Modified 2003-2019 by Guido Vollbeding.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
 *
 * This file contains master control logic for the JPEG compressor.
 * These routines are concerned with parameter validation, initial setup,
 * and inter-pass control (determining the number of passes and the work 
 * to be done in each pass).
 */

#include "jsc/jpegint.h"

/* Private state */

typedef enum {
        main_pass,                /* input data, also do first output step */
        huff_opt_pass,                /* Huffman code optimization pass */
        output_pass                /* data output pass */
} c_pass_type;

typedef struct {
  struct jpeg_comp_master pub;        /* public fields */

  c_pass_type pass_type;        /* the type of the current pass */

  JINT pass_number;                /* # of passes completed */
  JINT total_passes;                /* total # of passes needed */

  JINT scan_number;                /* current index in scan_info[] */
} my_comp_master;

/*
 * Support routines that do various essential calculations.
 */

JSC_LOCAL(void)
initial_setup (j_compress_ptr cinfo)
/* Do computations that are needed before master selection phase */
{
  JINT ci;
  JINT ssize;
  jpeg_component_info *compptr;

  /* Sanity check on block_size */
  JSC_ASSERT_1(cinfo->block_size >= 1, cinfo->block_size);
  JSC_ASSERT_1(cinfo->block_size <= 16, cinfo->block_size);

  /* Derive natural_order from block_size */
  // Abcouwer JSC 2021 - only block size of 8 used
  JSC_ASSERT_2(cinfo->block_size == DCTSIZE,cinfo->block_size, DCTSIZE);
  cinfo->natural_order = jpeg_natural_order;

  /* Derive lim_Se from block_size */
  cinfo->lim_Se = cinfo->block_size < DCTSIZE ?
    cinfo->block_size * cinfo->block_size - 1 : DCTSIZE2-1;

  /* Sanity check on image dimensions */
  JSC_ASSERT_1(cinfo->jpeg_height > 0, cinfo->jpeg_height);
  JSC_ASSERT_1(cinfo->jpeg_width  > 0, cinfo->jpeg_width);
  JSC_ASSERT_1(cinfo->num_components > 0, cinfo->num_components);

  /* Make sure image isn't bigger than I can handle */
  JSC_ASSERT_2(cinfo->jpeg_height <= JPEG_MAX_DIMENSION,
          cinfo->jpeg_height, JPEG_MAX_DIMENSION);
  JSC_ASSERT_2(cinfo->jpeg_width  <= JPEG_MAX_DIMENSION,
          cinfo->jpeg_width, JPEG_MAX_DIMENSION);

  /* Only 8 to 12 bits data precision are supported for DCT based JPEG */
  JSC_ASSERT_1(cinfo->data_precision >= 8, cinfo->data_precision);
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
      JSC_ASSERT_1(compptr->v_samp_factor > 0, compptr->v_samp_factor);
      JSC_ASSERT_2(compptr->h_samp_factor <= MAX_SAMP_FACTOR,
              compptr->h_samp_factor, MAX_SAMP_FACTOR);
      JSC_ASSERT_2(compptr->v_samp_factor <= MAX_SAMP_FACTOR,
              compptr->v_samp_factor, MAX_SAMP_FACTOR);
      cinfo->max_h_samp_factor = JSC_MAX(cinfo->max_h_samp_factor,
                                   compptr->h_samp_factor);
      cinfo->max_v_samp_factor = JSC_MAX(cinfo->max_v_samp_factor,
                                   compptr->v_samp_factor);
  }

  /* Compute dimensions of components */

  for (ci = 0; ci < cinfo->num_components; ci++) {
    compptr = cinfo->comp_info + ci;
    /* Fill in the correct component_index value; don't rely on application */
    compptr->component_index = ci;
    /* In selecting the actual DCT scaling for each component, we try to
     * scale down the chroma components via DCT scaling rather than downsampling.
     * This saves time if the downsampler gets to use 1:1 scaling.
     * Note this code adapts subsampling ratios which are powers of 2.
     */
    ssize = 1;
    // Abcouwer JSC 2021 - no DCT_SCALING
    compptr->DCT_h_scaled_size = cinfo->min_DCT_h_scaled_size * ssize;
    ssize = 1;
    compptr->DCT_v_scaled_size = cinfo->min_DCT_v_scaled_size * ssize;

    /* We don't support DCT ratios larger than 2. */
    if (compptr->DCT_h_scaled_size > compptr->DCT_v_scaled_size * 2) {
        compptr->DCT_h_scaled_size = compptr->DCT_v_scaled_size * 2;
    } else if (compptr->DCT_v_scaled_size > compptr->DCT_h_scaled_size * 2) {
        compptr->DCT_v_scaled_size = compptr->DCT_h_scaled_size * 2;
    } else {
        // ratio is fine
    }

    /* Size in DCT blocks */
    compptr->width_in_blocks = (JDIMENSION)
      jdiv_round_up((JLONG) cinfo->jpeg_width * (JLONG) compptr->h_samp_factor,
                    (JLONG) (cinfo->max_h_samp_factor * cinfo->block_size));
    compptr->height_in_blocks = (JDIMENSION)
      jdiv_round_up((JLONG) cinfo->jpeg_height * (JLONG) compptr->v_samp_factor,
                    (JLONG) (cinfo->max_v_samp_factor * cinfo->block_size));
    /* Size in samples */
    compptr->downsampled_width = (JDIMENSION)
      jdiv_round_up((JLONG) cinfo->jpeg_width *
                    (JLONG) (compptr->h_samp_factor * compptr->DCT_h_scaled_size),
                    (JLONG) (cinfo->max_h_samp_factor * cinfo->block_size));
    compptr->downsampled_height = (JDIMENSION)
      jdiv_round_up((JLONG) cinfo->jpeg_height *
                    (JLONG) (compptr->v_samp_factor * compptr->DCT_v_scaled_size),
                    (JLONG) (cinfo->max_v_samp_factor * cinfo->block_size));
    /* Don't need quantization scale after DCT,
     * until color conversion says otherwise.
     */
    compptr->component_needed = FALSE;
  }

  /* Compute number of fully interleaved MCU rows (number of times that
   * main controller will call coefficient controller).
   */
  cinfo->total_iMCU_rows = (JDIMENSION)
    jdiv_round_up((JLONG) cinfo->jpeg_height,
                  (JLONG) (cinfo->max_v_samp_factor * cinfo->block_size));
}

// Abcouwer JSC 2021 - no C_MULTISCAN_FILES_SUPPORTED

JSC_LOCAL(void)
select_scan_parameters (j_compress_ptr cinfo)
/* Set up the scan parameters for the current scan */
{
  JINT ci;

  // Abcouwer JSC 2021 - no C_MULTISCAN_FILES_SUPPORTED

  /* Prepare for single sequential-JPEG scan containing all components */
  JSC_ASSERT_2(cinfo->num_components <= MAX_COMPS_IN_SCAN,
          cinfo->num_components, MAX_COMPS_IN_SCAN);
  cinfo->comps_in_scan = cinfo->num_components;
  for (ci = 0; ci < cinfo->num_components; ci++) {
    cinfo->cur_comp_info[ci] = &cinfo->comp_info[ci];
  }
  cinfo->Ss = 0;
  cinfo->Se = cinfo->block_size * cinfo->block_size - 1;
  cinfo->Ah = 0;
  cinfo->Al = 0;
}


JSC_LOCAL(void)
per_scan_setup (j_compress_ptr cinfo)
/* Do computations that are needed before processing a JPEG scan */
/* cinfo->comps_in_scan and cinfo->cur_comp_info[] are already set */
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
    JSC_ASSERT_1(cinfo->comps_in_scan > 0,cinfo->comps_in_scan);
    JSC_ASSERT_2(cinfo->comps_in_scan <= MAX_COMPS_IN_SCAN,
            cinfo->comps_in_scan, MAX_COMPS_IN_SCAN);

    /* Overall image size in MCUs */
    cinfo->MCUs_per_row = (JDIMENSION)
      jdiv_round_up((JLONG) cinfo->jpeg_width,
                    (JLONG) (cinfo->max_h_samp_factor * cinfo->block_size));
    cinfo->MCU_rows_in_scan = (JDIMENSION)
      jdiv_round_up((JLONG) cinfo->jpeg_height,
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
      JSC_ASSERT_3(cinfo->blocks_in_MCU + mcublks <= C_MAX_BLOCKS_IN_MCU,
              cinfo->blocks_in_MCU, mcublks, C_MAX_BLOCKS_IN_MCU);
      while (mcublks > 0) {
        mcublks--;
        cinfo->MCU_membership[cinfo->blocks_in_MCU++] = ci;
      }
    }

  }

  /* Convert restart specified in rows to actual MCU count. */
  /* Note that count must fit in 16 bits, so we provide limiting. */
  if (cinfo->restart_in_rows > 0) {
    JLONG nominal = (JLONG) cinfo->restart_in_rows * (JLONG) cinfo->MCUs_per_row;
    cinfo->restart_interval = (JUINT) JSC_MIN(nominal, 65535L);
  }
}


/*
 * Per-pass setup.
 * This is called at the beginning of each pass.  We determine which modules
 * will be active during this pass and give them appropriate start_pass calls.
 * We also set is_last_pass to indicate whether any more passes will be
 * required.
 */

JSC_METHODDEF(void)
prepare_for_pass_std (j_compress_ptr cinfo)
{
    JSC_ASSERT(cinfo != NULL);
    my_comp_master * master = (my_comp_master *) cinfo->master;
    JSC_ASSERT(master != NULL);
    // Abcouwer JSC 2021 - no optimization or output pass - only single pass
    JSC_ASSERT_2(master->pass_type == main_pass, master->pass_type, main_pass);
    /* Initial pass: will collect input data, and do either Huffman
     * optimization or data output for the first scan.
     */
    select_scan_parameters(cinfo);
    per_scan_setup(cinfo);
    // Abcouwer JSC 2021 - remove raw data in
    (*cinfo->cconvert->start_pass)(cinfo);
    (*cinfo->downsample->start_pass)(cinfo);
    (*cinfo->prep->start_pass)(cinfo, JBUF_PASS_THRU);
    (*cinfo->fdct->start_pass)(cinfo);
    // Abcouwer JSC 2021 - remove optimized coding
    (*cinfo->entropy->start_pass)(cinfo, FALSE/*cinfo->optimize_coding*/);
    (*cinfo->coef->start_pass)(cinfo,
            (master->total_passes > 1 ? JBUF_SAVE_AND_PASS : JBUF_PASS_THRU));
    (*cinfo->main->start_pass)(cinfo, JBUF_PASS_THRU);
    // Abcouwer JSC 2021 - remove optimized coding
    /* Will write frame/scan headers at first jpeg_write_scanlines call */
    master->pub.call_pass_startup = TRUE;

    master->pub.is_last_pass =
            (master->pass_number == master->total_passes - 1);
    // Abcouwer JSC 2021 - assert 1 / last pass
    JSC_ASSERT(master->pub.is_last_pass);

    /* Set up progress monitor's pass info if present */
    if (cinfo->progress != NULL) {
        cinfo->progress->completed_passes = master->pass_number;
        cinfo->progress->total_passes = master->total_passes;
    }
}


/*
 * Special start-of-pass hook.
 * This is called by jpeg_write_scanlines if call_pass_startup is TRUE.
 * In single-pass processing, we need this hook because we don't want to
 * write frame/scan headers during jpeg_start_compress; we want to let the
 * application write COM markers etc. between jpeg_start_compress and the
 * jpeg_write_scanlines loop.
 * In multi-pass processing, this routine is not used.
 */

JSC_METHODDEF(void)
pass_startup_std (j_compress_ptr cinfo)
{
  JSC_ASSERT(cinfo != NULL);
  JSC_ASSERT(cinfo->master != NULL);
  cinfo->master->call_pass_startup = FALSE; /* reset flag so call only once */

  JSC_ASSERT(cinfo->marker != NULL);
  (*cinfo->marker->write_frame_header) (cinfo);
  (*cinfo->marker->write_scan_header) (cinfo);
}


/*
 * Finish up at end of pass.
 */

JSC_METHODDEF(void)
finish_pass_master_std (j_compress_ptr cinfo)
{
    JSC_ASSERT(cinfo != NULL);
    my_comp_master * master = (my_comp_master *) cinfo->master;

    /* The entropy coder always needs an end-of-pass call,
     * either to analyze statistics or to flush its output buffer.
     */
    JSC_ASSERT(cinfo->entropy != NULL);
    (*cinfo->entropy->finish_pass)(cinfo);

    /* Update state for next pass */
    // Abcouwer JSC 2021 - no optimization or multi-pass
    JSC_ASSERT(master != NULL);
    JSC_ASSERT_2(master->pass_type == main_pass, master->pass_type, main_pass);
    master->pass_type = output_pass;
    // Abcouwer JSC 2021 - remove optimized coding
    master->scan_number++;
    master->pass_number++;
}


/*
 * Initialize master compression control.
 */

JSC_GLOBAL(void)
jinit_c_master_control (j_compress_ptr cinfo, boolean transcode_only)
{
    JSC_ASSERT(cinfo != NULL);
    my_comp_master * master;

    JSC_ASSERT(cinfo->mem != NULL);
    master = (my_comp_master *) (*cinfo->mem->get_mem)((j_common_ptr) cinfo,
            JPOOL_IMAGE, SIZEOF(my_comp_master));
    cinfo->master = &master->pub;
    master->pub.prepare_for_pass = prepare_for_pass_std;
    master->pub.pass_startup = pass_startup_std;
    master->pub.finish_pass = finish_pass_master_std;
    master->pub.is_last_pass = FALSE;

    /* Validate parameters, determine derived values */
    initial_setup(cinfo);

    // Abcouwer JSC 2021 - no C_MULTISCAN_FILES_SUPPORTED
    JSC_ASSERT(cinfo->scan_info == NULL);

    // Abcouwer JSC 2021 - remove progressive encoding
    cinfo->num_scans = 1;

    // Abcouwer JSC 2021 - remove arithmetic encoding, optimized, progressive

    /* Initialize my private state */
    if (transcode_only) {
        /* no main pass in transcoding */
        // Abcouwer JSC 2021 - remove optimized coding
        master->pass_type = output_pass;
    } else {
        /* for normal compression, first pass is always this type: */
        master->pass_type = main_pass;
    }
    master->scan_number = 0;
    master->pass_number = 0;
    // Abcouwer JSC 2021 - remove optimized coding
    master->total_passes = cinfo->num_scans;
}
