/*
 * jchuff.c
 *
 * Copyright (C) 1991-1997, Thomas G. Lane.
 * Modified 2006-2019 by Guido Vollbeding.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
 *
 * This file contains Huffman entropy encoding routines.
 * Both sequential and progressive modes are supported in this single module.
 *
 * Much of the complexity here has to do with supporting output suspension.
 * If the data destination module demands suspension, we want to be able to
 * back up to the start of the current MCU.  To do this, we copy state
 * variables into local working storage, and update them back to the
 * permanent JPEG objects only upon successful completion of an MCU.
 *
 * We do not support output suspension for the progressive JPEG mode, since
 * the library currently does not allow multiple-scan files to be written
 * with output suspension.
 */

#include "jsc/jpegint.h"



#define MAX_COEF_BITS 10

/* MAX_CORR_BITS is the number of bits the AC refinement correction-bit
 * buffer can hold.  Larger sizes may slightly improve compression, but
 * 1000 is already well into the realm of overkill.
 * The minimum safe size is 64 bits.
 */

#define MAX_CORR_BITS  1000        /* Max # of correction bits I can buffer */

/* Outputting bytes to the file.
 * NB: these must be called only when actually outputting,
 * that is, entropy->gather_statistics == FALSE.
 */


/* Emit a byte, taking 'action' if must suspend. */
#define emit_byte_s(state,val,action)  \
        { *(state)->next_output_byte = (JOCTET) (val);  \
          (state)->next_output_byte++; \
          (state)->free_in_buffer--; \
          if ((state)->free_in_buffer == 0)  \
              { action; } }

/* Derived data constructed for each Huffman table */

typedef struct {
  JUINT ehufco[256];        /* code for each symbol */
  U8 ehufsi[256];                /* length of code for each symbol */
  /* If no code has been allocated for a symbol S, ehufsi[S] contains 0 */
} c_derived_tbl;


/* Expanded entropy encoder object for Huffman encoding.
 *
 * The savable_state subrecord contains fields that change within an MCU,
 * but must not be updated permanently until we complete the MCU.
 */

typedef struct {
  INT32 put_buffer;                /* current bit-accumulation buffer */
  JINT put_bits;                        /* # of bits now in it */
  JINT last_dc_val[MAX_COMPS_IN_SCAN]; /* last DC coef for each component */
} savable_state;



typedef struct {
  struct jpeg_entropy_encoder pub; /* public fields */

  savable_state saved;                /* Bit buffer & DC state at start of MCU */

  /* These fields are NOT loaded into local working state. */
  JUINT restarts_to_go;        /* MCUs left in this restart interval */
  JINT next_restart_num;                /* next restart number to write (0-7) */

  /* Pointers to derived tables (these workspaces have image lifespan) */
  c_derived_tbl * dc_derived_tbls[NUM_HUFF_TBLS];
  c_derived_tbl * ac_derived_tbls[NUM_HUFF_TBLS];

  // Abcouwer JSC 2021 - don't need stats tables - not optimizing

  /* Following fields used only in progressive mode */

  /* Mode flag: TRUE for optimization, FALSE for actual data output */
  boolean gather_statistics;

  /* next_output_byte/free_in_buffer are local copies of cinfo->dest fields.
   */
  JOCTET * next_output_byte;        /* => next byte to write in buffer */
  JSIZE free_in_buffer;        /* # of byte spaces remaining in buffer */
  j_compress_ptr cinfo;                /* link to cinfo (needed for dump_buffer) */

  /* Coding status for AC components */
  JINT ac_tbl_no;                /* the table number of the single component */
  JUINT EOBRUN;                /* run length of EOBs */
  JUINT BE;                /* # of buffered correction bits before MCU */
  U8 * bit_buffer;                /* buffer for correction bits (1 per char) */
  /* packing correction bits tightly would save some space but cost time... */
} huff_entropy_encoder;

/* Working state while writing an MCU (sequential mode).
 * This struct contains all the fields that are needed by subroutines.
 */

