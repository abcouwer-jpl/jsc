/*
 * jdmarker.c
 *
 * Copyright (C) 1991-1998, Thomas G. Lane.
 * Modified 2009-2019 by Guido Vollbeding.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
 *
 * This file contains routines to decode JPEG datastream markers.
 * Most of the complexity arises from our desire to support input
 * suspension: if not all of the data for a marker is available,
 * we must exit back to the application.  On resumption, we reprocess
 * the marker.
 */

#include "jsc/jpegint.h"
#include "jsc/jsc_types_pub.h"

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
  struct jpeg_marker_reader pub; /* public fields */

  /* Application-overridable marker processing methods */
  jpeg_marker_parser_method process_COM;
  jpeg_marker_parser_method process_APPn[16];

  /* Limit on marker data length to save for each marker type */
  JUINT length_limit_COM;
  JUINT length_limit_APPn[16];

  JUINT bytes_read;                /* data bytes read so far in marker */
} my_marker_reader;

/*
 * Macros for fetching data from the data source module.
 *
 * At all times, cinfo->src->next_input_byte and ->bytes_in_buffer reflect
 * the current restart point; we update them only when we have reached a
 * suitable place to restart if a suspension occurs.
 */

/* Declare and initialize local copies of input pointer/count */
#define INPUT_VARS(cinfo)  \
        struct jpeg_source_mgr * datasrc = (cinfo)->src;  \
        const JOCTET * next_input_byte = datasrc->next_input_byte;  \
        JSIZE bytes_in_buffer = datasrc->bytes_in_buffer

/* Unload the local copies --- do this only at a restart boundary */
#define INPUT_SYNC(cinfo)  \
        ( datasrc->next_input_byte = next_input_byte,  \
          datasrc->bytes_in_buffer = bytes_in_buffer )

/* Reload the local copies --- used only in MAKE_BYTE_AVAIL */
#define INPUT_RELOAD(cinfo)  \
        ( next_input_byte = datasrc->next_input_byte,  \
          bytes_in_buffer = datasrc->bytes_in_buffer )

/* Internal macro for INPUT_BYTE and INPUT_2BYTES: make a byte available.
 * Note we do *not* do INPUT_SYNC before calling fill_input_buffer,
 * but we must reload the local copies after a successful fill.
 */
#define MAKE_BYTE_AVAIL(cinfo,action)  \
        if (bytes_in_buffer == 0) {  \
          if (! (*datasrc->fill_input_buffer) (cinfo))  \
            { action; }  \
          INPUT_RELOAD(cinfo);  \
        }

/* Read a byte into variable V.
 * If must suspend, take the specified action (typically "return FALSE").
 */
#define MAKESTMT(stuff)     do { stuff } while (0)

#define INPUT_BYTE(cinfo,V,action)  \
        MAKESTMT( MAKE_BYTE_AVAIL(cinfo,action); \
                  bytes_in_buffer--; \
                  V = GETJOCTET(*next_input_byte++); )

/* As above, but read two bytes interpreted as an unsigned 16-bit integer.
 * V should be declared unsigned int or perhaps INT32.
 */
#define INPUT_2BYTES(cinfo,V,action)  \
        MAKESTMT( MAKE_BYTE_AVAIL(cinfo,action); \
                  bytes_in_buffer--; \
                  V = ((JUINT) GETJOCTET(*next_input_byte++)) << 8; \
                  MAKE_BYTE_AVAIL(cinfo,action); \
                  bytes_in_buffer--; \
                  V += GETJOCTET(*next_input_byte++); )

// Abcouwer JSC 2021 - no arithmetic encoding
#define get_dac(cinfo)  skip_variable(cinfo)

/*
 * Routines for processing APPn and COM markers.
 * These are either saved in memory or discarded, per application request.
 * APP0 and APP14 are specially checked to see if they are
 * JFIF and Adobe markers, respectively.
 */

#define APP0_DATA_LEN        14        /* Length of interesting data in APP0 */
#define APP14_DATA_LEN       12        /* Length of interesting data in APP14 */
#define APPN_DATA_LEN        14        /* Must be the largest of the above!! */

/*
 * Routines to process JPEG markers.
 *
 * Entry condition: JPEG marker itself has been read and its code saved
 *   in cinfo->unread_marker; input restart point is just after the marker.
 *
 * Exit: if return TRUE, have read and processed any parameters, and have
 *   updated the restart point to point after the parameters.
 *   If return FALSE, was forced to suspend before reaching end of
 *   marker parameters; restart point has not been moved.  Same routine
 *   will be called again after application supplies more input data.
 *
 * This approach to suspension assumes that all of a marker's parameters
 * can fit into a single input bufferload.  This should hold for "normal"
 * markers.  Some COM/APPn markers might have large parameter segments
 * that might not fit.  If we are simply dropping such a marker, we use
 * skip_input_data to get past it, and thereby put the problem on the
 * source manager's shoulders.  If we are saving the marker's contents
 * into memory, we use a slightly different convention: when forced to
 * suspend, the marker processor updates the restart point to the end of
 * what it's consumed (ie, the end of the buffer) before returning FALSE.
 * On resumption, cinfo->unread_marker still contains the marker code,
 * but the data source will point to the next chunk of marker data.
 * The marker processor must retain internal state to deal with this.
 *
 * Note that we don't bother to avoid duplicate trace messages if a
 * suspension occurs within marker parameters.  Other side effects
 * require more care.
 */


JSC_LOCAL(boolean)
get_soi (j_decompress_ptr cinfo)
/* Process an SOI marker */
{
  JINT i;
  
  JSC_TRACE(cinfo->trace_level, 1, JTRC_SOI, "Start of Image");

  if (cinfo->marker->saw_SOI) {
    JSC_WARN(JERR_SOI_DUPLICATE,"Invalid JPEG file structure: two SOI markers");
    return FALSE;
  }

  /* Reset all parameters that are defined to be reset by SOI */

  for (i = 0; i < NUM_ARITH_TBLS; i++) {
    cinfo->arith_dc_L[i] = 0;
    cinfo->arith_dc_U[i] = 1;
    cinfo->arith_ac_K[i] = 5;
  }
  cinfo->restart_interval = 0;

  /* Set initial assumptions for colorspace etc */

  cinfo->jpeg_color_space = JCS_UNKNOWN;
  // Abcouwer JSC 2021 - remove support for subtract green / color transform
  // Abcouwer JSC 2021 - no CCIR601_sampling

  cinfo->saw_JFIF_marker = FALSE;
  cinfo->JFIF_major_version = 1; /* set default JFIF APP0 values */
  cinfo->JFIF_minor_version = 1;
  cinfo->density_unit = 0;
  cinfo->X_density = 1;
  cinfo->Y_density = 1;
  cinfo->saw_Adobe_marker = FALSE;
  cinfo->Adobe_transform = 0;

  cinfo->marker->saw_SOI = TRUE;

  return TRUE;
}


