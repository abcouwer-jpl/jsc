/*
 * jpeglib.h
 *
 * Copyright (C) 1991-1998, Thomas G. Lane.
 * Modified 2002-2019 by Guido Vollbeding.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
 *
 * This file defines the application interface for the JPEG library.
 * Most applications using the library need only include this file,
 * and perhaps jerror.h if they want to know the exact error codes.
 */

#ifndef JPEGLIB_H
#define JPEGLIB_H

/*
 * First we include the configuration files that record how this
 * installation of the JPEG library is set up.  jconfig.h can be
 * generated automatically for many systems.  jmorecfg.h contains
 * manual configuration options that most people need not worry about.
 */

#include "jsc_conf_global_types.h"


/* Version IDs for the JPEG library.
 * Might be useful for tests like "#if JPEG_LIB_VERSION >= 90".
 */

#define JPEG_LIB_VERSION        90	/* Compatibility version 9.0 */
#define JPEG_LIB_VERSION_MAJOR  9
#define JPEG_LIB_VERSION_MINOR  4


/* Various constants determining the sizes of things.
 * All of these are specified by the JPEG standard,
 * so don't change them if you want to be compatible.
 */

#define DCTSIZE		    8	/* The basic DCT block is 8x8 coefficients */
#define DCTSIZE2	    64	/* DCTSIZE squared; # of elements in a block */
#define NUM_QUANT_TBLS      4	/* Quantization tables are numbered 0..3 */
#define NUM_HUFF_TBLS       2	/* Huffman tables are numbered 0..1 (mod Abcouwer JSC)*/
#define NUM_ARITH_TBLS      16	/* Arith-coding tables are numbered 0..15 */
#define MAX_COMPS_IN_SCAN   4	/* JPEG limit on # of components in one scan */
#define MAX_SAMP_FACTOR     4	/* JPEG limit on sampling factors */
/* Unfortunately, some bozo at Adobe saw no reason to be bound by the standard;
 * the PostScript DCT filter can emit files with many more than 10 blocks/MCU.
 * If you happen to run across such a file, you can up D_MAX_BLOCKS_IN_MCU
 * to handle it.  We even let you do this from the jconfig.h file.  However,
 * we strongly discourage changing C_MAX_BLOCKS_IN_MCU; just because Adobe
 * sometimes emits noncompliant files doesn't mean you should too.
 */
#define C_MAX_BLOCKS_IN_MCU   10 /* compressor's limit on blocks per MCU */
#define D_MAX_BLOCKS_IN_MCU   10 /* decompressor's limit on blocks per MCU */

#define JMSG_LENGTH_MAX  200    /* recommended size of format_message buffer */
#define JMSG_STR_PARM_MAX  80

// jpeg_read_header() - return value is one of:
#define JPEG_SUSPENDED      0 /* Suspended due to lack of input data */
#define JPEG_HEADER_OK      1 /* Found valid image datastream */
#define JPEG_HEADER_TABLES_ONLY 2 /* Found valid table-specs-only datastream */

// jpeg_consume_input() - Return value is one of: */
/* #define JPEG_SUSPENDED   0    Suspended due to lack of input data */
#define JPEG_REACHED_SOS    1 /* Reached start of new scan */
#define JPEG_REACHED_EOI    2 /* Reached end of image */
#define JPEG_ROW_COMPLETED  3 /* Completed one iMCU row */
#define JPEG_SCAN_COMPLETED 4 /* Completed last iMCU row of a scan */

/* These marker codes are exported since applications and data source modules
 * are likely to want to use them.
 */

#define JPEG_RST0   0xD0    /* RST0 marker code */
#define JPEG_EOI    0xD9    /* EOI marker code */
#define JPEG_APP0   0xE0    /* APP0 marker code */
#define JPEG_COM    0xFE    /* COM marker code */


/* Initialization of JPEG compression objects.
 * jpeg_create_compress() and jpeg_create_decompress() are the exported
 * names that applications should call.  These expand to calls on
 * jpeg_CreateCompress and jpeg_CreateDecompress with additional information
 * passed for version mismatch checking.
 * NB: you must set up the error-manager BEFORE calling jpeg_create_xxx.
 */
#define jpeg_create_compress(cinfo) \
    jpeg_CreateCompress((cinfo), JPEG_LIB_VERSION, \
            (JSIZE) sizeof(struct jpeg_compress_struct))
#define jpeg_create_decompress(cinfo) \
    jpeg_CreateDecompress((cinfo), JPEG_LIB_VERSION, \
              (JSIZE) sizeof(struct jpeg_decompress_struct))



/* Data structures for images (arrays of samples and of DCT coefficients).
 * On 80x86 machines, the image arrays are too big for near pointers,
 * but the pointer arrays can fit in near memory.
 */

typedef JSAMPLE *JSAMPROW;	/* ptr to one image row of pixel samples. */
typedef JSAMPROW *JSAMPARRAY;	/* ptr to some rows (a 2-D sample array) */
typedef JSAMPARRAY *JSAMPIMAGE;	/* a 3-D sample array: top index is color */

typedef JCOEF JBLOCK[DCTSIZE2];	/* one block of coefficients */
typedef JBLOCK *JBLOCKROW;	/* pointer to one row of coefficient blocks */
typedef JBLOCKROW *JBLOCKARRAY;		/* a 2-D array of coefficient blocks */
typedef JBLOCKARRAY *JBLOCKIMAGE;	/* a 3-D array of coefficient blocks */

