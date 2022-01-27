/*
 * jdhuff.c
 *
 * Copyright (C) 1991-1997, Thomas G. Lane.
 * Modified 2006-2019 by Guido Vollbeding.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
 *
 * This file contains Huffman entropy decoding routines.
 * Both sequential and progressive modes are supported in this single module.
 *
 * Much of the complexity here has to do with supporting input suspension.
 * If the data source module demands suspension, we want to be able to back
 * up to the start of the current MCU.  To do this, we copy state variables
 * into local working storage, and update them back to the permanent
 * storage only upon successful completion of an MCU.
 */

#include "jsc/jpegint.h"

/* Derived data constructed for each Huffman table */

#define HUFF_LOOKAHEAD        8        /* # of bits of lookahead */

#define BIT_BUF_SIZE  32        /* size of buffer in bits */

// Abcouwer JSC 2021 - remove BITREAD macros - each only used once

/*
 * These macros provide the in-line portion of bit fetching.
 * Use CHECK_BIT_BUFFER to ensure there are N bits in get_buffer
 * before using GET_BITS, PEEK_BITS, or DROP_BITS.
 * The variables get_buffer and bits_left are assumed to be locals,
 * but the state struct might not be (jpeg_huff_decode needs this).
 *        CHECK_BIT_BUFFER(state,n,action);
 *                Ensure there are N bits in get_buffer; if suspend, take action.
 *      val = GET_BITS(n);
 *                Fetch next N bits.
 *      val = PEEK_BITS(n);
 *                Fetch next N bits without removing them from the buffer.
 *        DROP_BITS(n);
 *                Discard next N bits.
 * The value N should be a simple variable, not an expression, because it
 * is evaluated multiple times.
 */

#define CHECK_BIT_BUFFER(state,nbits,action) \
        { if (bits_left < (nbits)) {  \
            if (! jpeg_fill_bit_buffer(&(state),get_buffer,bits_left,(nbits)))  \
              { action; }  \
            get_buffer = (state).get_buffer; bits_left = (state).bits_left; } }

#define GET_BITS(nbits) \
        (((JINT) (get_buffer >> (bits_left -= (nbits)))) & BIT_MASK(nbits))

#define PEEK_BITS(nbits) \
        (((JINT) (get_buffer >> (bits_left -  (nbits)))) & BIT_MASK(nbits))

#define DROP_BITS(nbits) \
        (bits_left -= (nbits))


/*
 * Code for extracting next Huffman-coded symbol from input bit stream.
 * Again, this is time-critical and we make the main paths be macros.
 *
 * We use a lookahead table to process codes of up to HUFF_LOOKAHEAD bits
 * without looping.  Usually, more than 95% of the Huffman codes will be 8
 * or fewer bits long.  The few overlength codes are handled with a loop,
 * which need not be inline code.
 *
 * Notes about the HUFF_DECODE macro:
 * 1. Near the end of the data segment, we may fail to get enough bits
 *    for a lookahead.  In that case, we do it the hard way.
 * 2. If the lookahead table contains no entry, the next code must be
 *    more than HUFF_LOOKAHEAD bits long.
 * 3. jpeg_huff_decode returns -1 if forced to suspend.
 */

#define HUFF_DECODE(result,state,htbl,failaction,slowlabel) \
{ register JINT nb, look; \
  if (bits_left < HUFF_LOOKAHEAD) { \
    if (! jpeg_fill_bit_buffer(&(state),get_buffer,bits_left, 0)) {failaction;} \
    get_buffer = (state).get_buffer; bits_left = (state).bits_left; \
    if (bits_left < HUFF_LOOKAHEAD) { \
      nb = 1; goto slowlabel; \
    } \
  } \
  look = PEEK_BITS(HUFF_LOOKAHEAD); \
  if ((nb = (htbl)->look_nbits[look]) != 0) { \
    DROP_BITS(nb); \
    (result) = (htbl)->look_sym[look]; \
  } else { \
    nb = HUFF_LOOKAHEAD+1; \
slowlabel: \
    if (((result)=jpeg_huff_decode(&(state),get_buffer,bits_left,(htbl),nb)) < 0) \
        { failaction; } \
    get_buffer = (state).get_buffer; bits_left = (state).bits_left; \
  } \
}