JSC_LOCAL(boolean)
get_sof (j_decompress_ptr cinfo, boolean is_baseline, boolean is_prog,
         boolean is_arith)
/* Process a SOFn marker */
{
  INT32 length;
  JINT c;
  JINT ci;
  JINT i;
  jpeg_component_info * compptr;
  INPUT_VARS(cinfo);

  // Abcouwer ZSC 2021 - only support baseline
  if(!is_baseline || is_prog || is_arith) {
      JSC_WARN(JERR_JSC_NOSUPPORT, "Option not supported by JSC.");
      return FALSE;
  }

  cinfo->is_baseline = is_baseline;

  INPUT_2BYTES(cinfo, length, return FALSE);

  INPUT_BYTE(cinfo, cinfo->data_precision, return FALSE);
  INPUT_2BYTES(cinfo, cinfo->image_height, return FALSE);
  INPUT_2BYTES(cinfo, cinfo->image_width, return FALSE);
  INPUT_BYTE(cinfo, cinfo->num_components, return FALSE);

  length -= 8;

  JSC_TRACE_4(cinfo->trace_level, 1, JTRC_SOF,
          "Start Of Frame 0x%02x: width=%u, height=%u, components=%d",
          cinfo->unread_marker, (JINT) cinfo->image_width,
          (JINT) cinfo->image_height, cinfo->num_components);

  if(cinfo->marker->saw_SOF) {
      JSC_WARN(JERR_SOF_DUPLICATE,
              "Invalid JPEG file structure: two SOF markers");
      return FALSE;
  }

  /* We don't support files in which the image height is initially specified */
  /* as 0 and is later redefined by DNL.  As long as we have to check that,  */
  /* might as well have a general sanity check. */
  // Abcouwer JSC 2021 - graceful exit
  if(cinfo->image_height <= 0
          || cinfo->image_width <= 0
          || cinfo->num_components <= 0) {
      JSC_WARN(JERR_EMPTY_IMAGE,"Empty JPEG image (DNL not supported)");
      return FALSE;
  }

  if(length != (cinfo->num_components * 3)) {
      JSC_WARN(JERR_BAD_LENGTH,"Bogus marker length");
      return FALSE;
  }

  /* Only 8 to 12 bits data precision are supported for DCT based JPEG */
  // Abcouwer JSC 2021 - only 8 bits supported
  if(cinfo->data_precision != BITS_IN_JSAMPLE) {
      JSC_WARN_1(JERR_BAD_PRECISION, "Unsupported JPEG data precision %d",
              cinfo->data_precision);
      return FALSE;
  }

  // Abcouwer JSC 2021 - check we have enough working memory to decompress
  if (cinfo->mem->get_mem_size((j_common_ptr) cinfo) <
          JSC_WORKING_MEM_SIZE(cinfo->num_components, cinfo->image_width)) {
      JSC_WARN_3(JERR_OUT_OF_MEMORY, "memsize %d not enough to decompress image "
              "with %d components and width of %d.",
              cinfo->mem->get_mem_size((j_common_ptr) cinfo) ,
              cinfo->num_components, cinfo->image_width);
      return FALSE;
  }

  if (cinfo->comp_info == NULL) {        /* do only once, even if suspend */
    cinfo->comp_info = (jpeg_component_info *) (*cinfo->mem->get_mem)
                        ((j_common_ptr) cinfo, JPOOL_IMAGE,
                         cinfo->num_components * SIZEOF(jpeg_component_info));
  }

  for (ci = 0; ci < cinfo->num_components; ci++) {
    INPUT_BYTE(cinfo, c, return FALSE);
    /* Check to see whether component id has already been seen   */
    /* (in violation of the spec, but unfortunately seen in some */
    /* files).  If so, create "fake" component id equal to the   */
    /* max id seen so far + 1. */
    // Abcouwer JSC 2021 - clean up loop
    compptr = cinfo->comp_info;
    for (i = 0; i < ci; i++) {
      if (c == compptr->component_id) {
        compptr = cinfo->comp_info;
        c = compptr->component_id;
        compptr++;
        for (i = 1; i < ci; i++) {
          if (compptr->component_id > c) {
              c = compptr->component_id;
          }
          compptr++;
        }
        c++;
        break;
      }
      compptr++;
    }
    compptr->component_id = c;
    compptr->component_index = ci;
    INPUT_BYTE(cinfo, c, return FALSE);
    compptr->h_samp_factor = (c >> 4) & 15;
    compptr->v_samp_factor = (c     ) & 15;
    if (compptr->h_samp_factor <= 0
            || compptr->h_samp_factor > MAX_SAMP_FACTOR
            || compptr->v_samp_factor <= 0
            || compptr->v_samp_factor > MAX_SAMP_FACTOR) {
        return FALSE;
    }
    INPUT_BYTE(cinfo, compptr->quant_tbl_no, return FALSE);
    if(compptr->quant_tbl_no < 0 || compptr->quant_tbl_no >= NUM_QUANT_TBLS) {
        return FALSE;
    }

    JSC_TRACE_4(cinfo->trace_level, 1, JTRC_SOF_COMPONENT,
            "    Component %d: %dhx%dv q=%d",
             compptr->component_id, compptr->h_samp_factor,
             compptr->v_samp_factor, compptr->quant_tbl_no);
  }

  cinfo->marker->saw_SOF = TRUE;

  INPUT_SYNC(cinfo);
  return TRUE;
}