typedef struct {
  JOCTET * next_output_byte;        /* => next byte to write in buffer */
  JSIZE free_in_buffer;        /* # of byte spaces remaining in buffer */
  savable_state cur;                /* Current bit buffer & DC state */
  j_compress_ptr cinfo;                /* dump_buffer needs access to this */
} working_state;


// Abcouwer JSC 2021 - no loner need IRIGHT_SHIFT


/* The legal range of a DCT coefficient is
 *  -1024 .. +1023  for 8-bit data;
 * -16384 .. +16383 for 12-bit data.
 * Hence the magnitude should always fit in 10 or 14 bits respectively.
 */

JSC_COMPILE_ASSERT(BITS_IN_JSAMPLE == 8, badbitsinjsample);

/*
 * Compute the derived values for a Huffman table.
 * This routine also performs some validation checks on the table.
 */

JSC_LOCAL(void)
jpeg_make_c_derived_tbl (j_compress_ptr cinfo, boolean isDC, JINT tblno,
                         c_derived_tbl ** pdtbl)
{
  JHUFF_TBL *htbl;
  c_derived_tbl *dtbl;
  JINT p;
  JINT i;
  JINT l;
  JINT lastp;
  JINT si;
  JINT maxsymbol;
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
  if (htbl == NULL)
    htbl = jpeg_std_huff_table((j_common_ptr) cinfo, isDC, tblno);

  /* Allocate a workspace if we haven't already done so. */
  if (*pdtbl == NULL)
    *pdtbl = (c_derived_tbl *) (*cinfo->mem->get_mem)
      ((j_common_ptr) cinfo, JPOOL_IMAGE, SIZEOF(c_derived_tbl));
  dtbl = *pdtbl;
  
  /* Figure C.1: make table of Huffman code length for each symbol */

  p = 0;
  for (l = 1; l <= 16; l++) {
    i = (JINT) htbl->bits[l];
    /* protect against table overrun */
    JSC_ASSERT_1(i>=0, i);
    JSC_ASSERT_2(p + i <= 256, p, i);
    while (i) {
      i--;
      huffsize[p++] = (U8) l;
    }
  }
  huffsize[p] = 0;
  lastp = p;
  
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
    JSC_ASSERT_2(((INT32) code) < (((INT32) 1) << si), code, si);
    code <<= 1;
    si++;
  }
  
  /* Figure C.3: generate encoding tables */
  /* These are code and size indexed by symbol value */

  /* Set all codeless symbols to have code length 0;
   * this lets us detect duplicate VAL entries here, and later
   * allows emit_bits to detect any attempt to emit such symbols.
   */
  JSC_MEMZERO(dtbl->ehufsi, SIZEOF(dtbl->ehufsi));

  /* This is also a convenient place to check for out-of-range
   * and duplicated VAL entries.  We allow 0..255 for AC symbols
   * but only 0..15 for DC.  (We could constrain them further
   * based on data depth and mode, but this seems enough.)
   */
  maxsymbol = isDC ? 15 : 255;

  for (p = 0; p < lastp; p++) {
    i = htbl->huffval[p];
    JSC_ASSERT_1(i >= 0, i);
    JSC_ASSERT_2(i <= maxsymbol, i, maxsymbol);
    JSC_ASSERT_2(!(dtbl->ehufsi[i]), i, dtbl->ehufsi[i]);
    dtbl->ehufco[i] = huffcode[p];
    dtbl->ehufsi[i] = huffsize[p];
  }
}


// Abcouwer JSC 2021 - no dumping/resizing of buffers
// Abcouwer JSC 2021 - remove progressive encoding

/* Outputting bits to the file */

/* Only the right 24 bits of put_buffer are used; the valid bits are
 * left-justified in this part.  At most 16 bits can be passed to emit_bits
 * in one call, and we never retain more than 7 bits in put_buffer
 * between calls, so 24 bits are sufficient.
 */

