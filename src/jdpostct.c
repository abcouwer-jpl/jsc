/*
 * jdpostct.c
 *
 * Copyright (C) 1994-1996, Thomas G. Lane.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
 *
 * This file contains the decompression postprocessing controller.
 * This controller manages the upsampling, color conversion, and color
 * quantization/reduction steps; specifically, it controls the buffering
 * between upsample/color conversion and color quantization/reduction.
 *
 * If no color quantization/reduction is required, then this module has no
 * work to do, and it just hands off to the upsample/color conversion code.
 * An integrated upsample/convert/quantize process would replace this module
 * entirely.
 */

#include "jsc/jpegint.h"

/* Private buffer controller object */

typedef struct {
  struct jpeg_d_post_controller pub; /* public fields */

  /* Color quantization source buffer: this holds output data from
   * the upsample/color conversion step to be passed to the quantizer.
   * For two-pass color quantization, we need a full-image buffer;
   * for one-pass operation, a strip buffer is sufficient.
   */
  // Abcouwer ZSC 2021 - remove virtual arrays
  JSAMPARRAY buffer;		/* strip buffer, or current strip of virtual */
  JDIMENSION strip_height;	/* buffer size in rows */
  /* for two-pass mode only: */
  JDIMENSION starting_row;	/* row # of first row in current strip */
  JDIMENSION next_row;		/* index of next row to fill/empty in strip */
} my_post_controller;
/*
 * Initialize for a processing pass.
 */

JSC_METHODDEF(void)
start_pass_dpost (j_decompress_ptr cinfo, J_BUF_MODE pass_mode)
{
    my_post_controller * post = (my_post_controller *) cinfo->post;

    JSC_ASSERT_2(pass_mode == JBUF_PASS_THRU, pass_mode, JBUF_PASS_THRU);
    // Abcouwer ZSC 2021 - don't allow requesting of quantized colors
    post->pub.post_process_data = cinfo->upsample->upsample;
    post->starting_row = post->next_row = 0;
}

// Abcouwer JSC 2021 - remove quantization support

/*
 * Initialize postprocessing controller.
 */

JSC_GLOBAL(void)
jinit_d_post_controller (j_decompress_ptr cinfo)
{
  my_post_controller * post;

  post = (my_post_controller *) (*cinfo->mem->get_mem) (
          (j_common_ptr) cinfo, JPOOL_IMAGE, SIZEOF(my_post_controller));
  cinfo->post = (struct jpeg_d_post_controller *) post;
  post->pub.start_pass = start_pass_dpost;
  // Abcouwer ZSC 2021 - remove virtual arrays
  post->buffer = NULL;		/* flag for no strip buffer */

  // Abcouwer ZSC 2021 - don't allow requesting of quantized colors
}