typedef JCOEF *JCOEFPTR;	/* useful in a couple of places */


/* Types for JPEG compression parameters and working tables. */


/* DCT coefficient quantization tables. */

typedef struct {
  /* This array gives the coefficient quantizers in natural array order
   * (not the zigzag order in which they are stored in a JPEG DQT marker).
   * CAUTION: IJG versions prior to v6a kept this array in zigzag order.
   */
  UINT16 quantval[DCTSIZE2];	/* quantization step for each coefficient */
  /* This field is used only during compression.  It's initialized FALSE when
   * the table is created, and set TRUE when it's been output to the file.
   * You could suppress output of a table by setting this to TRUE.
   * (See jpeg_suppress_tables for an example.)
   */
  boolean sent_table;		/* TRUE when table has been output */
} JQUANT_TBL;


/* Huffman coding tables. */

typedef struct {
  /* These two fields directly represent the contents of a JPEG DHT marker */
  UINT8 bits[17];		/* bits[k] = # of symbols with codes of */
				/* length k bits; bits[0] is unused */
  UINT8 huffval[256];		/* The symbols, in order of incr code length */
  /* This field is used only during compression.  It's initialized FALSE when
   * the table is created, and set TRUE when it's been output to the file.
   * You could suppress output of a table by setting this to TRUE.
   * (See jpeg_suppress_tables for an example.)
   */
  boolean sent_table;		/* TRUE when table has been output */
} JHUFF_TBL;


/* Basic info about one component (color channel). */

typedef struct {
  /* These values are fixed over the whole image. */
  /* For compression, they must be supplied by parameter setup; */
  /* for decompression, they are read from the SOF marker. */
  JINT component_id;		/* identifier for this component (0..255) */
  JINT component_index;		/* its index in SOF or cinfo->comp_info[] */
  JINT h_samp_factor;		/* horizontal sampling factor (1..4) */
  JINT v_samp_factor;		/* vertical sampling factor (1..4) */
  JINT quant_tbl_no;		/* quantization table selector (0..3) */
  /* These values may vary between scans. */
  /* For compression, they must be supplied by parameter setup; */
  /* for decompression, they are read from the SOS marker. */
  /* The decompressor output side may not use these variables. */
  JINT dc_tbl_no;		/* DC entropy table selector (0..3) */
  JINT ac_tbl_no;		/* AC entropy table selector (0..3) */

  /* Remaining fields should be treated as private by applications. */

  /* These values are computed during compression or decompression startup: */
  /* Component's size in DCT blocks.
   * Any dummy blocks added to complete an MCU are not counted; therefore
   * these values do not depend on whether a scan is interleaved or not.
   */
  JDIMENSION width_in_blocks;
  JDIMENSION height_in_blocks;
  /* Size of a DCT block in samples,
   * reflecting any scaling we choose to apply during the DCT step.
   * Values from 1 to 16 are supported.
   * Note that different components may receive different DCT scalings.
   */
  JINT DCT_h_scaled_size;
  JINT DCT_v_scaled_size;
  /* The downsampled dimensions are the component's actual, unpadded number
   * of samples at the main buffer (preprocessing/compression interface);
   * DCT scaling is included, so
   * downsampled_width =
   *   ceil(image_width * Hi/Hmax * DCT_h_scaled_size/block_size)
   * and similarly for height.
   */
  JDIMENSION downsampled_width;	 /* actual width in samples */
  JDIMENSION downsampled_height; /* actual height in samples */
  /* For decompression, in cases where some of the components will be
   * ignored (eg grayscale output from YCbCr image), we can skip most
   * computations for the unused components.
   * For compression, some of the components will need further quantization
   * scale by factor of 2 after DCT (eg BG_YCC output from normal RGB input).
   * The field is first set TRUE for decompression, FALSE for compression
   * in initial_setup, and then adapted in color conversion setup.
   */
  boolean component_needed;

  /* These values are computed before starting a scan of the component. */
  /* The decompressor output side may not use these variables. */
  JINT MCU_width;		/* number of blocks per MCU, horizontally */
  JINT MCU_height;		/* number of blocks per MCU, vertically */
  JINT MCU_blocks;		/* MCU_width * MCU_height */
  JINT MCU_sample_width;	/* MCU width in samples: MCU_width * DCT_h_scaled_size */
  JINT last_col_width;		/* # of non-dummy blocks across in last MCU */
  JINT last_row_height;		/* # of non-dummy blocks down in last MCU */

  /* Saved quantization table for component; NULL if none yet saved.
   * See jdinput.c comments about the need for this information.
   * This field is currently used only for decompression.
   */
  JQUANT_TBL * quant_table;

  /* Private per-component storage for DCT or IDCT subsystem. */
  void * dct_table;
} jpeg_component_info;


/* The script for encoding a multiple-scan file is an array of these: */

typedef struct {
  JINT comps_in_scan;		/* number of components encoded in this scan */
  JINT component_index[MAX_COMPS_IN_SCAN]; /* their SOF/comp_info[] indexes */
  JINT Ss;
  JINT Se;			/* progressive JPEG spectral selection parms */
  JINT Ah;
  JINT Al;			/* progressive JPEG successive approx. parms */
} jpeg_scan_info;

/* Known color spaces. */

