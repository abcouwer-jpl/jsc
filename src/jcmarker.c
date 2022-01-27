/*
 * jcmarker.c
 *
 * Copyright (C) 1991-1998, Thomas G. Lane.
 * Modified 2003-2019 by Guido Vollbeding.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
 *
 * This file contains routines to write JPEG datastream markers.
 */

#include "jsc/jpegint.h"


typedef enum {                        /* JPEG marker codes */
  M_SOF0  = 0xc0,
  M_SOF1  = 0xc1,
  M_SOF2  = 0xc2,
  M_SOF3  = 0xc3,

  M_SOF5  = 0xc5,
  M_SOF6  = 0xc6,
  M_SOF7  = 0xc7,

  M_JPG   = 0xc8,
  M_SOF9  = 0xc9,
  M_SOF10 = 0xca,
  M_SOF11 = 0xcb,

  M_SOF13 = 0xcd,
  M_SOF14 = 0xce,
  M_SOF15 = 0xcf,

  M_DHT   = 0xc4,

  M_DAC   = 0xcc,

  M_RST0  = 0xd0,
  M_RST1  = 0xd1,
  M_RST2  = 0xd2,
  M_RST3  = 0xd3,
  M_RST4  = 0xd4,
  M_RST5  = 0xd5,
  M_RST6  = 0xd6,
  M_RST7  = 0xd7,

  M_SOI   = 0xd8,
  M_EOI   = 0xd9,
  M_SOS   = 0xda,
  M_DQT   = 0xdb,
  M_DNL   = 0xdc,
  M_DRI   = 0xdd,
  M_DHP   = 0xde,
  M_EXP   = 0xdf,

  M_APP0  = 0xe0,
  M_APP1  = 0xe1,
  M_APP2  = 0xe2,
  M_APP3  = 0xe3,
  M_APP4  = 0xe4,
  M_APP5  = 0xe5,
  M_APP6  = 0xe6,
  M_APP7  = 0xe7,
  M_APP8  = 0xe8,
  M_APP9  = 0xe9,
  M_APP10 = 0xea,
  M_APP11 = 0xeb,
  M_APP12 = 0xec,
  M_APP13 = 0xed,
  M_APP14 = 0xee,
  M_APP15 = 0xef,

  M_JPG0  = 0xf0,
  M_JPG8  = 0xf8,
  M_JPG13 = 0xfd,
  M_COM   = 0xfe,

  M_TEM   = 0x01,

  M_ERROR = 0x100
} JPEG_MARKER;


/* Private state */

typedef struct {
  struct jpeg_marker_writer pub; /* public fields */

  JUINT last_restart_interval; /* last DRI value emitted; 0 after SOI */
} my_marker_writer;


/*
 * Basic output routines.
 *
 * Note that we do not support suspension while writing a marker.
 * Therefore, an application using suspension must ensure that there is
 * enough buffer space for the initial markers (typ. 600-700 bytes) before
 * calling jpeg_start_compress, and enough space to write the trailing EOI
 * (a few bytes) before calling jpeg_finish_compress.  Multipass compression
 * modes are not supported at all with suspension, so those two are the only
 * points where markers will be written.
 */

JSC_LOCAL(void)
emit_byte (j_compress_ptr cinfo, JINT val)
/* Emit a byte */
{
  // omit checking parameters for performance

  struct jpeg_destination_mgr * dest = cinfo->dest;

  *(dest->next_output_byte) = (JOCTET) val;
  dest->next_output_byte++;
  dest->free_in_buffer--;
  if (dest->free_in_buffer == 0) {
    boolean emptied = (*dest->empty_output_buffer) (cinfo);
    JSC_ASSERT(emptied);
  }
}


JSC_LOCAL(void)
emit_marker (j_compress_ptr cinfo, JPEG_MARKER mark)
/* Emit a marker code */
{
  emit_byte(cinfo, 0xFF);
  emit_byte(cinfo, (JINT) mark);
}


JSC_LOCAL(void)
emit_2bytes (j_compress_ptr cinfo, JINT value)
/* Emit a 2-byte integer; these are always MSB first in JPEG files */
{
  emit_byte(cinfo, (value >> 8) & 0xFF);
  emit_byte(cinfo, value & 0xFF);
}