JSC_LOCAL(boolean)
get_sos (j_decompress_ptr cinfo)
/* Process a SOS marker */
{
  INT32 length;
  JINT c;
  JINT ci;
  JINT i;
  JINT n;
  jpeg_component_info * compptr;
  INPUT_VARS(cinfo);

  if(!cinfo->marker->saw_SOF) {
      JSC_WARN(JERR_SOF_BEFORE,"Invalid JPEG file structure: SOS before SOF");
      return FALSE;
  }

  INPUT_2BYTES(cinfo, length, return FALSE);

  INPUT_BYTE(cinfo, n, return FALSE); /* Number of components */

  JSC_TRACE_1(cinfo->trace_level, 1, JTRC_SOS,
          "Start Of Scan: %d components",
          n);

  // Abcouwer JSC 2021 - remove progressive support
  if(length != (n * 2 + 6) || n > MAX_COMPS_IN_SCAN ||
          n == 0) {
      JSC_WARN(JERR_BAD_LENGTH,"Bogus marker length");
      return FALSE;
  }

  cinfo->comps_in_scan = n;

  /* Collect the component-spec parameters */

  for (i = 0; i < n; i++) {
    INPUT_BYTE(cinfo, c, return FALSE);

    /* Detect the case where component id's are not unique, and, if so, */
    /* create a fake component id using the same logic as in get_sof.   */
    /* Note:  This also ensures that all of the SOF components are      */
    /* referenced in the single scan case, which prevents access to     */
    /* uninitialized memory in later decoding stages. */
    for (ci = 0; ci < i; ci++) {
      if (c == cinfo->cur_comp_info[ci]->component_id) {
        c = cinfo->cur_comp_info[0]->component_id;
        for (ci = 1; ci < i; ci++) {
          compptr = cinfo->cur_comp_info[ci];
          if (compptr->component_id > c) {
              c = compptr->component_id;
          }
        }
        c++;
        break;
      }
    }

    for (ci = 0; ci < cinfo->num_components; ci++) {
        compptr = cinfo->comp_info + ci;
      if (c == compptr->component_id)
        goto id_found;
    }

    // if we got here, bad component id
    JSC_WARN_1(JERR_BAD_COMPONENT_ID, "Invalid component ID %d in SOS", c);
    return FALSE;

  id_found:

    cinfo->cur_comp_info[i] = compptr;
    INPUT_BYTE(cinfo, c, return FALSE);
    compptr->dc_tbl_no = (c >> 4) & 15;
    compptr->ac_tbl_no = (c     ) & 15;
    if(compptr->dc_tbl_no >= NUM_HUFF_TBLS
            || compptr->ac_tbl_no >= NUM_HUFF_TBLS) {
        JSC_WARN_2(JERR_NO_HUFF_TABLE,
                "Huffman table 0x%02x or 0x%02x was not defined",
                compptr->dc_tbl_no, compptr->ac_tbl_no);
        return FALSE;
    }

    JSC_TRACE_3(cinfo->trace_level, 1, JTRC_SOS_COMPONENT,
            "    Component %d: dc=%d ac=%d", compptr->component_id,
            compptr->dc_tbl_no, compptr->ac_tbl_no);
  }

  /* Collect the additional scan parameters Ss, Se, Ah/Al. */
  INPUT_BYTE(cinfo, c, return FALSE);
  cinfo->Ss = c;
  INPUT_BYTE(cinfo, c, return FALSE);
  cinfo->Se = c;
  INPUT_BYTE(cinfo, c, return FALSE);
  cinfo->Ah = (c >> 4) & 15;
  cinfo->Al = (c     ) & 15;

  JSC_TRACE_4(cinfo->trace_level, 1, JTRC_SOS_PARAMS,
          "  Ss=%d, Se=%d, Ah=%d, Al=%d",
          cinfo->Ss, cinfo->Se, cinfo->Ah, cinfo->Al);

  /* Prepare to scan data & restart markers */
  cinfo->marker->next_restart_num = 0;

  /* Count another (non-pseudo) SOS marker */
  if (n) {
      cinfo->input_scan_number++;
  }

  // Abcouwer JSC - do MCU block check from per_scan_setup()
  JINT blocks_in_MCU = 0;
  for (ci = 0; ci < cinfo->comps_in_scan; ci++) {
      compptr = cinfo->cur_comp_info[ci];
      blocks_in_MCU += compptr->h_samp_factor * compptr->v_samp_factor;
  }
  if (blocks_in_MCU > D_MAX_BLOCKS_IN_MCU) {
      JSC_WARN(JERR_BAD_MCU_SIZE,
              "Sampling factors too large for interleaved scan");
      return FALSE;
  }

  INPUT_SYNC(cinfo);
  return TRUE;
}

JSC_LOCAL(boolean)
get_dht (j_decompress_ptr cinfo)
/* Process a DHT marker */
{
  INT32 length;
  UINT8 bits[17];
  UINT8 huffval[256];
  JINT i;
  JINT index;
  JINT count;
  JHUFF_TBL **htblptr;
  INPUT_VARS(cinfo);

  INPUT_2BYTES(cinfo, length, return FALSE);
  length -= 2;
  
  while (length > 16) {
    INPUT_BYTE(cinfo, index, return FALSE);

    JSC_TRACE_1(cinfo->trace_level, 1, JTRC_DHT,
            "Define Huffman Table 0x%02x", index);
      
    bits[0] = 0;
    count = 0;
    for (i = 1; i <= 16; i++) {
      INPUT_BYTE(cinfo, bits[i], return FALSE);
      count += bits[i];
    }

    length -= 1 + 16;

    JSC_TRACE_8(cinfo->trace_level, 2, JTRC_HUFFBITS,
            "        %3d %3d %3d %3d %3d %3d %3d %3d",
            bits[1], bits[2], bits[3], bits[4],
            bits[5], bits[6], bits[7], bits[8]);
    JSC_TRACE_8(cinfo->trace_level, 2, JTRC_HUFFBITS,
            "        %3d %3d %3d %3d %3d %3d %3d %3d",
            bits[ 9], bits[10], bits[11], bits[12],
            bits[13], bits[14], bits[15], bits[16]);

    /* Here we just do minimal validation of the counts to avoid walking
     * off the end of our table space.  jdhuff.c will check more carefully.
     */
    // Abcouwer JSC 2021 - do more checks here - avoid asserting later on garbage
    if(count > 256 || ((INT32) count) > length) {
        JSC_WARN(JERR_BAD_HUFF_TABLE, "Bogus Huffman table definition");
        return FALSE;
    }

    for (i = 0; i < count; i++) {
      INPUT_BYTE(cinfo, huffval[i], return FALSE);
    }

    length -= count;

    if (index & 0x10) {                /* AC table definition */
      index -= 0x10;
      htblptr = &cinfo->ac_huff_tbl_ptrs[index];
    } else {                        /* DC table definition */
      htblptr = &cinfo->dc_huff_tbl_ptrs[index];
      // Abcouwer JSC 2021 - DC tables mut be in range 0..15
      for (i = 0; i < count; i++) {
          if(huffval[i] > 15) {
              JSC_WARN(JERR_JSC_BAD_HUFFVALS, "Bad huffman table values.");
              return FALSE;
          }
      }
    }

    if(index < 0 || index >= NUM_HUFF_TBLS) {
        JSC_WARN_1(JERR_DHT_INDEX, "Bogus DHT index %d", index);
        return FALSE;
    }

    if (*htblptr == NULL) {
      *htblptr = jpeg_get_mem_huff_table((j_common_ptr) cinfo);
    }

    JSC_MEMCOPY((*htblptr)->bits, bits, SIZEOF((*htblptr)->bits));
    if (count > 0) {
      JSC_MEMCOPY((*htblptr)->huffval, huffval, count * SIZEOF(UINT8));
    }
  }

  if(length != 0) {
      JSC_WARN(JERR_BAD_LENGTH, "Bogus marker length");
      return FALSE;
  }

  INPUT_SYNC(cinfo);
  return TRUE;
}