typedef enum {
	JCS_UNKNOWN,		/* error/unspecified */
	JCS_GRAYSCALE,		/* monochrome */
	JCS_RGB,		/* red/green/blue, standard RGB (sRGB) */
	JCS_YCbCr,		/* Y/Cb/Cr (also known as YUV), standard YCC */
	JCS_CMYK,		/* C/M/Y/K */
	JCS_YCCK,		/* Y/Cb/Cr/K */
	JCS_BG_RGB,		/* big gamut red/green/blue, bg-sRGB */
	JCS_BG_YCC		/* big gamut Y/Cb/Cr, bg-sYCC */
} J_COLOR_SPACE;

// Abcouwer JSC 2021 - remove support for subtract green / color transform
// Abcouwer JSC 2021 - remove support for dct other than float


/* Dithering options for decompression. */

typedef enum {
	JDITHER_NONE,		/* no dithering */
	JDITHER_ORDERED,	/* simple ordered dither */
	JDITHER_FS		/* Floyd-Steinberg error diffusion dither */
} J_DITHER_MODE;


/* Common fields between JPEG compression and decompression master structs. */

#define jpeg_common_fields \
  struct jpeg_memory_mgr * mem;	/* Memory manager module */\
  struct jpeg_progress_mgr * progress; /* Progress monitor, or NULL if none */\
  struct jpeg_static_memory * statmem; /* Abcouwer JSC 2021 - new for static mem */ \
  void * client_data;		/* Available for use by application */\
  boolean is_decompressor;	/* So common code can tell which is which */\
  JINT global_state;		/* For checking call sequence validity */ \
  JINT trace_level     /* max msg_level that will be displayed */

//  struct jpeg_error_mgr * err;  /* Error handler module */


/* Routines that are to be used by both halves of the library are declared
 * to receive a pointer to this structure.  There are no actual instances of
 * jpeg_common_struct, only of jpeg_compress_struct and jpeg_decompress_struct.
 */
struct jpeg_common_struct {
  jpeg_common_fields;		/* Fields common to both master struct types */
  /* Additional fields follow in an actual jpeg_compress_struct or
   * jpeg_decompress_struct.  All three structs must agree on these
   * initial fields!  (This would be a lot cleaner in C++.)
   */
};

typedef struct jpeg_common_struct * j_common_ptr;
typedef struct jpeg_compress_struct * j_compress_ptr;
typedef struct jpeg_decompress_struct * j_decompress_ptr;


/* Master record for a compression instance */

struct jpeg_compress_struct {
  jpeg_common_fields;		/* Fields shared with jpeg_decompress_struct */

  /* Destination for compressed data */
  struct jpeg_destination_mgr * dest;

  /* Description of source image --- these fields must be filled in by
   * outer application before starting compression.  in_color_space must
   * be correct before you can even call jpeg_set_defaults().
   */

  JDIMENSION image_width;	/* input image width */
  JDIMENSION image_height;	/* input image height */
  JINT input_components;		/* # of color components in input image */
  J_COLOR_SPACE in_color_space;	/* colorspace of input image */

  F64 input_gamma;		/* image gamma of input image */

  /* Compression parameters --- these fields must be set before calling
   * jpeg_start_compress().  We recommend calling jpeg_set_defaults() to
   * initialize everything to reasonable defaults, then changing anything
   * the application specifically wants to change.  That way you won't get
   * burnt when new parameters are added.  Also note that there are several
   * helper routines to simplify changing parameters.
   */

  JUINT scale_num;   /* fraction by which to scale image */
  JUINT scale_denom; /* fraction by which to scale image */

  JDIMENSION jpeg_width;	/* scaled JPEG image width */
  JDIMENSION jpeg_height;	/* scaled JPEG image height */
  /* Dimensions of actual JPEG image that will be written to file,
   * derived from input dimensions by scaling factors above.
   * These fields are computed by jpeg_start_compress().
   * You can also use jpeg_calc_jpeg_dimensions() to determine these values
   * in advance of calling jpeg_start_compress().
   */

  JINT data_precision;		/* bits of precision in image data */

  JINT num_components;		/* # of color components in JPEG image */
  J_COLOR_SPACE jpeg_color_space; /* colorspace of JPEG image */

  jpeg_component_info * comp_info;
  /* comp_info[i] describes component that appears i'th in SOF */

  JQUANT_TBL * quant_tbl_ptrs[NUM_QUANT_TBLS];
  JINT q_scale_factor[NUM_QUANT_TBLS];
  /* ptrs to coefficient quantization tables, or NULL if not defined,
   * and corresponding scale factors (percentage, initialized 100).
   */

  JHUFF_TBL * dc_huff_tbl_ptrs[NUM_HUFF_TBLS];
  JHUFF_TBL * ac_huff_tbl_ptrs[NUM_HUFF_TBLS];
  /* ptrs to Huffman coding tables, or NULL if not defined */

  UINT8 arith_dc_L[NUM_ARITH_TBLS]; /* L values for DC arith-coding tables */
  UINT8 arith_dc_U[NUM_ARITH_TBLS]; /* U values for DC arith-coding tables */
  UINT8 arith_ac_K[NUM_ARITH_TBLS]; /* Kx values for AC arith-coding tables */