/*
 * Routines to write specific marker types.
 */

JSC_LOCAL(JINT)
emit_dqt (j_compress_ptr cinfo, JINT index)
/* Emit a DQT marker */
/* Returns the precision used (0 = 8bits, 1 = 16bits) for baseline checking */
{
  JSC_ASSERT(cinfo != NULL);
  JSC_ASSERT_1(0 <= index, index);
  JSC_ASSERT_2(index < JSC_NUM_ARRAY_ELEMENTS(cinfo->quant_tbl_ptrs),
            index, JSC_NUM_ARRAY_ELEMENTS(cinfo->quant_tbl_ptrs));

  JQUANT_TBL * qtbl = cinfo->quant_tbl_ptrs[index];
  JINT prec;
  JINT i;

  JSC_ASSERT(qtbl != NULL);

  prec = 0;
  for (i = 0; i <= cinfo->lim_Se; i++) {
    if (qtbl->quantval[cinfo->natural_order[i]] > 255)
      prec = 1;
  }

  if (! qtbl->sent_table) {
    emit_marker(cinfo, M_DQT);

    emit_2bytes(cinfo,
      prec ? cinfo->lim_Se * 2 + 2 + 1 + 2 : cinfo->lim_Se + 1 + 1 + 2);

    emit_byte(cinfo, index + (prec<<4));

    for (i = 0; i <= cinfo->lim_Se; i++) {
      /* The table entries must be emitted in zigzag order. */
      JUINT qval = qtbl->quantval[cinfo->natural_order[i]];
      if (prec) {
        emit_byte(cinfo, (JINT) (qval >> 8));
      }
      emit_byte(cinfo, (JINT) (qval & 0xFF));
    }

    qtbl->sent_table = TRUE;
  }

  return prec;
}


JSC_LOCAL(void)
emit_dht (j_compress_ptr cinfo, JINT index, boolean is_ac)
/* Emit a DHT marker */
{
  JHUFF_TBL * htbl;
  JINT length;
  JINT i;
  
  if (is_ac) {
    htbl = cinfo->ac_huff_tbl_ptrs[index];
    index += 0x10;                /* output index has AC bit set */
  } else {
    htbl = cinfo->dc_huff_tbl_ptrs[index];
  }

  JSC_ASSERT(htbl != NULL);
  
  if (! htbl->sent_table) {
    emit_marker(cinfo, M_DHT);
    
    length = 0;
    for (i = 1; i <= 16; i++) {
      length += htbl->bits[i];
    }
    
    emit_2bytes(cinfo, length + 2 + 1 + 16);
    emit_byte(cinfo, index);
    
    for (i = 1; i <= 16; i++) {
      emit_byte(cinfo, htbl->bits[i]);
    }
    
    for (i = 0; i < length; i++) {
      emit_byte(cinfo, htbl->huffval[i]);
    }
    
    htbl->sent_table = TRUE;
  }
}

// Abcouwer JSC 2021 - remove arithmetic encoding

JSC_LOCAL(void)
emit_dri (j_compress_ptr cinfo)
/* Emit a DRI marker */
{
  emit_marker(cinfo, M_DRI);
  
  emit_2bytes(cinfo, 4);        /* fixed length */

  emit_2bytes(cinfo, (JINT) cinfo->restart_interval);
}

// Abcouwer JSC 2021 - remove support for subtract green / color transform,
// no lse marker

JSC_LOCAL(void)
emit_sof (j_compress_ptr cinfo, JPEG_MARKER code)
/* Emit a SOF marker */
{
  JINT ci;
  jpeg_component_info *compptr;
  
  emit_marker(cinfo, code);
  
  emit_2bytes(cinfo, 3 * cinfo->num_components + 2 + 5 + 1); /* length */

  /* Make sure image isn't bigger than SOF field can handle */
  JSC_ASSERT_2((JLONG) cinfo->jpeg_height <= 65535L, cinfo->jpeg_height, 65535);
  JSC_ASSERT_2((JLONG) cinfo->jpeg_width <= 65535L, cinfo->jpeg_width, 65535);

  emit_byte(cinfo, cinfo->data_precision);
  emit_2bytes(cinfo, (JINT) cinfo->jpeg_height);
  emit_2bytes(cinfo, (JINT) cinfo->jpeg_width);

  emit_byte(cinfo, cinfo->num_components);

  for (ci = 0; ci < cinfo->num_components; ci++) {
    compptr = cinfo->comp_info + ci;
    emit_byte(cinfo, compptr->component_id);
    emit_byte(cinfo, (compptr->h_samp_factor << 4) + compptr->v_samp_factor);
    emit_byte(cinfo, compptr->quant_tbl_no);
  }
}