JSC_LOCAL(boolean)
get_dqt (j_decompress_ptr cinfo)
/* Process a DQT marker */
{
  INT32 length, count, i;
  JINT n;
  JINT prec;
  JUINT tmp;
  JQUANT_TBL *quant_ptr;
  const JINT *natural_order;
  INPUT_VARS(cinfo);

  INPUT_2BYTES(cinfo, length, return FALSE);
  length -= 2;

  while (length > 0) {
    length--;
    INPUT_BYTE(cinfo, n, return FALSE);
    prec = n >> 4;
    n &= 0x0F;

    JSC_TRACE_2(cinfo->trace_level, 1, JTRC_DQT,
            "Define Quantization Table %d  precision %d",
            n, prec);

    //JSC_ASSERT_2(n < NUM_QUANT_TBLS, n, NUM_QUANT_TBLS);
    if(n >= NUM_QUANT_TBLS) {
        JSC_WARN_1(JERR_DQT_INDEX,"Bogus DQT index %d",n);
        return FALSE;
    }

    if (cinfo->quant_tbl_ptrs[n] == NULL) {
      cinfo->quant_tbl_ptrs[n] = jpeg_get_mem_quant_table((j_common_ptr) cinfo);
    } else {
      JSC_WARN_1(JERR_JSC_DUPLICATE_QUANT_TBL,"Got duplicate quant tbl %d",n);
      return FALSE;
    }
    quant_ptr = cinfo->quant_tbl_ptrs[n];

    // Abcouwer JSC 2021 - don't support
    if(prec) {
        JSC_WARN(JERR_JSC_NOSUPPORT, "Nonzero prec not supported.");
        return FALSE;
    }
    if(length < DCTSIZE2) {
        JSC_WARN(JERR_JSC_NOSUPPORT, "DCT size < 8 not supported.");
        return FALSE;
    }

    count = DCTSIZE2;

    natural_order = jpeg_natural_order;
    // Abcouwer JSC 2021 - only 8x8

    for (i = 0; i < count; i++) {
      INPUT_BYTE(cinfo, tmp, return FALSE);
      /* We convert the zigzag-order table to natural array order. */
      quant_ptr->quantval[natural_order[i]] = (UINT16) tmp;
    }

    if (cinfo->trace_level >= 2) {
      for (i = 0; i < DCTSIZE2; i += 8) {
        JSC_TRACE_8(cinfo->trace_level, 2, JTRC_QUANTVALS,
                 "        %4u %4u %4u %4u %4u %4u %4u %4u",
                 quant_ptr->quantval[i],   quant_ptr->quantval[i+1],
                 quant_ptr->quantval[i+2], quant_ptr->quantval[i+3],
                 quant_ptr->quantval[i+4], quant_ptr->quantval[i+5],
                 quant_ptr->quantval[i+6], quant_ptr->quantval[i+7]);
      }
    }

    length -= count;
    if (prec) {
        length -= count;
    }
  }

  // Abcouwer JSC - return false if corrupted
  if (length < 0) {
      JSC_WARN(JERR_BAD_LENGTH, "Bogus marker length");
      return FALSE;
  }

  INPUT_SYNC(cinfo);
  return TRUE;
}


JSC_LOCAL(boolean)
get_dri (j_decompress_ptr cinfo)
/* Process a DRI marker */
{
  INT32 length;
  JUINT tmp;
  INPUT_VARS(cinfo);

  INPUT_2BYTES(cinfo, length, return FALSE);
  
  if(length != 4) {
      JSC_WARN(JERR_BAD_LENGTH, "Bogus marker length");
      return FALSE;
  }

  INPUT_2BYTES(cinfo, tmp, return FALSE);

  JSC_TRACE_1(cinfo->trace_level, 1, JTRC_DRI,
          "Define Restart Interval %u", tmp);

  cinfo->restart_interval = tmp;

  INPUT_SYNC(cinfo);
  return TRUE;
}

// Abcouwer JSC 2021 - remove support for subtract green / color transform



JSC_LOCAL(void)
examine_app0 (j_decompress_ptr cinfo, JOCTET * data,
              JUINT datalen, INT32 remaining)
