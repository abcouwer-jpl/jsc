/*
 * jdmainct.c
 *
 * Copyright (C) 1994-1996, Thomas G. Lane.
 * Modified 2002-2016 by Guido Vollbeding.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
 *
 * This file contains the main buffer controller for decompression.
 * The main buffer lies between the JPEG decompressor proper and the
 * post-processor; it holds downsampled data in the JPEG colorspace.
 *
 * Note that this code is bypassed in raw-data mode, since the application
 * supplies the equivalent of the main buffer in that case.
 */

#include "jsc/jpegint.h"

/*
 * In the current system design, the main buffer need never be a full-image
 * buffer; any full-height buffers will be found inside the coefficient or
 * postprocessing controllers.  Nonetheless, the main controller is not
 * trivial.  Its responsibility is to provide context rows for upsampling/
 * rescaling, and doing this in an efficient fashion is a bit tricky.
 *
 * Postprocessor input data is counted in "row groups".  A row group is
 * defined to be (v_samp_factor * DCT_v_scaled_size / min_DCT_v_scaled_size)
 * sample rows of each component.  (We require DCT_scaled_size values to be
 * chosen such that these numbers are integers.  In practice DCT_scaled_size
 * values will likely be powers of two, so we actually have the stronger
 * condition that DCT_scaled_size / min_DCT_scaled_size is an integer.)
 * Upsampling will typically produce max_v_samp_factor pixel rows from each
 * row group (times any additional scale factor that the upsampler is
 * applying).
 *
 * The coefficient controller will deliver data to us one iMCU row at a time;
 * each iMCU row contains v_samp_factor * DCT_v_scaled_size sample rows, or
 * exactly min_DCT_v_scaled_size row groups.  (This amount of data corresponds
 * to one row of MCUs when the image is fully interleaved.)  Note that the
 * number of sample rows varies across components, but the number of row
 * groups does not.  Some garbage sample rows may be included in the last iMCU
 * row at the bottom of the image.
 *
 * Depending on the vertical scaling algorithm used, the upsampler may need
 * access to the sample row(s) above and below its current input row group.
 * The upsampler is required to set need_context_rows TRUE at global selection
 * time if so.  When need_context_rows is FALSE, this controller can simply
 * obtain one iMCU row at a time from the coefficient controller and dole it
 * out as row groups to the postprocessor.
 *
 * When need_context_rows is TRUE, this controller guarantees that the buffer
 * passed to postprocessing contains at least one row group's worth of samples
 * above and below the row group(s) being processed.  Note that the context
 * rows "above" the first passed row group appear at negative row offsets in
 * the passed buffer.  At the top and bottom of the image, the required
 * context rows are manufactured by duplicating the first or last real sample
 * row; this avoids having special cases in the upsampling inner loops.
 *
 * The amount of context is fixed at one row group just because that's a
 * convenient number for this controller to work with.  The existing
 * upsamplers really only need one sample row of context.  An upsampler
 * supporting arbitrary output rescaling might wish for more than one row
 * group of context when shrinking the image; tough, we don't handle that.
 * (This is justified by the assumption that downsizing will be handled mostly
 * by adjusting the DCT_scaled_size values, so that the actual scale factor at
 * the upsample step needn't be much less than one.)
 *
 * To provide the desired context, we have to retain the last two row groups
 * of one iMCU row while reading in the next iMCU row.  (The last row group
 * can't be processed until we have another row group for its below-context,
 * and so we have to save the next-to-last group too for its above-context.)
 * We could do this most simply by copying data around in our buffer, but
 * that'd be very slow.  We can avoid copying any data by creating a rather
 * strange pointer structure.  Here's how it works.  We allocate a workspace
 * consisting of M+2 row groups (where M = min_DCT_v_scaled_size is the number
 * of row groups per iMCU row).  We create two sets of redundant pointers to
 * the workspace.  Labeling the physical row groups 0 to M+1, the synthesized
 * pointer lists look like this:
 *                   M+1                          M-1
 * master pointer --> 0         master pointer --> 0
 *                    1                            1
 *                   ...                          ...
 *                   M-3                          M-3
 *                   M-2                           M
 *                   M-1                          M+1
 *                    M                           M-2
 *                   M+1                          M-1
 *                    0                            0
 * We read alternate iMCU rows using each master pointer; thus the last two
 * row groups of the previous iMCU row remain un-overwritten in the workspace.
 * The pointer lists are set up so that the required context rows appear to
 * be adjacent to the proper places when we pass the pointer lists to the
 * upsampler.
 *
 * The above pictures describe the normal state of the pointer lists.
 * At top and bottom of the image, we diddle the pointer lists to duplicate
 * the first or last sample row as necessary (this is cheaper than copying
 * sample rows around).
 *
 * This scheme breaks down if M < 2, ie, min_DCT_v_scaled_size is 1.  In that
 * situation each iMCU row provides only one row group so the buffering logic
 * must be different (eg, we must read two iMCU rows before we can emit the
 * first row group).  For now, we simply do not support providing context
 * rows when min_DCT_v_scaled_size is 1.  That combination seems unlikely to
 * be worth providing --- if someone wants a 1/8th-size preview, they probably
 * want it quick and dirty, so a context-free upsampler is sufficient.
 */


/* Private buffer controller object */