JSC_LOCAL(void)
emit_sos (j_compress_ptr cinfo)
/* Emit a SOS marker */
{
  JINT i;
  JINT td;
  JINT ta;
  jpeg_component_info *compptr;
  
  emit_marker(cinfo, M_SOS);
  
  emit_2bytes(cinfo, 2 * cinfo->comps_in_scan + 2 + 1 + 3); /* length */
  
  emit_byte(cinfo, cinfo->comps_in_scan);
  
  for (i = 0; i < cinfo->comps_in_scan; i++) {
    compptr = cinfo->cur_comp_info[i];
    emit_byte(cinfo, compptr->component_id);

    /* We emit 0 for unused field(s); this is recommended by the P&M text
     * but does not seem to be specified in the standard.
     */

    /* DC needs no table for refinement scan */
    td = cinfo->Ss == 0 && cinfo->Ah == 0 ? compptr->dc_tbl_no : 0;
    /* AC needs no table when not present */
    ta = cinfo->Se ? compptr->ac_tbl_no : 0;

    emit_byte(cinfo, (td << 4) + ta);
  }

  emit_byte(cinfo, cinfo->Ss);
  emit_byte(cinfo, cinfo->Se);
  emit_byte(cinfo, (cinfo->Ah << 4) + cinfo->Al);
}

// Abcouwer JSC 2021 - remove progressive encoding

JSC_LOCAL(void)
emit_jfif_app0 (j_compress_ptr cinfo)
/* Emit a JFIF-compliant APP0 marker */
{
  /*
   * Length of APP0 block        (2 bytes)
   * Block ID                        (4 bytes - ASCII "JFIF")
   * Zero byte                        (1 byte to terminate the ID string)
   * Version Major, Minor        (2 bytes - major first)
   * Units                        (1 byte - 0x00 = none, 0x01 = inch, 0x02 = cm)
   * Xdpu                        (2 bytes - dots per unit horizontal)
   * Ydpu                        (2 bytes - dots per unit vertical)
   * Thumbnail X size                (1 byte)
   * Thumbnail Y size                (1 byte)
   */
  
  emit_marker(cinfo, M_APP0);
  
  emit_2bytes(cinfo, 2 + 4 + 1 + 2 + 1 + 2 + 2 + 1 + 1); /* length */

  emit_byte(cinfo, 0x4A);        /* Identifier: ASCII "JFIF" */
  emit_byte(cinfo, 0x46);
  emit_byte(cinfo, 0x49);
  emit_byte(cinfo, 0x46);
  emit_byte(cinfo, 0);
  emit_byte(cinfo, cinfo->JFIF_major_version); /* Version fields */
  emit_byte(cinfo, cinfo->JFIF_minor_version);
  emit_byte(cinfo, cinfo->density_unit); /* Pixel size information */
  emit_2bytes(cinfo, (JINT) cinfo->X_density);
  emit_2bytes(cinfo, (JINT) cinfo->Y_density);
  emit_byte(cinfo, 0);                /* No thumbnail image */
  emit_byte(cinfo, 0);
}