/* Examine first few bytes from an APP0.
 * Take appropriate action if it is a JFIF marker.
 * datalen is # of bytes at data[], remaining is length of rest of marker data.
 */
{
  INT32 totallen = (INT32) datalen + remaining;

  if (datalen >= APP0_DATA_LEN &&
      GETJOCTET(data[0]) == 0x4A &&
      GETJOCTET(data[1]) == 0x46 &&
      GETJOCTET(data[2]) == 0x49 &&
      GETJOCTET(data[3]) == 0x46 &&
      GETJOCTET(data[4]) == 0) {
    /* Found JFIF APP0 marker: save info */
    cinfo->saw_JFIF_marker = TRUE;
    cinfo->JFIF_major_version = GETJOCTET(data[5]);
    cinfo->JFIF_minor_version = GETJOCTET(data[6]);
    cinfo->density_unit = GETJOCTET(data[7]);
    cinfo->X_density = (GETJOCTET(data[8]) << 8) + GETJOCTET(data[9]);
    cinfo->Y_density = (GETJOCTET(data[10]) << 8) + GETJOCTET(data[11]);
    /* Check version.
     * Major version must be 1 or 2, anything else signals an incompatible
     * change.
     * (We used to treat this as an error, but now it's a nonfatal warning,
     * because some bozo at Hijaak couldn't read the spec.)
     * Minor version should be 0..2, but process anyway if newer.
     */
    if (cinfo->JFIF_major_version != 1 && cinfo->JFIF_major_version != 2) {
        JSC_WARN_2(JWRN_JFIF_MAJOR,
                "Warning: unknown JFIF revision number %d.%02d",
              cinfo->JFIF_major_version, cinfo->JFIF_minor_version);
    }
    /* Generate trace messages */
    JSC_TRACE_5(cinfo->trace_level, 1, JTRC_JFIF,
            "JFIF APP0 marker: version %d.%02d, density %dx%d  %d",
             cinfo->JFIF_major_version, cinfo->JFIF_minor_version,
             cinfo->X_density, cinfo->Y_density, cinfo->density_unit);
    /* Validate thumbnail dimensions and issue appropriate messages */
    if (GETJOCTET(data[12]) | GETJOCTET(data[13])) {
      JSC_TRACE_2(cinfo->trace_level, 1, JTRC_JFIF_THUMBNAIL,
              "    with %d x %d thumbnail image",
               GETJOCTET(data[12]), GETJOCTET(data[13]));
    }
    totallen -= APP0_DATA_LEN;
    if (totallen !=
        ((INT32)GETJOCTET(data[12]) * (INT32)GETJOCTET(data[13]) * (INT32) 3)) {
        JSC_TRACE_1(cinfo->trace_level, 1, JTRC_JFIF_BADTHUMBNAILSIZE,
                "Warning: thumbnail image size does not match data length %u",
                (JINT) totallen);
    }
  } else if (datalen >= 6 &&
      GETJOCTET(data[0]) == 0x4A &&
      GETJOCTET(data[1]) == 0x46 &&
      GETJOCTET(data[2]) == 0x58 &&
      GETJOCTET(data[3]) == 0x58 &&
      GETJOCTET(data[4]) == 0) {
    /* Found JFIF "JFXX" extension APP0 marker */
    /* The library doesn't actually do anything with these,
     * but we try to produce a helpful trace message.
     */
    switch (GETJOCTET(data[5])) {
    case 0x10:
      JSC_TRACE_1(cinfo->trace_level, 1, JTRC_THUMB_JPEG,
              "JFIF extension marker: JPEG-compressed thumbnail image, length %u",
              (JINT) totallen);
      break;
    case 0x11:
        JSC_TRACE_1(cinfo->trace_level, 1, JTRC_THUMB_PALETTE,
                "JFIF extension marker: palette thumbnail image, length %u",
                (JINT) totallen);
      break;
    case 0x13:
        JSC_TRACE_1(cinfo->trace_level, 1, JTRC_THUMB_RGB,
                "JFIF extension marker: RGB thumbnail image, length %u",
                (JINT) totallen);
      break;
    default:
        JSC_TRACE_2(cinfo->trace_level, 1, JTRC_JFIF_EXTENSION,
                "JFIF extension marker: type 0x%02x, length %u",
                GETJOCTET(data[5]), (JINT) totallen);
    }
  } else {
    /* Start of APP0 does not match "JFIF" or "JFXX", or too short */
    JSC_TRACE_1(cinfo->trace_level, 1, JTRC_APP0,
            "Unknown APP0 marker (not JFIF), length %u",
            (JINT) totallen);
  }
}


JSC_LOCAL(void)
examine_app14 (j_decompress_ptr cinfo, JOCTET * data,
               JUINT datalen, INT32 remaining)
/* Examine first few bytes from an APP14.
 * Take appropriate action if it is an Adobe marker.
 * datalen is # of bytes at data[], remaining is length of rest of marker data.
 */
{
  JUINT version;
  JUINT flags0;
  JUINT flags1;
  JUINT transform;

  if (datalen >= APP14_DATA_LEN &&
      GETJOCTET(data[0]) == 0x41 &&
      GETJOCTET(data[1]) == 0x64 &&
      GETJOCTET(data[2]) == 0x6F &&
      GETJOCTET(data[3]) == 0x62 &&
      GETJOCTET(data[4]) == 0x65) {
    /* Found Adobe APP14 marker */
    version = (GETJOCTET(data[5]) << 8) + GETJOCTET(data[6]);
    flags0 = (GETJOCTET(data[7]) << 8) + GETJOCTET(data[8]);
    flags1 = (GETJOCTET(data[9]) << 8) + GETJOCTET(data[10]);
    transform = GETJOCTET(data[11]);
    JSC_TRACE_4(cinfo->trace_level, 1, JTRC_ADOBE,
            "Adobe APP14 marker: version %d, flags 0x%04x 0x%04x, transform %d",
            version, flags0, flags1, transform);
    cinfo->saw_Adobe_marker = TRUE;
    cinfo->Adobe_transform = (UINT8) transform;
  } else {
    /* Start of APP14 does not match "Adobe", or too short */
    JSC_TRACE_1(cinfo->trace_level, 1, JTRC_APP14,
            "Unknown APP14 marker (not Adobe), length %u",
            (JINT) (datalen + remaining));
  }
}


JSC_METHODDEF(boolean)
get_interesting_appn (j_decompress_ptr cinfo)
/* Process an APP0 or APP14 marker without saving it */
{
  INT32 length;
  JOCTET b[APPN_DATA_LEN];
  JUINT i;
  JUINT numtoread;
  INPUT_VARS(cinfo);

  INPUT_2BYTES(cinfo, length, return FALSE);
  length -= 2;

  /* get the interesting part of the marker data */
  if (length >= APPN_DATA_LEN) {
    numtoread = APPN_DATA_LEN;
  } else if (length > 0) {
    numtoread = (JUINT) length;
  } else {
    numtoread = 0;
  }
  for (i = 0; i < numtoread; i++) {
    INPUT_BYTE(cinfo, b[i], return FALSE);
  }
  length -= numtoread;

  /* process it */
  switch (cinfo->unread_marker) {
  case M_APP0:
    examine_app0(cinfo, (JOCTET *) b, numtoread, length);
    break;
  case M_APP14:
    examine_app14(cinfo, (JOCTET *) b, numtoread, length);
    break;
  default:
    /* can't get here unless jpeg_save_markers chooses wrong processor */
    JSC_WARN_1(JERR_UNKNOWN_MARKER, "Unsupported marker type 0x%02x",
            cinfo->unread_marker);
    return FALSE;
  }

  /* skip any remaining data -- could be lots */
  INPUT_SYNC(cinfo);
  if (length > 0) {
    (*cinfo->src->skip_input_data) (cinfo, (JLONG) length);
  }

  return TRUE;
}