JSC_INLINE
JSC_LOCAL(boolean)
emit_bits_s (working_state * state, JUINT code, JINT size)
/* Emit some bits; return TRUE if successful, FALSE if must suspend */
{
  /* This routine is heavily used, so it's worth coding tightly. */
  register INT32 put_buffer;
  register JINT put_bits;

  /* if size is 0, caller used an invalid Huffman table entry */
  JSC_ASSERT(size != 0);

  /* mask off any extra bits in code */
  put_buffer = ((INT32) code) & ((((INT32) 1) << size) - 1);

  /* new number of bits in buffer */
  put_bits = size + state->cur.put_bits;

  put_buffer <<= 24 - put_bits; /* align incoming bits */

  /* and merge with old buffer contents */
  put_buffer |= state->cur.put_buffer;

  while (put_bits >= 8) {
      JINT c = (JINT) ((put_buffer >> 16) & 0xFF);

    emit_byte_s(state, c, return FALSE);
    if (c == 0xFF) {                /* need to stuff a zero byte? */
      emit_byte_s(state, 0, return FALSE);
    }
    put_buffer <<= 8;
    put_bits -= 8;
  }

  state->cur.put_buffer = put_buffer; /* update state variables */
  state->cur.put_bits = put_bits;

  return TRUE;
}

// Abcouwer JSC 2021 - remove progressive encoding

JSC_LOCAL(boolean)
flush_bits_s (working_state * state)
{
  if (! emit_bits_s(state, 0x7F, 7)) /* fill any partial byte with ones */
    return FALSE;
  state->cur.put_buffer = 0;             /* and reset bit-buffer to empty */
  state->cur.put_bits = 0;
  return TRUE;
}

// Abcouwer JSC 2021 - remove progressive encoding


/*
 * Emit a restart marker & resynchronize predictions.
 */

JSC_LOCAL(boolean)
emit_restart_s (working_state * state, JINT restart_num)
{
  JINT ci;

  if (! flush_bits_s(state))
    return FALSE;

  emit_byte_s(state, 0xFF, return FALSE);
  emit_byte_s(state, JPEG_RST0 + restart_num, return FALSE);

  /* Re-initialize DC predictions to 0 */
  for (ci = 0; ci < state->cinfo->comps_in_scan; ci++) {
    state->cur.last_dc_val[ci] = 0;
  }

  /* The restart counter is not updated until we successfully write the MCU. */

  return TRUE;
}

// Abcouwer JSC 2021 - remove progressive encoding

/* Encode a single block's worth of coefficients */

JSC_LOCAL(boolean)
encode_one_block (working_state * state, JCOEFPTR block, JINT last_dc_val,
                  c_derived_tbl *dctbl, c_derived_tbl *actbl)
{
  register JINT temp;
  register JINT temp2;
  register JINT nbits;
  register JINT r;
  register JINT k;
  JINT Se = state->cinfo->lim_Se;
  const JINT * natural_order = state->cinfo->natural_order;

  /* Encode the DC coefficient difference per section F.1.2.1 */

  temp = temp2 = block[0] - last_dc_val;

  if (temp < 0) {
    temp = -temp;                /* temp is abs value of input */
    /* For a negative input, want temp2 = bitwise complement of abs(input) */
    /* This code assumes we are on a two's complement machine */
    temp2--;
  }

  /* Find the number of bits needed for the magnitude of the coefficient */
  nbits = 0;
  while (temp) {
    nbits++;
    temp >>= 1;
  }
  /* Check for out-of-range coefficient values.
   * Since we're encoding a difference, the range limit is twice as much.
   */
  JSC_ASSERT_2(nbits <= MAX_COEF_BITS+1, nbits, MAX_COEF_BITS);

  /* Emit the Huffman-coded symbol for the number of bits */
  if (! emit_bits_s(state, dctbl->ehufco[nbits], dctbl->ehufsi[nbits]))
    return FALSE;

  /* Emit that number of bits of the value, if positive, */
  /* or the complement of its magnitude, if negative. */
  if (nbits)                        /* emit_bits rejects calls with size 0 */
    if (! emit_bits_s(state, (JUINT) temp2, nbits))
      return FALSE;

  /* Encode the AC coefficients per section F.1.2.2 */

  r = 0;                        /* r = run length of zeros */

  for (k = 1; k <= Se; k++) {
      temp2 = block[natural_order[k]];
    if (temp2 == 0) {
      r++;
    } else {
      /* if run length > 15, must emit special run-length-16 codes (0xF0) */
      while (r > 15) {
        if (! emit_bits_s(state, actbl->ehufco[0xF0], actbl->ehufsi[0xF0]))
          return FALSE;
        r -= 16;
      }

      temp = temp2;
      if (temp < 0) {
        temp = -temp;                /* temp is abs value of input */
        /* This code assumes we are on a two's complement machine */
        temp2--;
      }

      /* Find the number of bits needed for the magnitude of the coefficient */
      nbits = 1;                /* there must be at least one 1 bit */
      while ((temp >>= 1)) {
        nbits++;
      }
      /* Check for out-of-range coefficient values */
      JSC_ASSERT_2(nbits <= MAX_COEF_BITS, nbits, MAX_COEF_BITS);

      /* Emit Huffman symbol for run length / number of bits */
      temp = (r << 4) + nbits;
      if (! emit_bits_s(state, actbl->ehufco[temp], actbl->ehufsi[temp]))
        return FALSE;

      /* Emit that number of bits of the value, if positive, */
      /* or the complement of its magnitude, if negative. */
      if (! emit_bits_s(state, (JUINT) temp2, nbits))
        return FALSE;

      r = 0;
    }
  }

  /* If the last coef(s) were zero, emit an end-of-block code */
  if (r > 0)
    if (! emit_bits_s(state, actbl->ehufco[0], actbl->ehufsi[0]))
      return FALSE;

  return TRUE;
}