JSC_LOCAL(void)
emit_adobe_app14 (j_compress_ptr cinfo)
/* Emit an Adobe APP14 marker */
{
  /*
   * Length of APP14 block        (2 bytes)
   * Block ID                        (5 bytes - ASCII "Adobe")
   * Version Number                (2 bytes - currently 100)
   * Flags0                        (2 bytes - currently 0)
   * Flags1                        (2 bytes - currently 0)
   * Color transform                (1 byte)
   *
   * Although Adobe TN 5116 mentions Version = 101, all the Adobe files
   * now in circulation seem to use Version = 100, so that's what we write.
   *
   * We write the color transform byte as 1 if the JPEG color space is
   * YCbCr, 2 if it's YCCK, 0 otherwise.  Adobe's definition has to do with
   * whether the encoder performed a transformation, which is pretty useless.
   */
  
  emit_marker(cinfo, M_APP14);
  
  emit_2bytes(cinfo, 2 + 5 + 2 + 2 + 2 + 1); /* length */

  emit_byte(cinfo, 0x41);        /* Identifier: ASCII "Adobe" */
  emit_byte(cinfo, 0x64);
  emit_byte(cinfo, 0x6F);
  emit_byte(cinfo, 0x62);
  emit_byte(cinfo, 0x65);
  emit_2bytes(cinfo, 100);        /* Version */
  emit_2bytes(cinfo, 0);        /* Flags0 */
  emit_2bytes(cinfo, 0);        /* Flags1 */
  switch (cinfo->jpeg_color_space) {
  case JCS_YCbCr:
    emit_byte(cinfo, 1);        /* Color transform = 1 */
    break;
  case JCS_YCCK:
    emit_byte(cinfo, 2);        /* Color transform = 2 */
    break;
  default:
    emit_byte(cinfo, 0);        /* Color transform = 0 */
  }
}


/*
 * These routines allow writing an arbitrary marker with parameters.
 * The only intended use is to emit COM or APPn markers after calling
 * write_file_header and before calling write_frame_header.
 * Other uses are not guaranteed to produce desirable results.
 * Counting the parameter bytes properly is the caller's responsibility.
 */

JSC_METHODDEF(void)
write_marker_header (j_compress_ptr cinfo, JINT marker, JUINT datalen)
/* Emit an arbitrary marker header */
{
  JSC_ASSERT_2(datalen <= (JUINT) 65533, datalen, 65533);

  emit_marker(cinfo, (JPEG_MARKER) marker);

  emit_2bytes(cinfo, (JINT) (datalen + 2));        /* total length */
}

JSC_METHODDEF(void)
write_marker_byte (j_compress_ptr cinfo, JINT val)
/* Emit one byte of marker parameters following write_marker_header */
{
  emit_byte(cinfo, val);
}


/*
 * Write datastream header.
 * This consists of an SOI and optional APPn markers.
 * We recommend use of the JFIF marker, but not the Adobe marker,
 * when using YCbCr or grayscale data.  The JFIF marker is also used
 * for other standard JPEG colorspaces.  The Adobe marker is helpful
 * to distinguish RGB, CMYK, and YCCK colorspaces.
 * Note that an application can write additional header markers after
 * jpeg_start_compress returns.
 */

JSC_METHODDEF(void)
write_file_header (j_compress_ptr cinfo)
{
  JSC_ASSERT(cinfo != NULL);
  my_marker_writer * marker = (my_marker_writer *) cinfo->marker;

  emit_marker(cinfo, M_SOI);        /* first the SOI */

  /* SOI is defined to reset restart interval to 0 */
  JSC_ASSERT(marker != NULL);
  marker->last_restart_interval = 0;

  if (cinfo->write_JFIF_header)        /* next an optional JFIF APP0 */
    emit_jfif_app0(cinfo);
  if (cinfo->write_Adobe_marker) /* next an optional Adobe APP14 */
    emit_adobe_app14(cinfo);
}


/*
 * Write frame header.
 * This consists of DQT and SOFn markers,
 * a conditional LSE marker and a conditional pseudo SOS marker.
 * Note that we do not emit the SOF until we have emitted the DQT(s).
 * This avoids compatibility problems with incorrect implementations that
 * try to error-check the quant table numbers as soon as they see the SOF.
 */