// Abcouwer JSC 2021 - no marker saving supported

JSC_METHODDEF(boolean)
skip_variable (j_decompress_ptr cinfo)
/* Skip over an unknown or uninteresting variable-length marker */
{
  INT32 length;
  INPUT_VARS(cinfo);

  INPUT_2BYTES(cinfo, length, return FALSE);
  length -= 2;
  
  JSC_TRACE_2(cinfo->trace_level, 1, JTRC_MISC_MARKER,
          "Miscellaneous marker 0x%02x, length %u",
          cinfo->unread_marker, (JINT) length);

  INPUT_SYNC(cinfo);                /* do before skip_input_data */
  if (length > 0)
    (*cinfo->src->skip_input_data) (cinfo, (JLONG) length);

  return TRUE;
}


/*
 * Find the next JPEG marker, save it in cinfo->unread_marker.
 * Returns FALSE if had to suspend before reaching a marker;
 * in that case cinfo->unread_marker is unchanged.
 *
 * Note that the result might not be a valid marker code,
 * but it will never be 0 or FF.
 */

JSC_LOCAL(boolean)
next_marker (j_decompress_ptr cinfo)
{
  JINT c;
  INPUT_VARS(cinfo);

  for (;;) {
    INPUT_BYTE(cinfo, c, return FALSE);
    /* Skip any non-FF bytes.
     * This may look a bit inefficient, but it will not occur in a valid file.
     * We sync after each discarded byte so that a suspending data source
     * can discard the byte from its buffer.
     */
    while (c != 0xFF) {
      cinfo->marker->discarded_bytes++;
      INPUT_SYNC(cinfo);
      INPUT_BYTE(cinfo, c, return FALSE);
    }
    /* This loop swallows any duplicate FF bytes.  Extra FFs are legal as
     * pad bytes, so don't count them in discarded_bytes.  We assume there
     * will not be so many consecutive FF bytes as to overflow a suspending
     * data source's input buffer.
     */
    do {
      INPUT_BYTE(cinfo, c, return FALSE);
    } while (c == 0xFF);
    if (c != 0)
      break;                        /* found a valid marker, exit loop */
    /* Reach here if we found a stuffed-zero data sequence (FF/00).
     * Discard it and loop back to try again.
     */
    cinfo->marker->discarded_bytes += 2;
    INPUT_SYNC(cinfo);
  }

  if (cinfo->marker->discarded_bytes != 0) {
    JSC_WARN_2(JWRN_EXTRANEOUS_DATA,
            "Corrupt JPEG data: %u extraneous bytes before marker 0x%02x",
            cinfo->marker->discarded_bytes, c);
    cinfo->marker->discarded_bytes = 0;
    return FALSE;
  }

  cinfo->unread_marker = c;

  INPUT_SYNC(cinfo);
  return TRUE;
}


JSC_LOCAL(boolean)
first_marker (j_decompress_ptr cinfo)
/* Like next_marker, but used to obtain the initial SOI marker. */
/* For this marker, we do not allow preceding garbage or fill; otherwise,
 * we might well scan an entire input file before realizing it ain't JPEG.
 * If an application wants to process non-JFIF files, it must seek to the
 * SOI before calling the JPEG library.
 */
{
  JINT c;
  JINT c2;
  INPUT_VARS(cinfo);

  INPUT_BYTE(cinfo, c, return FALSE);
  INPUT_BYTE(cinfo, c2, return FALSE);

  if (c != 0xFF || c2 != (int) M_SOI) {
    JSC_WARN_2(JERR_NO_SOI, "Not a JPEG file: starts with 0x%02x 0x%02x",
            c, c2);
    return FALSE;
  }

  cinfo->unread_marker = c2;

  INPUT_SYNC(cinfo);
  return TRUE;
}


/*
 * Read markers until SOS or EOI.
 *
 * Returns same codes as are defined for jpeg_consume_input:
 * JPEG_SUSPENDED, JPEG_REACHED_SOS, or JPEG_REACHED_EOI.
 *
 * Note: This function may return a pseudo SOS marker (with zero
 * component number) for treat by input controller's consume_input.
 * consume_input itself should filter out (skip) the pseudo marker
 * after processing for the caller.
 */