///* This macro is to work around compilers with missing or broken
// * structure assignment.  You'll need to fix this code if you have
// * such a compiler and you change MAX_COMPS_IN_SCAN.
// */
//
//// Abcouwer JSC 2021 - remove support for alternative assign_state
//#define ASSIGN_STATE(dest,src)  ((dest) = (src))

// Abcouwer JSC 2021 - remove conditional compilation
#define MIN_GET_BITS  (BIT_BUF_SIZE-7)

typedef struct {
  /* Basic tables: (element [0] of each array is unused) */
  INT32 maxcode[18];                /* largest code of length k (-1 if none) */
  /* (maxcode[17] is a sentinel to ensure jpeg_huff_decode terminates) */
  INT32 valoffset[17];                /* huffval[] offset for codes of length k */
  /* valoffset[k] = huffval[] index of 1st symbol of code length k, less
   * the smallest code of length k; so given a code of length k, the
   * corresponding symbol is huffval[code + valoffset[k]]
   */

  /* Link to public Huffman table (needed only in jpeg_huff_decode) */
  JHUFF_TBL *pub;

  /* Lookahead tables: indexed by the next HUFF_LOOKAHEAD bits of
   * the input data stream.  If the next Huffman code is no more
   * than HUFF_LOOKAHEAD bits long, we can obtain its length and
   * the corresponding symbol directly from these tables.
   */
  JINT look_nbits[1<<HUFF_LOOKAHEAD]; /* # bits, or 0 if too long */
  UINT8 look_sym[1<<HUFF_LOOKAHEAD]; /* symbol, or unused */
} d_derived_tbl;


/*
 * Fetching the next N bits from the input stream is a time-critical operation
 * for the Huffman decoders.  We implement it with a combination of inline
 * macros and out-of-line subroutines.  Note that N (the number of bits
 * demanded at one time) never exceeds 15 for JPEG use.
 *
 * We read source bytes into get_buffer and dole out bits as needed.
 * If get_buffer already contains enough bits, they are fetched in-line
 * by the macros CHECK_BIT_BUFFER and GET_BITS.  When there aren't enough
 * bits, jpeg_fill_bit_buffer is called; it will attempt to fill get_buffer
 * as full as possible (not just to the number of bits needed; this
 * prefetching reduces the overhead cost of calling jpeg_fill_bit_buffer).
 * Note that jpeg_fill_bit_buffer may return FALSE to indicate suspension.
 * On TRUE return, jpeg_fill_bit_buffer guarantees that get_buffer contains
 * at least the requested number of bits --- dummy zeroes are inserted if
 * necessary.
 */

typedef INT32 bit_buf_type;        /* type of bit-extraction buffer */

/* If long is > 32 bits on your machine, and shifting/masking longs is
 * reasonably fast, making bit_buf_type be long and setting BIT_BUF_SIZE
 * appropriately should be a win.  Unfortunately we can't define the size
 * with something like  #define BIT_BUF_SIZE (sizeof(bit_buf_type)*8)
 * because not all machines measure sizeof in 8-bit bytes.
 */

typedef struct {                /* Bitreading state saved across MCUs */
  bit_buf_type get_buffer;        /* current bit-extraction buffer */
  JINT bits_left;                /* number of unused bits in it */
} bitread_perm_state;

typedef struct {                /* Bitreading working state within an MCU */
  /* Current data source location */
  /* We need a copy, rather than munging the original, in case of suspension */
  const JOCTET * next_input_byte; /* next byte to read from source */
  JSIZE bytes_in_buffer;        /* number of bytes remaining in source buffer */
  /* Bit input buffer --- note these values are kept in register variables,
   * not in this struct, inside the inner loops.
   */
  bit_buf_type get_buffer;        /* current bit-extraction buffer */
  JINT bits_left;                /* number of unused bits in it */
  /* Pointer needed by jpeg_fill_bit_buffer. */
  j_decompress_ptr cinfo;        /* back link to decompress master record */
} bitread_working_state;



/*
 * Expanded entropy decoder object for Huffman decoding.
 *
 * The savable_state subrecord contains fields that change within an MCU,
 * but must not be updated permanently until we complete the MCU.
 */

typedef struct {
  JUINT EOBRUN;                        /* remaining EOBs in EOBRUN */
  JINT last_dc_val[MAX_COMPS_IN_SCAN];        /* last DC coef for each component */
} savable_state;