  JINT num_scans;		/* # of entries in scan_info array */
  const jpeg_scan_info * scan_info; /* script for multi-scan file, or NULL */
  /* The default value of scan_info is NULL, which causes a single-scan
   * sequential JPEG file to be emitted.  To create a multi-scan file,
   * set num_scans and scan_info to point to an array of scan definitions.
   */

  // Abcouwer JSC 2021 - reduce options 0 no arithmetic encoding,
  // no optimized entropy encoding, no raw data
  // boolean raw_data_in;		/* TRUE=caller supplies downsampled data */
  // boolean arith_code;		/* TRUE=arithmetic coding, FALSE=Huffman */
  //  boolean optimize_coding;	/* TRUE=optimize entropy encoding parms */
  //boolean CCIR601_sampling;	/* TRUE=first samples are cosited */
  boolean do_fancy_downsampling; /* TRUE=apply fancy downsampling */
  JINT smoothing_factor;		/* 1..100, or 0 for no input smoothing */

  // Abcouwer JSC 2021 - no dct_method member - only float dct supported
 // J_DCT_METHOD dct_method;	/* DCT algorithm selector */

  /* The restart interval can be specified in absolute MCUs by setting
   * restart_interval, or in MCU rows by setting restart_in_rows
   * (in which case the correct restart_interval will be figured
   * for each scan).
   */
  JUINT restart_interval; /* MCUs per restart, or 0 for no restart */
  JINT restart_in_rows;		/* if > 0, MCU rows per restart interval */

  /* Parameters controlling emission of special markers. */

  boolean write_JFIF_header;	/* should a JFIF marker be written? */
  UINT8 JFIF_major_version;	/* What to write for the JFIF version number */
  UINT8 JFIF_minor_version;
  /* These three values are not used by the JPEG code, merely copied */
  /* into the JFIF APP0 marker.  density_unit can be 0 for unknown, */
  /* 1 for dots/inch, or 2 for dots/cm.  Note that the pixel aspect */
  /* ratio is defined by X_density/Y_density even when density_unit=0. */
  UINT8 density_unit;		/* JFIF code for pixel size units */
  UINT16 X_density;		/* Horizontal pixel density */
  UINT16 Y_density;		/* Vertical pixel density */
  boolean write_Adobe_marker;	/* should an Adobe marker be written? */

  // Abcouwer JSC 2021 - remove support for subtract green / color transform
//  J_COLOR_TRANSFORM color_transform;
  /* Color transform identifier, writes LSE marker if nonzero */

  /* State variable: index of next scanline to be written to
   * jpeg_write_scanlines().  Application may use this to control its
   * processing loop, e.g., "while (next_scanline < image_height)".
   */

  JDIMENSION next_scanline;	/* 0 .. image_height-1  */

  /* Remaining fields are known throughout compressor, but generally
   * should not be touched by a surrounding application.
   */

  /*
   * These fields are computed during compression startup
   */
  // Abcouwer JSC 2021 - remove progressive
  //  boolean progressive_mode;	/* TRUE if scan script uses progressive mode */
  JINT max_h_samp_factor;	/* largest h_samp_factor */
  JINT max_v_samp_factor;	/* largest v_samp_factor */

  JINT min_DCT_h_scaled_size;	/* smallest DCT_h_scaled_size of any component */
  JINT min_DCT_v_scaled_size;	/* smallest DCT_v_scaled_size of any component */

  JDIMENSION total_iMCU_rows;	/* # of iMCU rows to be input to coef ctlr */
  /* The coefficient controller receives data in units of MCU rows as defined
   * for fully interleaved scans (whether the JPEG file is interleaved or not).
   * There are v_samp_factor * DCT_v_scaled_size sample rows of each component
   * in an "iMCU" (interleaved MCU) row.
   */

  /*
   * These fields are valid during any one scan.
   * They describe the components and MCUs actually appearing in the scan.
   */
  JINT comps_in_scan;		/* # of JPEG components in this scan */
  jpeg_component_info * cur_comp_info[MAX_COMPS_IN_SCAN];
  /* *cur_comp_info[i] describes component that appears i'th in SOS */

  JDIMENSION MCUs_per_row;	/* # of MCUs across the image */
  JDIMENSION MCU_rows_in_scan;	/* # of MCU rows in the image */

  JINT blocks_in_MCU;		/* # of DCT blocks per MCU */
  JINT MCU_membership[C_MAX_BLOCKS_IN_MCU];
  /* MCU_membership[i] is index in cur_comp_info of component owning */
  /* i'th block in an MCU */

  /* progressive JPEG parameters for scan */
  JINT Ss;
  JINT Se;
  JINT Ah;
  JINT Al;

  JINT block_size;		/* the basic DCT block size: 1..16 */
  const JINT * natural_order;	/* natural-order position array */
  JINT lim_Se;			/* min( Se, DCTSIZE2-1 ) */

  /*
   * Links to compression subobjects (methods and private variables of modules)
   */
  struct jpeg_comp_master * master;
  struct jpeg_c_main_controller * main;
  struct jpeg_c_prep_controller * prep;
  struct jpeg_c_coef_controller * coef;
  struct jpeg_marker_writer * marker;
  struct jpeg_color_converter * cconvert;
  struct jpeg_downsampler * downsample;
  struct jpeg_forward_dct * fdct;
  struct jpeg_entropy_encoder * entropy;
  jpeg_scan_info * script_space; /* workspace for jpeg_simple_progression */
  JINT script_space_size;
};