JSC_METHODDEF(void)
write_frame_header (j_compress_ptr cinfo)
{
    JSC_ASSERT(cinfo != NULL);

    JINT ci;
    JINT prec;
    jpeg_component_info *compptr;

    /* Emit DQT for each quantization table.
     * Note that emit_dqt() suppresses any duplicate tables.
     */
    prec = 0;
    JSC_ASSERT(cinfo->comp_info != NULL);
    for (ci = 0; ci < cinfo->num_components; ci++) {
        compptr = cinfo->comp_info + ci;
        prec += emit_dqt(cinfo, compptr->quant_tbl_no);
    }
    /* now prec is nonzero iff there are any 16-bit quant tables. */
    JSC_ASSERT_1(prec == 0, prec);

    /* Check for a non-baseline specification.
     * Note we assume that Huffman table numbers won't be changed later.
     */
    // Abcouwer JSC 2021 - should always be baseline
    JSC_ASSERT_1(cinfo->data_precision == 8, cinfo->data_precision);
    JSC_ASSERT_2(cinfo->block_size == DCTSIZE, cinfo->block_size, DCTSIZE);
    JSC_ASSERT(compptr != NULL);
    for (ci = 0; ci < cinfo->num_components; ci++) {
        compptr = cinfo->comp_info + ci;
        JSC_ASSERT_2(compptr->dc_tbl_no <= 1, ci, compptr->dc_tbl_no);
        JSC_ASSERT_2(compptr->ac_tbl_no <= 1, ci, compptr->dc_tbl_no);
    }

    // Abcouwer JSC 2021 - remove non-baseline
    emit_sof(cinfo, M_SOF0); /* SOF code for baseline implementation */

    // Abcouwer JSC 2021 - remove support for subtract green / color transform
    // Abcouwer JSC 2021 - remove progressive encoding
}


/*
 * Write scan header.
 * This consists of DHT or DAC markers, optional DRI, and SOS.
 * Compressed data will be written following the SOS.
 */

JSC_METHODDEF(void)
write_scan_header (j_compress_ptr cinfo)
{
    JSC_ASSERT(cinfo != NULL);
    my_marker_writer * marker = (my_marker_writer *) cinfo->marker;
    JSC_ASSERT(marker != NULL);
    JINT i;
    jpeg_component_info *compptr;

    // Abcouwer JSC 2021 - remove arithmetic encoding

    /* Emit Huffman tables.
     * Note that emit_dht() suppresses any duplicate tables.
     */
    for (i = 0; i < cinfo->comps_in_scan; i++) {
        compptr = cinfo->cur_comp_info[i];
        JSC_ASSERT(compptr != NULL);
        /* DC needs no table for refinement scan */
        if (cinfo->Ss == 0 && cinfo->Ah == 0) {
            emit_dht(cinfo, compptr->dc_tbl_no, FALSE);
        }
        /* AC needs no table when not present */
        if (cinfo->Se) {
            emit_dht(cinfo, compptr->ac_tbl_no, TRUE);
        }
    }

    /* Emit DRI if required --- note that DRI value could change for each scan.
     * We avoid wasting space with unnecessary DRIs, however.
     */
    if (cinfo->restart_interval != marker->last_restart_interval) {
        emit_dri(cinfo);
        marker->last_restart_interval = cinfo->restart_interval;
    }

    emit_sos(cinfo);
}


/*
 * Write datastream trailer.
 */

JSC_METHODDEF(void)
write_file_trailer (j_compress_ptr cinfo)
{
  emit_marker(cinfo, M_EOI);
}

// Abcouwer JSC 2021 - remove write_tables_only()

/*
 * Initialize the marker writer module.
 */

JSC_GLOBAL(void)
jinit_marker_writer (j_compress_ptr cinfo)
{
    JSC_ASSERT(cinfo != NULL);

    my_marker_writer * marker;

    /* Create the subobject */
    marker = (my_marker_writer *) (*cinfo->mem->get_mem)((j_common_ptr) cinfo,
            JPOOL_IMAGE, SIZEOF(my_marker_writer));
    cinfo->marker = &marker->pub;
    /* Initialize method pointers */
    marker->pub.write_file_header = write_file_header;
    marker->pub.write_frame_header = write_frame_header;
    marker->pub.write_scan_header = write_scan_header;
    marker->pub.write_file_trailer = write_file_trailer;
    marker->pub.write_marker_header = write_marker_header;
    marker->pub.write_marker_byte = write_marker_byte;
    /* Initialize private state */
    marker->last_restart_interval = 0;
}