typedef struct {
  struct jpeg_entropy_decoder pub; /* public fields */

  /* These fields are loaded into local variables at start of each MCU.
   * In case of suspension, we exit WITHOUT updating them.
   */
  bitread_perm_state bitstate;        /* Bit buffer at start of MCU */
  savable_state saved;                /* Other state at start of MCU */

  /* These fields are NOT loaded into local working state. */
  boolean insufficient_data;        /* set TRUE after emitting warning */
  JUINT restarts_to_go;        /* MCUs left in this restart interval */

  /* Following two fields used only in progressive mode */

  /* Pointers to derived tables (these workspaces have image lifespan) */
  d_derived_tbl * derived_tbls[NUM_HUFF_TBLS];

  d_derived_tbl * ac_derived_tbl; /* active table during an AC scan */

  /* Following fields used only in sequential mode */

  /* Pointers to derived tables (these workspaces have image lifespan) */
  d_derived_tbl * dc_derived_tbls[NUM_HUFF_TBLS];
  d_derived_tbl * ac_derived_tbls[NUM_HUFF_TBLS];

  /* Precalculated info set up by start_pass for use in decode_mcu: */

  /* Pointers to derived tables to be used for each block within an MCU */
  d_derived_tbl * dc_cur_tbls[D_MAX_BLOCKS_IN_MCU];
  d_derived_tbl * ac_cur_tbls[D_MAX_BLOCKS_IN_MCU];
  /* Whether we care about the DC and AC coefficient values for each block */
  JINT coef_limit[D_MAX_BLOCKS_IN_MCU];
} huff_entropy_decoder;


static const JINT jpeg_zigzag_order[8][8] = {
  {  0,  1,  5,  6, 14, 15, 27, 28 },
  {  2,  4,  7, 13, 16, 26, 29, 42 },
  {  3,  8, 12, 17, 25, 30, 41, 43 },
  {  9, 11, 18, 24, 31, 40, 44, 53 },
  { 10, 19, 23, 32, 39, 45, 52, 54 },
  { 20, 22, 33, 38, 46, 51, 55, 60 },
  { 21, 34, 37, 47, 50, 56, 59, 61 },
  { 35, 36, 48, 49, 57, 58, 62, 63 }
};


/*
 * Compute the derived values for a Huffman table.
 * This routine also performs some validation checks on the table.
 */