/* Master record for a decompression instance */

struct jpeg_decompress_struct {
  jpeg_common_fields;		/* Fields shared with jpeg_compress_struct */

  /* Source of compressed data */
  struct jpeg_source_mgr * src;

  /* Basic description of image --- filled in by jpeg_read_header(). */
  /* Application may inspect these values to decide how to process image. */

  JDIMENSION image_width;	/* nominal image width (from SOF marker) */
  JDIMENSION image_height;	/* nominal image height */
  JINT num_components;		/* # of color components in JPEG image */
  J_COLOR_SPACE jpeg_color_space; /* colorspace of JPEG image */

  /* Decompression processing parameters --- these fields must be set before
   * calling jpeg_start_decompress().  Note that jpeg_read_header() initializes
   * them to default values.
   */

  J_COLOR_SPACE out_color_space; /* colorspace for output */

  JUINT scale_num, scale_denom; /* fraction by which to scale image */

  F64 output_gamma;		/* image gamma wanted in output */

  boolean buffered_image;	/* TRUE=multiple output passes */
  boolean raw_data_out;		/* TRUE=downsampled data wanted */

  // Abcouwer JSC 2021 - no dct_method member - only float dct supported
//  J_DCT_METHOD dct_method;	/* IDCT algorithm selector */

  boolean do_fancy_upsampling;	/* TRUE=apply fancy upsampling */
  boolean do_block_smoothing;	/* TRUE=apply interblock smoothing */

  // Abcouwer ZSC 2021 - remove requesting of colormapped output

  /* Description of actual output image that will be returned to application.
   * These fields are computed by jpeg_start_decompress().
   * You can also use jpeg_calc_output_dimensions() to determine these values
   * in advance of calling jpeg_start_decompress().
   */

  JDIMENSION output_width;	/* scaled image width */
  JDIMENSION output_height;	/* scaled image height */
  JINT out_color_components;	/* # of color components in out_color_space */
  JINT output_components;	/* # of color components returned */
  /* output_components is 1 (a colormap index) when quantizing colors;
   * otherwise it equals out_color_components.
   */
  JINT rec_outbuf_height;	/* min recommended height of scanline buffer */
  /* If the buffer passed to jpeg_read_scanlines() is less than this many rows
   * high, space and time will be wasted due to unnecessary data copying.
   * Usually rec_outbuf_height will be 1 or 2, at most 4.
   */

  // Abcouwer JSC - remove colormap

  /* State variables: these variables indicate the progress of decompression.
   * The application may examine these but must not modify them.
   */

  /* Row index of next scanline to be read from jpeg_read_scanlines().
   * Application may use this to control its processing loop, e.g.,
   * "while (output_scanline < output_height)".
   */
  JDIMENSION output_scanline;	/* 0 .. output_height-1  */

  /* Current input scan number and number of iMCU rows completed in scan.
   * These indicate the progress of the decompressor input side.
   */
  JINT input_scan_number;	/* Number of SOS markers seen so far */
  JDIMENSION input_iMCU_row;	/* Number of iMCU rows completed */

  /* The "output scan number" is the notional scan being displayed by the
   * output side.  The decompressor will not allow output scan/row number
   * to get ahead of input scan/row, but it can fall arbitrarily far behind.
   */
  JINT output_scan_number;	/* Nominal scan number being displayed */
  JDIMENSION output_iMCU_row;	/* Number of iMCU rows read */

  /* Current progression status.  coef_bits[c][i] indicates the precision
   * with which component c's DCT coefficient i (in zigzag order) is known.
   * It is -1 when no data has yet been received, otherwise it is the point
   * transform (shift) value for the most recent scan of the coefficient
   * (thus, 0 at completion of the progression).
   * This pointer is NULL when reading a non-progressive file.
   */
  JINT (*coef_bits)[DCTSIZE2];	/* -1 or current Al value for each coef */

  /* Internal JPEG parameters --- the application usually need not look at
   * these fields.  Note that the decompressor output side may not use
   * any parameters that can change between scans.
   */

  /* Quantization and Huffman tables are carried forward across input
   * datastreams when processing abbreviated JPEG datastreams.
   */

  JQUANT_TBL * quant_tbl_ptrs[NUM_QUANT_TBLS];
  /* ptrs to coefficient quantization tables, or NULL if not defined */

  JHUFF_TBL * dc_huff_tbl_ptrs[NUM_HUFF_TBLS];
  JHUFF_TBL * ac_huff_tbl_ptrs[NUM_HUFF_TBLS];
  /* ptrs to Huffman coding tables, or NULL if not defined */

  /* These parameters are never carried across datastreams, since they
   * are given in SOF/SOS markers or defined to be reset by SOI.
   */

  JINT data_precision;		/* bits of precision in image data */

  jpeg_component_info * comp_info;
  /* comp_info[i] describes component that appears i'th in SOF */

  boolean is_baseline;		/* TRUE if Baseline SOF0 encountered */
  // Abcouwer ZSC 2021 - remove progressive, arithmetic coding
  // boolean progressive_mode;	/* TRUE if SOFn specifies progressive mode */
  //boolean arith_code;		/* TRUE=arithmetic coding, FALSE=Huffman */