JSC_METHODDEF(JINT)
read_markers (j_decompress_ptr cinfo)
{
  /* Outer loop repeats once for each marker. */
    // Abcouwer JSC 2021 - add loop limit
  JINT i;
  JINT loop_limit = 1000;
  for (i = 0; i < loop_limit; i++) {
    /* Collect the marker proper, unless we already did. */
    /* NB: first_marker() enforces the requirement that SOI appear first. */
    if (cinfo->unread_marker == 0) {
        if (!cinfo->marker->saw_SOI && !first_marker(cinfo)) {
            return JPEG_SUSPENDED;
        }
        if (cinfo->marker->saw_SOI && !next_marker(cinfo)) {
            return JPEG_SUSPENDED;
        }
    }

    /* At this point cinfo->unread_marker contains the marker code and the
     * input point is just past the marker proper, but before any parameters.
     * A suspension will cause us to return with this state still true.
     */
    switch (cinfo->unread_marker) {
    case M_SOI:
      if (! get_soi(cinfo))
        return JPEG_SUSPENDED;
      break;

    case M_SOF0:                /* Baseline */
      if (! get_sof(cinfo, TRUE, FALSE, FALSE))
        return JPEG_SUSPENDED;
      break;

      // Abcouwer JSC 2021 - only support baseline
    case M_SOF1:                /* Extended sequential, Huffman */
    case M_SOF2:                /* Progressive, Huffman */
    case M_SOF9:                /* Extended sequential, arithmetic */
    case M_SOF10:                /* Progressive, arithmetic */
//      JSC_ASSERT_1(0, cinfo->unread_marker);
      return JPEG_SUSPENDED;
      break;

    /* Currently unsupported SOFn types */
    case M_SOF3:                /* Lossless, Huffman */
    case M_SOF5:                /* Differential sequential, Huffman */
    case M_SOF6:                /* Differential progressive, Huffman */
    case M_SOF7:                /* Differential lossless, Huffman */
    case M_JPG:                        /* Reserved for JPEG extensions */
    case M_SOF11:                /* Lossless, arithmetic */
    case M_SOF13:                /* Differential sequential, arithmetic */
    case M_SOF14:                /* Differential progressive, arithmetic */
    case M_SOF15:                /* Differential lossless, arithmetic */
//        JSC_ASSERT_1(0, cinfo->unread_marker);
        return JPEG_SUSPENDED;

      break;

    case M_SOS:
      if (! get_sos(cinfo)) {
        return JPEG_SUSPENDED;
      }
      cinfo->unread_marker = 0;        /* processed the marker */
      return JPEG_REACHED_SOS;
      break;

    case M_EOI:
      JSC_TRACE(cinfo->trace_level, 1, JTRC_EOI, "End Of Image");
      cinfo->unread_marker = 0;        /* processed the marker */
      return JPEG_REACHED_EOI;
      break;

    case M_DAC:
      if (! get_dac(cinfo)) {
        return JPEG_SUSPENDED;
      }
      break;

    case M_DHT:
      if (! get_dht(cinfo)) {
        return JPEG_SUSPENDED;
      }
      break;

    case M_DQT:
      if (! get_dqt(cinfo)) {
        return JPEG_SUSPENDED;
      }
      break;

    case M_DRI:
      if (! get_dri(cinfo)) {
        return JPEG_SUSPENDED;
      }
      break;

    case M_JPG8:
      // Abcouwer JSC 2021 - remove support for subtract green / color transform
//      JSC_ASSERT_1(0, cinfo->unread_marker);
      return JPEG_SUSPENDED;
      break;

    case M_APP0:
    case M_APP1:
    case M_APP2:
    case M_APP3:
    case M_APP4:
    case M_APP5:
    case M_APP6:
    case M_APP7:
    case M_APP8:
    case M_APP9:
    case M_APP10:
    case M_APP11:
    case M_APP12:
    case M_APP13:
    case M_APP14:
    case M_APP15:
      if (! (*((my_marker_reader *) cinfo->marker)->process_APPn[
                cinfo->unread_marker - (JINT) M_APP0]) (cinfo)) {
        return JPEG_SUSPENDED;
      }
      break;

    case M_COM:
      if (! (*((my_marker_reader *) cinfo->marker)->process_COM) (cinfo)) {
        return JPEG_SUSPENDED;
      }
      break;

    case M_RST0:                /* these are all parameterless */
    case M_RST1:
    case M_RST2:
    case M_RST3:
    case M_RST4:
    case M_RST5:
    case M_RST6:
    case M_RST7:
    case M_TEM:
      JSC_TRACE_1(cinfo->trace_level, 1, JTRC_PARMLESS_MARKER,
              "Unexpected marker 0x%02x",
              cinfo->unread_marker);
      break;

    case M_DNL:                        /* Ignore DNL ... perhaps the wrong thing */
      if (! skip_variable(cinfo)) {
        return JPEG_SUSPENDED;
      }
      break;

    default:                        /* must be DHP, EXP, JPGn, or RESn */
      /* For now, we treat the reserved markers as fatal errors since they are
       * likely to be used to signal incompatible JPEG Part 3 extensions.
       * Once the JPEG 3 version-number marker is well defined, this code
       * ought to change!
       */
        return JPEG_SUSPENDED;
    }
    /* Successfully processed marker, so reset state variable */
    cinfo->unread_marker = 0;
  } /* end loop */

  // should never get here
  JSC_ASSERT_2(0, i, loop_limit);
}


/*
 * Read a restart marker, which is expected to appear next in the datastream;
 * if the marker is not there, take appropriate recovery action.
 * Returns FALSE if suspension is required.
 *
 * This is called by the entropy decoder after it has read an appropriate
 * number of MCUs.  cinfo->unread_marker may be nonzero if the entropy decoder
 * has already read a marker from the data source.  Under normal conditions
 * cinfo->unread_marker will be reset to 0 before returning; if not reset,
 * it holds a marker which the decoder will be unable to read past.
 */

JSC_METHODDEF(boolean)
read_restart_marker (j_decompress_ptr cinfo)
{
  /* Obtain a marker unless we already did. */
  /* Note that next_marker will complain if it skips any data. */
  if (cinfo->unread_marker == 0) {
    if (! next_marker(cinfo)) {
      return FALSE;
    }
  }

  if (cinfo->unread_marker ==
      ((JINT) M_RST0 + cinfo->marker->next_restart_num)) {
    /* Normal case --- swallow the marker and let entropy decoder continue */
    JSC_TRACE_1(cinfo->trace_level, 3, JTRC_RST,
            "RST%d", cinfo->marker->next_restart_num);
    cinfo->unread_marker = 0;
  } else {
    /* Uh-oh, the restart markers have been messed up. */
    /* Let the data source manager determine how to resync. */
    if (! (*cinfo->src->resync_to_restart) (cinfo,
                                            cinfo->marker->next_restart_num)) {
      return FALSE;
    }
  }

  /* Update next-restart state */
  cinfo->marker->next_restart_num = (cinfo->marker->next_restart_num + 1) & 7;

  return TRUE;
}