JSC_LOCAL(void)
jpeg_make_d_derived_tbl (j_decompress_ptr cinfo, boolean isDC, JINT tblno,
                         d_derived_tbl ** pdtbl)
{
  JHUFF_TBL *htbl;
  d_derived_tbl *dtbl;
  JINT p;
  JINT i;
  JINT l;
  JINT si;
  JINT numsymbols;
  JINT lookbits;
  JINT ctr;
  U8 huffsize[257];
  JUINT huffcode[257];
  JUINT code;

  /* Note that huffsize[] and huffcode[] are filled in code-length order,
   * paralleling the order of the symbols themselves in htbl->huffval[].
   */

  /* Find the input Huffman table */
  JSC_ASSERT_1(tblno >= 0, tblno);
  JSC_ASSERT_2(tblno < NUM_HUFF_TBLS, tblno, NUM_HUFF_TBLS);
  htbl =
    isDC ? cinfo->dc_huff_tbl_ptrs[tblno] : cinfo->ac_huff_tbl_ptrs[tblno];
  if (htbl == NULL) {
      htbl = jpeg_std_huff_table((j_common_ptr) cinfo, isDC, tblno);
  }

  /* Allocate a workspace if we haven't already done so. */
  if (*pdtbl == NULL) {
        *pdtbl = (d_derived_tbl*) (*cinfo->mem->get_mem)((j_common_ptr) cinfo,
                JPOOL_IMAGE, SIZEOF(d_derived_tbl));
  }
  dtbl = *pdtbl;
  dtbl->pub = htbl;                /* fill in back link */
  
  /* Figure C.1: make table of Huffman code length for each symbol */

  p = 0;
  for (l = 1; l <= 16; l++) {
    i = (JINT) htbl->bits[l];
    /* protect against table overrun */
    JSC_ASSERT_1(i >= 0, i);
    JSC_ASSERT_2(p+i <= 256, p, i);
    while (i) {
      i--;
      huffsize[p++] = (U8) l;
    }
  }
  huffsize[p] = 0;
  numsymbols = p;
  
  /* Figure C.2: generate the codes themselves */
  /* We also validate that the counts represent a legal Huffman code tree. */
  
  code = 0;
  si = huffsize[0];
  p = 0;
  while (huffsize[p]) {
    while (((JINT) huffsize[p]) == si) {
      huffcode[p++] = code;
      code++;
    }
    /* code is now 1 more than the last code used for codelength si; but
     * it must still fit in si bits, since no code is allowed to be all ones.
     */
    JSC_ASSERT_2(! (((INT32) code) >= (((INT32) 1) << si)),
            code, si);
    code <<= 1;
    si++;
  }

  /* Figure F.15: generate decoding tables for bit-sequential decoding */

  p = 0;
  for (l = 1; l <= 16; l++) {
    if (htbl->bits[l]) {
      /* valoffset[l] = huffval[] index of 1st symbol of code length l,
       * minus the minimum code of length l
       */
      dtbl->valoffset[l] = (INT32) p - (INT32) huffcode[p];
      p += htbl->bits[l];
      dtbl->maxcode[l] = huffcode[p-1]; /* maximum code of length l */
    } else {
      dtbl->maxcode[l] = -1;        /* -1 if no codes of this length */
    }
  }
  dtbl->maxcode[17] = 0xFFFFFL; /* ensures jpeg_huff_decode terminates */

  /* Compute lookahead tables to speed up decoding.
   * First we set all the table entries to 0, indicating "too long";
   * then we iterate through the Huffman codes that are short enough and
   * fill in all the entries that correspond to bit sequences starting
   * with that code.
   */

  JSC_MEMZERO(dtbl->look_nbits, SIZEOF(dtbl->look_nbits));

  p = 0;
  for (l = 1; l <= HUFF_LOOKAHEAD; l++) {
    for (i = 1; i <= (JINT) htbl->bits[l]; i++, p++) {
      /* l = current code's length, p = its index in huffcode[] & huffval[]. */
      /* Generate left-justified code followed by all possible bit sequences */
      lookbits = huffcode[p] << (HUFF_LOOKAHEAD-l);
      for (ctr = (1 << (HUFF_LOOKAHEAD-l)); ctr > 0; ctr--) {
        dtbl->look_nbits[lookbits] = l;
        dtbl->look_sym[lookbits] = htbl->huffval[p];
        lookbits++;
      }
    }
  }

  /* Validate symbols as being reasonable.
   * For AC tables, we make no check, but accept all byte values 0..255.
   * For DC tables, we require the symbols to be in range 0..15.
   * (Tighter bounds could be applied depending on the data depth and mode,
   * but this is sufficient to ensure safe decoding.)
   */
  if (isDC) {
    for (i = 0; i < numsymbols; i++) {
      JINT sym = htbl->huffval[i];
      JSC_ASSERT_1(sym>=0, sym);
      JSC_ASSERT_1(sym<=15, sym);
    }
  }
}


/*
 * Out-of-line code for bit fetching.
 * Note: current values of get_buffer and bits_left are passed as parameters,
 * but are returned in the corresponding fields of the state struct.
 *
 * On most machines MIN_GET_BITS should be 25 to allow the full 32-bit width
 * of get_buffer to be used.  (On machines with wider words, an even larger
 * buffer could be used.)  However, on some machines 32-bit shifts are
 * quite slow and take time proportional to the number of places shifted.
 * (This is true with most PC compilers, for instance.)  In this case it may
 * be a win to set MIN_GET_BITS to the minimum value of 15.  This reduces the
 * average shift distance at the cost of more calls to jpeg_fill_bit_buffer.
 */




JSC_LOCAL(boolean)
jpeg_fill_bit_buffer (bitread_working_state * state,
                      register bit_buf_type get_buffer, register JINT bits_left,
                      JINT nbits)