  UINT8 arith_dc_L[NUM_ARITH_TBLS]; /* L values for DC arith-coding tables */
  UINT8 arith_dc_U[NUM_ARITH_TBLS]; /* U values for DC arith-coding tables */
  UINT8 arith_ac_K[NUM_ARITH_TBLS]; /* Kx values for AC arith-coding tables */

  JUINT restart_interval; /* MCUs per restart interval, or 0 for no restart */

  /* These fields record data obtained from optional markers recognized by
   * the JPEG library.
   */
  boolean saw_JFIF_marker;	/* TRUE iff a JFIF APP0 marker was found */
  /* Data copied from JFIF marker; only valid if saw_JFIF_marker is TRUE: */
  UINT8 JFIF_major_version;	/* JFIF version number */
  UINT8 JFIF_minor_version;
  UINT8 density_unit;		/* JFIF code for pixel size units */
  UINT16 X_density;		/* Horizontal pixel density */
  UINT16 Y_density;		/* Vertical pixel density */
  boolean saw_Adobe_marker;	/* TRUE iff an Adobe APP14 marker was found */
  UINT8 Adobe_transform;	/* Color transform code from Adobe marker */

  // Abcouwer JSC 2021 - remove support for subtract green / color transform
//  J_COLOR_TRANSFORM color_transform;
  /* Color transform identifier derived from LSE marker, otherwise zero */

  // Abcouwer JSC 2021 - no CCIR601_sampling
  //boolean CCIR601_sampling;	/* TRUE=first samples are cosited */

  /* Aside from the specific data retained from APPn markers known to the
   * library, the uninterpreted contents of any or all APPn and COM markers
   * can be saved in a list for examination by the application.
   */
  // Abcouwer JSC 2021 - remove marker list
  //jpeg_saved_marker_ptr marker_list; /* Head of list of saved markers */

  /* Remaining fields are known throughout decompressor, but generally
   * should not be touched by a surrounding application.
   */

  /*
   * These fields are computed during decompression startup
   */
  JINT max_h_samp_factor;	/* largest h_samp_factor */
  JINT max_v_samp_factor;	/* largest v_samp_factor */

  JINT min_DCT_h_scaled_size;	/* smallest DCT_h_scaled_size of any component */
  JINT min_DCT_v_scaled_size;	/* smallest DCT_v_scaled_size of any component */

  JDIMENSION total_iMCU_rows;	/* # of iMCU rows in image */
  /* The coefficient controller's input and output progress is measured in
   * units of "iMCU" (interleaved MCU) rows.  These are the same as MCU rows
   * in fully interleaved JPEG scans, but are used whether the scan is
   * interleaved or not.  We define an iMCU row as v_samp_factor DCT block
   * rows of each component.  Therefore, the IDCT output contains
   * v_samp_factor * DCT_v_scaled_size sample rows of a component per iMCU row.
   */

  JSAMPLE * sample_range_limit; /* table for fast range-limiting */

  /*
   * These fields are valid during any one scan.
   * They describe the components and MCUs actually appearing in the scan.
   * Note that the decompressor output side must not use these fields.
   */
  JINT comps_in_scan;		/* # of JPEG components in this scan */
  jpeg_component_info * cur_comp_info[MAX_COMPS_IN_SCAN];
  /* *cur_comp_info[i] describes component that appears i'th in SOS */

  JDIMENSION MCUs_per_row;	/* # of MCUs across the image */
  JDIMENSION MCU_rows_in_scan;	/* # of MCU rows in the image */

  JINT blocks_in_MCU;		/* # of DCT blocks per MCU */
  JINT MCU_membership[D_MAX_BLOCKS_IN_MCU];
  /* MCU_membership[i] is index in cur_comp_info of component owning */
  /* i'th block in an MCU */

  /* progressive JPEG parameters for scan */
  JINT Ss;
  JINT Se;
  JINT Ah;
  JINT Al;

  /* These fields are derived from Se of first SOS marker.
   */
  JINT block_size;		/* the basic DCT block size: 1..16 */
  const JINT * natural_order; /* natural-order position array for entropy decode */
  JINT lim_Se;			/* min( Se, DCTSIZE2-1 ) for entropy decode */

  /* This field is shared between entropy decoder and marker parser.
   * It is either zero or the code of a JPEG marker that has been
   * read from the data source, but has not yet been processed.
   */
  JINT unread_marker;

  /*
   * Links to decompression subobjects (methods, private variables of modules)
   */
  struct jpeg_decomp_master * master;
  struct jpeg_d_main_controller * main;
  struct jpeg_d_coef_controller * coef;
  struct jpeg_d_post_controller * post;
  struct jpeg_input_controller * inputctl;
  struct jpeg_marker_reader * marker;
  struct jpeg_entropy_decoder * entropy;
  struct jpeg_inverse_dct * idct;
  struct jpeg_upsampler * upsample;
  struct jpeg_color_deconverter * cconvert;
  struct jpeg_color_quantizer * cquantize;
};


/* "Object" declarations for JPEG modules that may be supplied or called
 * directly by the surrounding application.
 * As with all objects in the JPEG library, these structs only define the
 * publicly visible methods and state variables of a module.  Additional
 * private fields may exist after the public ones.
 */

// ABcouwer JSC 2021 - remove error handler

/* Progress monitor object */

struct jpeg_progress_mgr {
  JMETHOD(void, progress_monitor, (j_common_ptr cinfo));