typedef struct {
  struct jpeg_d_main_controller pub; /* public fields */

  /* Pointer to allocated workspace (M or M+2 row groups). */
  JSAMPARRAY buffer[MAX_COMPONENTS];

  JDIMENSION rowgroup_ctr;	/* counts row groups output to postprocessor */
  JDIMENSION rowgroups_avail;	/* row groups available to postprocessor */

  /* Remaining fields are only used in the context case. */

  boolean buffer_full;		/* Have we gotten an iMCU row from decoder? */

  /* These are the master pointers to the funny-order pointer lists. */
  JSAMPIMAGE xbuffer[2];	/* pointers to weird pointer lists */

  JINT whichptr;			/* indicates which pointer set is now in use */
  JINT context_state;		/* process_data state machine status */
  JDIMENSION iMCU_row_ctr;	/* counts iMCU rows to detect image top/bot */
} my_main_controller;

typedef my_main_controller * my_main_ptr;

/* context_state values: */
#define CTX_PREPARE_FOR_IMCU	0	/* need to prepare for MCU row */
#define CTX_PROCESS_IMCU	1	/* feeding iMCU to postprocessor */
#define CTX_POSTPONED_ROW	2	/* feeding postponed row group */


/* Forward declarations */
JSC_METHODDEF(void) process_data_simple_main
	JPP((j_decompress_ptr cinfo, JSAMPARRAY output_buf,
	     JDIMENSION *out_row_ctr, JDIMENSION out_rows_avail));

// Abcouwer JSC 2021 - don't support context rows

/*
 * Initialize for a processing pass.
 */

JSC_METHODDEF(void)
start_pass_main (j_decompress_ptr cinfo, J_BUF_MODE pass_mode)
{
    JSC_ASSERT(cinfo != NULL);
    // Abcouwer JSC 2021 - no 2pass support
    JSC_ASSERT_2(pass_mode == JBUF_PASS_THRU, pass_mode, JBUF_PASS_THRU);
    // Abcouwer JSC 2021 - don't support context rows
    JSC_ASSERT(!cinfo->upsample->need_context_rows);

    my_main_ptr mainp = (my_main_ptr) cinfo->main;
    /* Simple case with no context needed */
    mainp->pub.process_data = process_data_simple_main;
    mainp->rowgroup_ctr = mainp->rowgroups_avail; /* Mark buffer empty */
}


/*
 * Process some data.
 * This handles the simple case where no context is required.
 */

JSC_METHODDEF(void)
process_data_simple_main (j_decompress_ptr cinfo,
			  JSAMPARRAY output_buf, JDIMENSION *out_row_ctr,
			  JDIMENSION out_rows_avail)
{
  my_main_ptr mainp = (my_main_ptr) cinfo->main;

  /* Read input data if we haven't filled the main buffer yet */
  if (mainp->rowgroup_ctr >= mainp->rowgroups_avail) {
    if (! (*cinfo->coef->decompress_data) (cinfo, mainp->buffer)) {
      return;			/* suspension forced, can do nothing more */
    }
    mainp->rowgroup_ctr = 0;	/* OK, we have an iMCU row to work with */
  }

  /* Note: at the bottom of the image, we may pass extra garbage row groups
   * to the postprocessor.  The postprocessor has to check for bottom
   * of image anyway (at row resolution), so no point in us doing it too.
   */

  /* Feed the postprocessor */
  (*cinfo->post->post_process_data) (cinfo, mainp->buffer,
			&mainp->rowgroup_ctr, mainp->rowgroups_avail,
			output_buf, out_row_ctr, out_rows_avail);
}

// Abcouwer JSC 2021 - no 2pass support

/*
 * Initialize main buffer controller.
 */

JSC_GLOBAL(void)
jinit_d_main_controller (j_decompress_ptr cinfo, boolean need_full_buffer)
{
  my_main_ptr mainp;
  JINT ci;
  JINT rgroup;
  JINT ngroups;
  jpeg_component_info *compptr;

  mainp = (my_main_ptr)
    (*cinfo->mem->get_mem) ((j_common_ptr) cinfo, JPOOL_IMAGE,
				SIZEOF(my_main_controller));
  cinfo->main = &mainp->pub;
  mainp->pub.start_pass = start_pass_main;

  JSC_ASSERT(!need_full_buffer);

  /* Allocate the workspace.
   * ngroups is the number of row groups we need.
   */
  // Abcouwer JSC 2021 - don't support context rows
  JSC_ASSERT(!cinfo->upsample->need_context_rows);

  /* There are always min_DCT_v_scaled_size row groups in an iMCU row. */
  ngroups = cinfo->min_DCT_v_scaled_size;
  mainp->rowgroups_avail = (JDIMENSION) ngroups;

  for (ci = 0; ci < cinfo->num_components; ci++) {
    compptr = cinfo->comp_info + ci;
    rgroup = (compptr->v_samp_factor * compptr->DCT_v_scaled_size) /
      cinfo->min_DCT_v_scaled_size; /* height of a row group of component */
    mainp->buffer[ci] = (*cinfo->mem->get_sarray)
      ((j_common_ptr) cinfo, JPOOL_IMAGE,
       compptr->width_in_blocks * ((JDIMENSION) compptr->DCT_h_scaled_size),
       (JDIMENSION) (rgroup * ngroups));
  }
}