/* Load up the bit buffer to a depth of at least nbits */
{
  /* Copy heavily used state fields into locals (hopefully registers) */
  register const JOCTET * next_input_byte = state->next_input_byte;
  register JSIZE bytes_in_buffer = state->bytes_in_buffer;
  j_decompress_ptr cinfo = state->cinfo;

  /* Attempt to load at least MIN_GET_BITS bits into get_buffer. */
  /* (It is assumed that no request will be for more than that many bits.) */
  /* We fail to do so only if we hit a marker or are forced to suspend. */

  if (cinfo->unread_marker == 0) {        /* cannot advance past a marker */
    while (bits_left < MIN_GET_BITS) {
      register JINT c;

      /* Attempt to read a byte */
      if (bytes_in_buffer == 0) {
        // Abcouwer JSC 2021 - no refilling buffer
        return FALSE;
//        if (! (*cinfo->src->fill_input_buffer) (cinfo))
//          return FALSE;
//        next_input_byte = cinfo->src->next_input_byte;
//        bytes_in_buffer = cinfo->src->bytes_in_buffer;
      }
      bytes_in_buffer--;
      c = GETJOCTET(*next_input_byte);
      next_input_byte++;

      /* If it's 0xFF, check and discard stuffed zero byte */
      if (c == 0xFF) {
        /* Loop here to discard any padding FF's on terminating marker,
         * so that we can save a valid unread_marker value.  NOTE: we will
         * accept multiple FF's followed by a 0 as meaning a single FF data
         * byte.  This data pattern is not valid according to the standard.
         */
        do {
          if (bytes_in_buffer == 0) {
            if (! (*cinfo->src->fill_input_buffer) (cinfo)) {
              return FALSE;
            }
            next_input_byte = cinfo->src->next_input_byte;
            bytes_in_buffer = cinfo->src->bytes_in_buffer;
          }
          bytes_in_buffer--;
          c = GETJOCTET(*next_input_byte);
          next_input_byte++;
        } while (c == 0xFF);

        if (c == 0) {
          /* Found FF/00, which represents an FF data byte */
          c = 0xFF;
        } else {
          /* Oops, it's actually a marker indicating end of compressed data.
           * Save the marker code for later use.
           * Fine point: it might appear that we should save the marker into
           * bitread working state, not straight into permanent state.  But
           * once we have hit a marker, we cannot need to suspend within the
           * current MCU, because we will read no more bytes from the data
           * source.  So it is OK to update permanent state right away.
           */
          cinfo->unread_marker = c;
          /* See if we need to insert some fake zero bits. */
          goto no_more_bytes;
        }
      }

      /* OK, load c into get_buffer */
      get_buffer = (get_buffer << 8) | c;
      bits_left += 8;
    } /* end while */
  } else {
  no_more_bytes:
    /* We get here if we've read the marker that terminates the compressed
     * data segment.  There should be enough bits in the buffer register
     * to satisfy the request; if so, no problem.
     */
    if (nbits > bits_left) {
      /* Uh-oh.  Report corrupted data to user and stuff zeroes into
       * the data stream, so that we can produce some kind of image.
       * We use a nonvolatile flag to ensure that only one warning message
       * appears per data segment.
       */
      if (! ((huff_entropy_decoder *) cinfo->entropy)->insufficient_data) {
        JSC_WARN(JWRN_HIT_MARKER,
                "Corrupt JPEG data: premature end of data segment");
        ((huff_entropy_decoder *) cinfo->entropy)->insufficient_data = TRUE;
      }
      /* Fill the buffer with zero bits */
      get_buffer <<= MIN_GET_BITS - bits_left;
      bits_left = MIN_GET_BITS;
    }
  }

  /* Unload the local registers */
  state->next_input_byte = next_input_byte;
  state->bytes_in_buffer = bytes_in_buffer;
  state->get_buffer = get_buffer;
  state->bits_left = bits_left;

  return TRUE;
}


// Abcouwer JSC 2021 - don't avoid tables
#define BIT_MASK(nbits)   bmask[(nbits)]
#define HUFF_EXTEND(x,s)  ((x) <= bmask[(s) - 1] ? (x) - bmask[(s)] : (x))

static const JINT bmask[16] =        /* bmask[n] is mask for n rightmost bits */
  { 0, 0x0001, 0x0003, 0x0007, 0x000F, 0x001F, 0x003F, 0x007F, 0x00FF,
    0x01FF, 0x03FF, 0x07FF, 0x0FFF, 0x1FFF, 0x3FFF, 0x7FFF };

/*
 * Out-of-line code for Huffman code decoding.
 */