  JLONG pass_counter;		/* work units completed in this pass */
  JLONG pass_limit;		/* total number of work units in this pass */
  JINT completed_passes;		/* passes completed so far */
  JINT total_passes;		/* total number of passes expected */
};


/* Data destination object for compression */

struct jpeg_destination_mgr {
  JOCTET * next_output_byte;	/* => next byte to write in buffer */
  JSIZE free_in_buffer;	/* # of byte spaces remaining in buffer */

  JMETHOD(void, init_destination, (j_compress_ptr cinfo));
  JMETHOD(boolean, empty_output_buffer, (j_compress_ptr cinfo));
  JMETHOD(void, term_destination, (j_compress_ptr cinfo));
};


/* Data source object for decompression */

struct jpeg_source_mgr {
  const JOCTET * next_input_byte; /* => next byte to read from buffer */
  JSIZE bytes_in_buffer;	/* # of bytes remaining in buffer */

  JMETHOD(void, init_source, (j_decompress_ptr cinfo));
  JMETHOD(boolean, fill_input_buffer, (j_decompress_ptr cinfo));
  JMETHOD(void, skip_input_data, (j_decompress_ptr cinfo, JLONG num_bytes));
  JMETHOD(boolean, resync_to_restart, (j_decompress_ptr cinfo, JINT desired));
  JMETHOD(void, term_source, (j_decompress_ptr cinfo));
};


/* Memory manager object.
 * Allocates "small" objects (a few K total), "large" objects (tens of K),
 * and "really big" objects (virtual arrays with backing store if needed).
 * The memory manager does not allow individual objects to be freed; rather,
 * each created object is assigned to a pool, and whole pools can be freed
 * at once.  This is faster and more convenient than remembering exactly what
 * to free, especially where malloc()/free() are not too speedy.
 * NB: alloc routines never return NULL.  They exit to error_exit if not
 * successful.
 */

#define JPOOL_PERMANENT	0	/* lasts until master record is destroyed */
#define JPOOL_IMAGE	1	/* lasts until done with image/datastream */
#define JPOOL_NUMPOOLS	2

// Abcouwer JSC 2021 - remove virtual arrays

struct jpeg_memory_mgr {
  /* Method pointers */
  JMETHOD(JSIZE, get_mem_size, (j_common_ptr cinfo));
  JMETHOD(void *, get_mem, (j_common_ptr cinfo, JINT pool_id,
				JSIZE sizeofobject));
//  JMETHOD(void FAR *, alloc_large, (j_common_ptr cinfo, JINT pool_id,
//				     JSIZE sizeofobject));
  JMETHOD(JSAMPARRAY, get_sarray, (j_common_ptr cinfo, JINT pool_id,
				     JDIMENSION samplesperrow,
				     JDIMENSION numrows));
  // Abcouwer JSC 2021 - remove most array functions
  // Abcouwer JSC 2021 - remove free, self destruct

};

// Variables added for static memory manager - Abcouwer JSC 2021
struct jpeg_static_memory {
    void *buffer; // memory buffer
    JSIZE buffer_size_bytes; // size of memory buffer
    JSIZE bytes_used; // number of bytes doled out
    JSIZE bytes_free; // number of bytes remaining
};

/* Routine signature for application-supplied marker processing methods.
 * Need not pass marker code since it is stored in cinfo->unread_marker.
 */
typedef JMETHOD(boolean, jpeg_marker_parser_method, (j_decompress_ptr cinfo));


/* Declarations for routines called by application.
 * The JPP macro hides prototype parameters from compilers that can't cope.
 * Note JPP requires double parentheses.
 */

#define JPP(arglist)	arglist


// Abcouwer JSC 2021 - remove support for short names