/*
 * Encode and output one MCU's worth of Huffman-compressed coefficients.
 */

JSC_METHODDEF(boolean)
encode_mcu_huff (j_compress_ptr cinfo, JBLOCKROW *MCU_data)
{
  huff_entropy_encoder * entropy = (huff_entropy_encoder *) cinfo->entropy;
  working_state state;
  JINT blkn;
  JINT ci;
  jpeg_component_info * compptr;

  /* Load up working state */
  state.next_output_byte = cinfo->dest->next_output_byte;
  state.free_in_buffer = cinfo->dest->free_in_buffer;
  state.cur = entropy->saved;
  state.cinfo = cinfo;

  /* Emit restart marker if needed */
  if (cinfo->restart_interval) {
    if (entropy->restarts_to_go == 0)
      if (! emit_restart_s(&state, entropy->next_restart_num))
        return FALSE;
  }

  /* Encode the MCU data blocks */
  for (blkn = 0; blkn < cinfo->blocks_in_MCU; blkn++) {
    ci = cinfo->MCU_membership[blkn];
    compptr = cinfo->cur_comp_info[ci];
    if (! encode_one_block(&state,
                           MCU_data[blkn][0], state.cur.last_dc_val[ci],
                           entropy->dc_derived_tbls[compptr->dc_tbl_no],
                           entropy->ac_derived_tbls[compptr->ac_tbl_no]))
      return FALSE;
    /* Update last_dc_val */
    state.cur.last_dc_val[ci] = MCU_data[blkn][0][0];
  }

  /* Completed MCU, so update state */
  cinfo->dest->next_output_byte = state.next_output_byte;
  cinfo->dest->free_in_buffer = state.free_in_buffer;
  entropy->saved = state.cur;

  /* Update restart-interval state too */
  if (cinfo->restart_interval) {
    if (entropy->restarts_to_go == 0) {
      entropy->restarts_to_go = cinfo->restart_interval;
      entropy->next_restart_num++;
      entropy->next_restart_num &= 7;
    }
    entropy->restarts_to_go--;
  }

  return TRUE;
}


/*
 * Finish up at the end of a Huffman-compressed scan.
 */