JSC_LOCAL(JINT)
jpeg_huff_decode (bitread_working_state * state,
                  register bit_buf_type get_buffer, register JINT bits_left,
                  d_derived_tbl * htbl, JINT min_bits)
{
  register JINT l = min_bits;
  register INT32 code;

  /* HUFF_DECODE has determined that the code is at least min_bits */
  /* bits long, so fetch that many bits in one swoop. */

  CHECK_BIT_BUFFER(*state, l, return -1);
  code = GET_BITS(l);

  /* Collect the rest of the Huffman code one bit at a time. */
  /* This is per Figure F.16 in the JPEG spec. */

  while (code > htbl->maxcode[l]) {
    code <<= 1;
    CHECK_BIT_BUFFER(*state, 1, return -1);
    code |= GET_BITS(1);
    l++;
  }

  /* Unload the local registers */
  state->get_buffer = get_buffer;
  state->bits_left = bits_left;

  /* With garbage input we may reach the sentinel value l = 17. */

  if (l > 16) {
    JSC_WARN(JWRN_HUFF_BAD_CODE, "Corrupt JPEG data: bad Huffman code");
    return 0;                        /* fake a zero as the safest result */
  }

  return htbl->pub->huffval[ (JINT) (code + htbl->valoffset[l]) ];
}


/*
 * Finish up at the end of a Huffman-compressed scan.
 */

JSC_METHODDEF(void)
finish_pass_huff (j_decompress_ptr cinfo)
{
  JSC_ASSERT(cinfo != NULL);
  huff_entropy_decoder * entropy = (huff_entropy_decoder *) cinfo->entropy;

  /* Throw away any unused bits remaining in bit buffer; */
  /* include any full bytes in next_marker's count of discarded bytes */
  cinfo->marker->discarded_bytes += entropy->bitstate.bits_left / 8;
  entropy->bitstate.bits_left = 0;
}


/*
 * Check for a restart marker & resynchronize decoder.
 * Returns FALSE if must suspend.
 */

JSC_LOCAL(boolean)
process_restart (j_decompress_ptr cinfo)
{
  JSC_ASSERT(cinfo != NULL);
  huff_entropy_decoder * entropy = (huff_entropy_decoder *) cinfo->entropy;
  JINT ci;

  finish_pass_huff(cinfo);

  /* Advance past the RSTn marker */
  if (! (*cinfo->marker->read_restart_marker) (cinfo)) {
    return FALSE;
  }

  /* Re-initialize DC predictions to 0 */
  for (ci = 0; ci < cinfo->comps_in_scan; ci++) {
    entropy->saved.last_dc_val[ci] = 0;
  }
  /* Re-init EOB run count, too */
  entropy->saved.EOBRUN = 0;

  /* Reset restart counter */
  entropy->restarts_to_go = cinfo->restart_interval;

  /* Reset out-of-data flag, unless read_restart_marker left us smack up
   * against a marker.  In that case we will end up treating the next data
   * segment as empty, and we can avoid producing bogus output pixels by
   * leaving the flag set.
   */
  if (cinfo->unread_marker == 0) {
    entropy->insufficient_data = FALSE;
  }

  return TRUE;
}

// Abcouwer JSC 2021 - remove progressive support


/*
 * Decode one MCU's worth of Huffman-compressed coefficients,
 * full-size blocks.
 */