// function name mangling guard
#ifdef __cplusplus
extern "C" {
#endif

// Abcouwer JSC 2021 - remove error manager




// Abcouwer JSC 2021
// Provide buffer of memory for static memory allocation
JSC_EXTERN(struct jpeg_static_memory *) jpeg_give_static_mem
    JPP((struct jpeg_static_memory *statmem,
                void * buffer, JSIZE buffer_size_bytes));


/* Initialization of JPEG compression objects.
 * jpeg_create_compress() and jpeg_create_decompress() are the exported
 * names that applications should call.  These expand to calls on
 * jpeg_CreateCompress and jpeg_CreateDecompress with additional information
 * passed for version mismatch checking.
 * NB: you must set up the error-manager BEFORE calling jpeg_create_xxx.
 */
JSC_EXTERN(void) jpeg_CreateCompress JPP((j_compress_ptr cinfo,
				      JINT version, JSIZE structsize));
JSC_EXTERN(void) jpeg_CreateDecompress JPP((j_decompress_ptr cinfo,
					JINT version, JSIZE structsize));
/* Destruction of JPEG compression objects */
JSC_EXTERN(void) jpeg_destroy_compress JPP((j_compress_ptr cinfo));
JSC_EXTERN(void) jpeg_destroy_decompress JPP((j_decompress_ptr cinfo));

// Abcouwer JSC 2021 - no stdio

/* Data source and destination managers: memory buffers. */
JSC_EXTERN(void) jpeg_mem_dest JPP((j_compress_ptr cinfo,
			       U8 ** outbuffer,
			       JSIZE * outsize));
JSC_EXTERN(void) jpeg_mem_src JPP((j_decompress_ptr cinfo,
			      const U8 * inbuffer,
			      JSIZE insize));

/* Default parameter setup for compression */
JSC_EXTERN(void) jpeg_set_defaults JPP((j_compress_ptr cinfo));
/* Compression parameter setup aids */
JSC_EXTERN(void) jpeg_set_colorspace JPP((j_compress_ptr cinfo,
				      J_COLOR_SPACE colorspace));
JSC_EXTERN(void) jpeg_default_colorspace JPP((j_compress_ptr cinfo));
JSC_EXTERN(void) jpeg_set_quality JPP((j_compress_ptr cinfo, JINT quality,
				   boolean force_baseline));
// Abcouwer JSC 2021 - remove knobs
JSC_EXTERN(void) jpeg_suppress_tables JPP((j_compress_ptr cinfo,
				       boolean suppress));
JSC_EXTERN(JQUANT_TBL *) jpeg_get_mem_quant_table JPP((j_common_ptr cinfo));
JSC_EXTERN(JHUFF_TBL *) jpeg_get_mem_huff_table JPP((j_common_ptr cinfo));
JSC_EXTERN(JHUFF_TBL *) jpeg_std_huff_table JPP((j_common_ptr cinfo,
					     boolean isDC, JINT tblno));

/* Main entry points for compression */
JSC_EXTERN(void) jpeg_start_compress JPP((j_compress_ptr cinfo,
				      boolean write_all_tables));
JSC_EXTERN(JDIMENSION) jpeg_write_scanlines JPP((j_compress_ptr cinfo,
					     JSAMPARRAY scanlines,
					     JDIMENSION num_lines));
JSC_EXTERN(void) jpeg_finish_compress JPP((j_compress_ptr cinfo));

/* Precalculate JPEG dimensions for current compression parameters. */
JSC_EXTERN(void) jpeg_calc_jpeg_dimensions JPP((j_compress_ptr cinfo));

/* Write a special marker.  See libjpeg.txt concerning safe usage. */
JSC_EXTERN(void) jpeg_write_marker
	JPP((j_compress_ptr cinfo, JINT marker,
	     const JOCTET * dataptr, JUINT datalen));
/* Same, but piecemeal. */
JSC_EXTERN(void) jpeg_write_m_header
	JPP((j_compress_ptr cinfo, JINT marker, JUINT datalen));
JSC_EXTERN(void) jpeg_write_m_byte
	JPP((j_compress_ptr cinfo, JINT val));

// Abcouwer JSC 2021 - remove write_tables

/* Decompression startup: read start of JPEG datastream to see what's there */
JSC_EXTERN(JINT) jpeg_read_header JPP((j_decompress_ptr cinfo,
				  boolean require_image));
/* Return value is one of: */
// JPEG_SUSPENDED		0 /* Suspended due to lack of input data */
// JPEG_HEADER_OK		1 /* Found valid image datastream */
// JPEG_HEADER_TABLES_ONLY	2 /* Found valid table-specs-only datastream */
/* If you pass require_image = TRUE (normal case), you need not check for
 * a TABLES_ONLY return code; an abbreviated file will cause an error exit.
 * JPEG_SUSPENDED is only possible if you use a data source module that can
 * give a suspension return (the stdio source module doesn't).
 */

/* Main entry points for decompression */
JSC_EXTERN(boolean) jpeg_start_decompress JPP((j_decompress_ptr cinfo));
JSC_EXTERN(JDIMENSION) jpeg_read_scanlines JPP((j_decompress_ptr cinfo,
					    JSAMPARRAY scanlines,
					    JDIMENSION max_lines));
JSC_EXTERN(boolean) jpeg_finish_decompress JPP((j_decompress_ptr cinfo));

// Abcouwer JSC 2021 - remove reading of raw data

/* Additional entry points for buffered-image mode. */
JSC_EXTERN(boolean) jpeg_has_multiple_scans JPP((j_decompress_ptr cinfo));
JSC_EXTERN(boolean) jpeg_input_complete JPP((j_decompress_ptr cinfo));
// Abcouwer JSC 2021 - jpeg_consume_input moved private

/* Precalculate output dimensions for current decompression parameters. */
JSC_EXTERN(void) jpeg_core_output_dimensions JPP((j_decompress_ptr cinfo));
JSC_EXTERN(void) jpeg_calc_output_dimensions JPP((j_decompress_ptr cinfo));

// Abcouwer JSC 2021 - no marker saving support

/* Install a special processing method for COM or APPn markers. */
JSC_EXTERN(void) jpeg_set_marker_processor
	JPP((j_decompress_ptr cinfo, JINT marker_code,
	     jpeg_marker_parser_method routine));

// Abcouwer JSC 2021 - remove virtual arrays, transcoding
// Abcouwer JSC 2021 - don't allow aborting, only destroying.

/* Generic versions of jpeg_abort and jpeg_destroy that work on either
 * flavor of JPEG object.  These may be more convenient in some places.
 */
JSC_EXTERN(void) jpeg_abort JPP((j_common_ptr cinfo));
JSC_EXTERN(void) jpeg_destroy JPP((j_common_ptr cinfo));

/* Default restart-marker-resync procedure for use by data source modules */
JSC_EXTERN(boolean) jpeg_resync_to_restart JPP((j_decompress_ptr cinfo,
                JINT desired));


// Abcouwer JSC 2021 - remove short name support


#ifdef __cplusplus
}
#endif

#endif /* JPEGLIB_H */