JSC_METHODDEF(void)
finish_pass_huff (j_compress_ptr cinfo)
{
    JSC_ASSERT(cinfo != NULL);
    huff_entropy_encoder * entropy = (huff_entropy_encoder *) cinfo->entropy;
    working_state state;

    // Abcouwer JSC 2021 - remove progressive mode
    /* Load up working state ... flush_bits needs it */
    JSC_ASSERT(cinfo->dest != NULL);
    state.next_output_byte = cinfo->dest->next_output_byte;
    state.free_in_buffer = cinfo->dest->free_in_buffer;
    state.cur = entropy->saved;
    state.cinfo = cinfo;

    /* Flush out the last data */
    boolean flushed = flush_bits_s(&state);
    JSC_ASSERT(flushed);

    /* Update state */
    cinfo->dest->next_output_byte = state.next_output_byte;
    cinfo->dest->free_in_buffer = state.free_in_buffer;
    entropy->saved = state.cur;
}

// Abcouwer JSC 2021 - remove optimized encoding

/*
 * Initialize for a Huffman-compressed scan.
 * If gather_statistics is TRUE, we do not output anything during the scan,
 * just count the Huffman symbols used and generate Huffman code tables.
 */

JSC_METHODDEF(void)
start_pass_huff (j_compress_ptr cinfo, boolean gather_statistics)
{
    JSC_ASSERT(gather_statistics == FALSE); // Abcouwer JSC 2021 - disabled

    huff_entropy_encoder * entropy = (huff_entropy_encoder *) cinfo->entropy;
    JINT ci;
    JINT tbl;
    jpeg_component_info *compptr;

    // Abcouwer JSC 2021 - remove optimized encoding

    entropy->pub.finish_pass = finish_pass_huff;

    // Abcouwer JSC 2021 - remove progressive mode
    // Abcouwer JSC 2021 - remove optimized encoding

    entropy->pub.encode_mcu = encode_mcu_huff;

    for (ci = 0; ci < cinfo->comps_in_scan; ci++) {
        compptr = cinfo->cur_comp_info[ci];
        /* DC needs no table for refinement scan */
        if (cinfo->Ss == 0 && cinfo->Ah == 0) {
            tbl = compptr->dc_tbl_no;

            // Abcouwer JSC 2021 - remove optimized encoding

            /* Compute derived values for Huffman tables */
            /* We may do this more than once for a table, but it's not expensive */
            jpeg_make_c_derived_tbl(cinfo, TRUE, tbl,
                    &entropy->dc_derived_tbls[tbl]);

            /* Initialize DC predictions to 0 */
            entropy->saved.last_dc_val[ci] = 0;
        }
        /* AC needs no table when not present */
        if (cinfo->Se) {
            tbl = compptr->ac_tbl_no;

            // Abcouwer JSC 2021 - remove optimized encoding

            jpeg_make_c_derived_tbl(cinfo, FALSE, tbl,
                    &entropy->ac_derived_tbls[tbl]);
        }
    }

    /* Initialize bit buffer to empty */
    entropy->saved.put_buffer = 0;
    entropy->saved.put_bits = 0;

    /* Initialize restart stuff */
    entropy->restarts_to_go = cinfo->restart_interval;
    entropy->next_restart_num = 0;
}


/*
 * Module initialization routine for Huffman entropy encoding.
 */

JSC_GLOBAL(void)
jinit_huff_encoder (j_compress_ptr cinfo)
{
    JSC_ASSERT(cinfo != NULL);

    huff_entropy_encoder * entropy;
    JINT i;

    entropy = (huff_entropy_encoder *) (*cinfo->mem->get_mem)(
            (j_common_ptr) cinfo, JPOOL_IMAGE, SIZEOF(huff_entropy_encoder));
    cinfo->entropy = &entropy->pub;
    entropy->pub.start_pass = start_pass_huff;

    /* Mark tables unallocated */
    for (i = 0; i < NUM_HUFF_TBLS; i++) {
        entropy->dc_derived_tbls[i] = NULL;
        entropy->ac_derived_tbls[i] = NULL;
        // Abcouwer JSC 2021 - don't need stats tables - not optimizing
    }

    // Abcouwer JSC 2021 - remove progressive mode
}