JSC_METHODDEF(boolean)
decode_mcu (j_decompress_ptr cinfo, JBLOCKROW *MCU_data)
{
  JSC_ASSERT(cinfo != NULL);
  JSC_ASSERT(MCU_data != NULL);

  huff_entropy_decoder * entropy = (huff_entropy_decoder *) cinfo->entropy;
  JINT blkn;
//  BITREAD_STATE_VARS;
  register bit_buf_type get_buffer;
  register JINT bits_left;
  bitread_working_state br_state;
  savable_state state;

  /* Process restart marker if needed; may have to suspend */
  if (cinfo->restart_interval) {
    if (entropy->restarts_to_go == 0)
      if (! process_restart(cinfo))
        return FALSE;
  }

  /* If we've run out of data, just leave the MCU set to zeroes.
   * This way, we return uniform gray for the remainder of the segment.
   */
  if (! entropy->insufficient_data) {

    /* Load up working state */
    //BITREAD_LOAD_STATE(cinfo, entropy->bitstate);
    br_state.cinfo = cinfo; \
    br_state.next_input_byte = cinfo->src->next_input_byte;
    br_state.bytes_in_buffer = cinfo->src->bytes_in_buffer;
    get_buffer = entropy->bitstate.get_buffer;
    bits_left = entropy->bitstate.bits_left;
//    ASSIGN_STATE(state, entropy->saved);
    state = entropy->saved;

    /* Outer loop handles each block in the MCU */

    for (blkn = 0; blkn < cinfo->blocks_in_MCU; blkn++) {
      JBLOCKROW block = MCU_data[blkn];
      d_derived_tbl * htbl;
      register JINT s;
      register JINT k;
      register JINT r;
      JINT coef_limit;
      JINT ci;

      /* Decode a single block's worth of coefficients */

      /* Section F.2.2.1: decode the DC coefficient difference */
      htbl = entropy->dc_cur_tbls[blkn];
      HUFF_DECODE(s, br_state, htbl, return FALSE, label1);

      htbl = entropy->ac_cur_tbls[blkn];
      k = 1;
      coef_limit = entropy->coef_limit[blkn];
      if (coef_limit) {
        /* Convert DC difference to actual value, update last_dc_val */
        if (s) {
          CHECK_BIT_BUFFER(br_state, s, return FALSE);
          r = GET_BITS(s);
          s = HUFF_EXTEND(r, s);
        }
        ci = cinfo->MCU_membership[blkn];
        s += state.last_dc_val[ci];
        state.last_dc_val[ci] = s;
        /* Output the DC coefficient */
        (*block)[0] = (JCOEF) s;

        /* Section F.2.2.2: decode the AC coefficients */
        /* Since zeroes are skipped, output area must be cleared beforehand */
        while(k < coef_limit) {
          HUFF_DECODE(s, br_state, htbl, return FALSE, label2);

          r = s >> 4;
          s &= 15;

          if (s) {
            k += r;
            CHECK_BIT_BUFFER(br_state, s, return FALSE);
            r = GET_BITS(s);
            s = HUFF_EXTEND(r, s);
            /* Output coefficient in natural (dezigzagged) order.
             * Note: the extra entries in jpeg_natural_order[] will save us
             * if k >= DCTSIZE2, which could happen if the data is corrupted.
             */
            (*block)[jpeg_natural_order[k]] = (JCOEF) s;
          } else if (r != 15) {
              goto EndOfBlock;
          } else {
            k += 15;
          }
          k++;
        }
      } else if (s) {
        CHECK_BIT_BUFFER(br_state, s, return FALSE);
        DROP_BITS(s);
      } else {
          // do nothing
      }

      /* Section F.2.2.2: decode the AC coefficients */
      /* In this path we just discard the values */
      while (k < DCTSIZE2) {
        HUFF_DECODE(s, br_state, htbl, return FALSE, label3);

        r = s >> 4;
        s &= 15;

        if (s) {
          k += r;
          CHECK_BIT_BUFFER(br_state, s, return FALSE);
          DROP_BITS(s);
        } else if (r != 15) {
          break;
        } else {
          k += 15;
        }
        k++;
      }

      EndOfBlock: ;
    }

    /* Completed MCU, so update state */
    //BITREAD_SAVE_STATE(cinfo, entropy->bitstate);
    cinfo->src->next_input_byte = br_state.next_input_byte;
    cinfo->src->bytes_in_buffer = br_state.bytes_in_buffer;
    entropy->bitstate.get_buffer = get_buffer;
    entropy->bitstate.bits_left = bits_left;

//    ASSIGN_STATE(entropy->saved, state);
    entropy->saved = state;
  }

  /* Account for restart interval if using restarts */
  if (cinfo->restart_interval) {
    entropy->restarts_to_go--;
  }

  return TRUE;
}


/*
 * Initialize for a Huffman-compressed scan.
 */