/*
 * This is the default resync_to_restart method for data source managers
 * to use if they don't have any better approach.  Some data source managers
 * may be able to back up, or may have additional knowledge about the data
 * which permits a more intelligent recovery strategy; such managers would
 * presumably supply their own resync method.
 *
 * read_restart_marker calls resync_to_restart if it finds a marker other than
 * the restart marker it was expecting.  (This code is *not* used unless
 * a nonzero restart interval has been declared.)  cinfo->unread_marker is
 * the marker code actually found (might be anything, except 0 or FF).
 * The desired restart marker number (0..7) is passed as a parameter.
 * This routine is supposed to apply whatever error recovery strategy seems
 * appropriate in order to position the input stream to the next data segment.
 * Note that cinfo->unread_marker is treated as a marker appearing before
 * the current data-source input point; usually it should be reset to zero
 * before returning.
 * Returns FALSE if suspension is required.
 *
 * This implementation is substantially constrained by wanting to treat the
 * input as a data stream; this means we can't back up.  Therefore, we have
 * only the following actions to work with:
 *   1. Simply discard the marker and let the entropy decoder resume at next
 *      byte of file.
 *   2. Read forward until we find another marker, discarding intervening
 *      data.  (In theory we could look ahead within the current bufferload,
 *      without having to discard data if we don't find the desired marker.
 *      This idea is not implemented here, in part because it makes behavior
 *      dependent on buffer size and chance buffer-boundary positions.)
 *   3. Leave the marker unread (by failing to zero cinfo->unread_marker).
 *      This will cause the entropy decoder to process an empty data segment,
 *      inserting dummy zeroes, and then we will reprocess the marker.
 *
 * #2 is appropriate if we think the desired marker lies ahead, while #3 is
 * appropriate if the found marker is a future restart marker (indicating
 * that we have missed the desired restart marker, probably because it got
 * corrupted).
 * We apply #2 or #3 if the found marker is a restart marker no more than
 * two counts behind or ahead of the expected one.  We also apply #2 if the
 * found marker is not a legal JPEG marker code (it's certainly bogus data).
 * If the found marker is a restart marker more than 2 counts away, we do #1
 * (too much risk that the marker is erroneous; with luck we will be able to
 * resync at some future point).
 * For any valid non-restart JPEG marker, we apply #3.  This keeps us from
 * overrunning the end of a scan.  An implementation limited to single-scan
 * files might find it better to apply #2 for markers other than EOI, since
 * any other marker would have to be bogus data in that case.
 */

JSC_GLOBAL(boolean)
jpeg_resync_to_restart (j_decompress_ptr cinfo, JINT desired)
{
  JINT marker = cinfo->unread_marker;
  JINT action = 1;
  
  /* Always put up a warning. */
  JSC_WARN_2(JWRN_MUST_RESYNC,
          "Corrupt JPEG data: found marker 0x%02x instead of RST%d",
          marker, desired);
  
  /* Outer loop handles repeated decision after scanning forward. */
  for (;;) {
    if (marker < (JINT) M_SOF0) {
      action = 2;                /* invalid marker */
    } else if (marker < (JINT) M_RST0 || marker > (JINT) M_RST7) {
      action = 3;                /* valid non-restart marker */
    } else {
      if (marker == ((JINT) M_RST0 + ((desired+1) & 7)) ||
          marker == ((JINT) M_RST0 + ((desired+2) & 7))) {
        action = 3;                /* one of the next two expected restarts */
      } else if (marker == ((JINT) M_RST0 + ((desired-1) & 7)) ||
               marker == ((JINT) M_RST0 + ((desired-2) & 7))) {
        action = 2;                /* a prior restart, so advance */
      } else {
        action = 1;                /* desired restart or too far away */
      }
    }
    JSC_TRACE_2(cinfo->trace_level, 4, JTRC_RECOVERY_ACTION,
            "At marker 0x%02x, recovery action %d",
            marker, action);
    switch (action) {
    case 1:
      /* Discard marker and let entropy decoder resume processing. */
      cinfo->unread_marker = 0;
      return TRUE;
    case 2:
      /* Scan to the next marker, and repeat the decision loop. */
      if (! next_marker(cinfo)) {
        return FALSE;
      }
      marker = cinfo->unread_marker;
      break;
    case 3:
      /* Return without advancing past this marker. */
      /* Entropy decoder will be forced to process an empty segment. */
      return TRUE;
    default:
        JSC_ASSERT_1(0, action);
    }
  } /* end loop */
}


/*
 * Reset marker processing state to begin a fresh datastream.
 */

JSC_METHODDEF(void)
reset_marker_reader (j_decompress_ptr cinfo)
{
  my_marker_reader * marker = (my_marker_reader *) cinfo->marker;

  cinfo->comp_info = NULL;                /* until allocated by get_sof */
  cinfo->input_scan_number = 0;                /* no SOS seen yet */
  cinfo->unread_marker = 0;                /* no pending marker */
  marker->pub.saw_SOI = FALSE;                /* set internal state too */
  marker->pub.saw_SOF = FALSE;
  marker->pub.discarded_bytes = 0;
}


/*
 * Initialize the marker reader module.
 * This is called only once, when the decompression object is created.
 */

JSC_GLOBAL(void)
jinit_marker_reader (j_decompress_ptr cinfo)
{
  my_marker_reader * marker;
  JINT i;

  /* Create subobject in permanent pool */
  marker = (my_marker_reader *) (*cinfo->mem->get_mem)
    ((j_common_ptr) cinfo, JPOOL_PERMANENT, SIZEOF(my_marker_reader));
  cinfo->marker = &marker->pub;
  /* Initialize public method pointers */
  marker->pub.reset_marker_reader = reset_marker_reader;
  marker->pub.read_markers = read_markers;
  marker->pub.read_restart_marker = read_restart_marker;
  /* Initialize COM/APPn processing.
   * By default, we examine and then discard APP0 and APP14,
   * but simply discard COM and all other APPn.
   */
  marker->process_COM = skip_variable;
  marker->length_limit_COM = 0;
  for (i = 0; i < 16; i++) {
    marker->process_APPn[i] = skip_variable;
    marker->length_limit_APPn[i] = 0;
  }
  marker->process_APPn[0] = get_interesting_appn;
  marker->process_APPn[14] = get_interesting_appn;
  /* Reset marker processing state */
  reset_marker_reader(cinfo);
}

// Abcouwer JSC 2021 - no marker saving support

/*
 * Install a special processing method for COM or APPn markers.
 */

JSC_GLOBAL(void)
jpeg_set_marker_processor (j_decompress_ptr cinfo, JINT marker_code,
                           jpeg_marker_parser_method routine)
{
  my_marker_reader * marker = (my_marker_reader *) cinfo->marker;

  if (marker_code == (JINT) M_COM) {
    marker->process_COM = routine;
  } else if (marker_code >= (JINT) M_APP0 && marker_code <= (JINT) M_APP15) {
    marker->process_APPn[marker_code - (JINT) M_APP0] = routine;
  } else {
    JSC_ASSERT_1(0, marker_code);
  }
}