JSC_METHODDEF(void)
start_pass_huff_decoder (j_decompress_ptr cinfo)
{
  JSC_ASSERT(cinfo != NULL);

  huff_entropy_decoder * entropy = (huff_entropy_decoder *) cinfo->entropy;
  JINT ci;
  JINT blkn;
  JINT tbl;
  JINT i;
  jpeg_component_info * compptr;

// Abcouwer JSC 2021 - remove progressive support

    /* Check that the scan parameters Ss, Se, Ah/Al are OK for sequential JPEG.
     * This ought to be an error condition, but we make it a warning because
     * there are some baseline files out there with all zeroes in these bytes.
     */
    if (cinfo->Ss != 0 || cinfo->Ah != 0 || cinfo->Al != 0 ||
        ((cinfo->is_baseline || cinfo->Se < DCTSIZE2) &&
        cinfo->Se != cinfo->lim_Se)) {
      JSC_WARN(JWRN_NOT_SEQUENTIAL,
              "Invalid SOS parameters for sequential JPEG");
    }

    /* Select MCU decoding routine */
    /* We retain the hard-coded case for full-size blocks.
     * This is not necessary, but it appears that this version is slightly
     * more performant in the given implementation.
     * With an improved implementation we would prefer a single optimized
     * function.
     */
    // Abcouwer JSC 2021 - should always be baseline
    JSC_ASSERT_2(cinfo->lim_Se == DCTSIZE2-1, cinfo->lim_Se, DCTSIZE2-1);
    entropy->pub.decode_mcu = decode_mcu;

    for (ci = 0; ci < cinfo->comps_in_scan; ci++) {
      compptr = cinfo->cur_comp_info[ci];
      /* Compute derived values for Huffman tables */
      /* We may do this more than once for a table, but it's not expensive */
      tbl = compptr->dc_tbl_no;
      jpeg_make_d_derived_tbl(cinfo, TRUE, tbl,
                              & entropy->dc_derived_tbls[tbl]);
      if (cinfo->lim_Se) {        /* AC needs no table when not present */
        tbl = compptr->ac_tbl_no;
        jpeg_make_d_derived_tbl(cinfo, FALSE, tbl,
                                & entropy->ac_derived_tbls[tbl]);
      }
      /* Initialize DC predictions to 0 */
      entropy->saved.last_dc_val[ci] = 0;
    }

    /* Precalculate decoding info for each block in an MCU of this scan */
    for (blkn = 0; blkn < cinfo->blocks_in_MCU; blkn++) {
      ci = cinfo->MCU_membership[blkn];
      compptr = cinfo->cur_comp_info[ci];
      /* Precalculate which table to use for each block */
      entropy->dc_cur_tbls[blkn] = entropy->dc_derived_tbls[compptr->dc_tbl_no];
      entropy->ac_cur_tbls[blkn] =        /* AC needs no table when not present */
        cinfo->lim_Se ? entropy->ac_derived_tbls[compptr->ac_tbl_no] : NULL;
      /* Decide whether we really care about the coefficient values */
      if (compptr->component_needed) {
        ci = compptr->DCT_v_scaled_size;
        i = compptr->DCT_h_scaled_size;
        // Abcouwer JSC 2021 - should always be baseline
        JSC_ASSERT_2(cinfo->lim_Se == DCTSIZE2-1, cinfo->lim_Se, DCTSIZE2-1);

          if (ci <= 0 || ci > 8) ci = 8;
          if (i <= 0 || i > 8) i = 8;
          entropy->coef_limit[blkn] = 1 + jpeg_zigzag_order[ci - 1][i - 1];
      } else {
        entropy->coef_limit[blkn] = 0;
      }
    }

  /* Initialize bitread state variables */
  entropy->bitstate.bits_left = 0;
  entropy->bitstate.get_buffer = 0; /* unnecessary, but keeps Purify quiet */
  entropy->insufficient_data = FALSE;

  /* Initialize restart counter */
  entropy->restarts_to_go = cinfo->restart_interval;
}


/*
 * Module initialization routine for Huffman entropy decoding.
 */

JSC_GLOBAL(void)
jinit_huff_decoder (j_decompress_ptr cinfo)
{
  huff_entropy_decoder * entropy;
  JINT i;

  entropy = (huff_entropy_decoder *) (*cinfo->mem->get_mem)
    ((j_common_ptr) cinfo, JPOOL_IMAGE, SIZEOF(huff_entropy_decoder));
  cinfo->entropy = &entropy->pub;
  entropy->pub.start_pass = start_pass_huff_decoder;
  entropy->pub.finish_pass = finish_pass_huff;

  // Abcouwer JSC 2021 - remove progressive support

    /* Mark derived tables unallocated */
    for (i = 0; i < NUM_HUFF_TBLS; i++) {
      entropy->dc_derived_tbls[i] = entropy->ac_derived_tbls[i] = NULL;
    }
}
