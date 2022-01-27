/*
 * jerror.h
 *
 * Copyright (C) 1994-1997, Thomas G. Lane.
 * Modified 1997-2018 by Guido Vollbeding.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
 *
 * This file defines the error and message codes for the JPEG library.
 * Edit this file to add new codes, or to translate the message strings to
 * some other language.
 * A set of error-reporting macros are defined too.  Some applications using
 * the JPEG library may wish to include this file to get the error codes
 * and/or the macros.
 */

#ifndef JERROR_H
#define JERROR_H

// Abcouwer JSC 2021 - unwind the double processing trick for enums and messages

typedef enum {

    JMSG_NOMESSAGE,

    /* For maintenance convenience, list is alphabetical by message code name */
    JERR_BAD_COMPONENT_ID, // Invalid component ID %d in SOS
    JERR_BAD_HUFF_TABLE, // Bogus Huffman table definition
    JERR_BAD_LENGTH, // Bogus marker length
    JERR_BAD_MCU_SIZE, // Sampling factors too large for interleaved scan
    JERR_BAD_PRECISION, // Unsupported JPEG data precision %d
    JERR_DHT_INDEX, // Bogus DHT index %d
    JERR_DQT_INDEX, // Bogus DQT index %d
    JERR_EMPTY_IMAGE, // Empty JPEG image (DNL not supported)
    JERR_FRACT_SAMPLE_NOTIMPL, // Fractional sampling not implemented yet
    JERR_JSC_NOSUPPORT, // Option not supported by JSC.
    JERR_JSC_BAD_HUFFVALS, // Bad huffman table values.
    JERR_JSC_DUPLICATE_QUANT_TBL, // Got duplicate quant tbl %d
    JERR_JSC_WRITE_LINE_FAIL,
        // Failed to write scanline, output buffer may be filled.
    JERR_NO_HUFF_TABLE, // Huffman table 0x%02x was not defined
    JERR_NO_IMAGE, // JPEG datastream contains no image
    JERR_NO_QUANT_TABLE, // Quantization table 0x%02x was not defined
    JERR_NO_SOI, // Not a JPEG file: starts with 0x%02x 0x%02x
    JERR_OUT_OF_MEMORY, // Insufficient memory (case %d)
    JERR_SOF_BEFORE, // Invalid JPEG file structure: %s before SOF
    JERR_SOF_DUPLICATE, // Invalid JPEG file structure: two SOF markers
    JERR_SOF_NO_SOS, // Invalid JPEG file structure: missing SOS marker
    JERR_SOI_DUPLICATE, // Invalid JPEG file structure: two SOI markers
    JERR_UNKNOWN_MARKER, // Unsupported marker type 0x%02x

    JTRC_ADOBE,
        // Adobe APP14 marker: version %d, flags 0x%04x 0x%04x, transform %d
    JTRC_APP0, // Unknown APP0 marker (not JFIF), length %u
    JTRC_APP14, // Unknown APP14 marker (not Adobe), length %u
    JTRC_DHT, // Define Huffman Table 0x%02x
    JTRC_DQT, // Define Quantization Table %d  precision %d
    JTRC_DRI, // Define Restart Interval %u
    JTRC_EOI, // End Of Image
    JTRC_HUFFBITS, //         %3d %3d %3d %3d %3d %3d %3d %3d
    JTRC_JFIF, // JFIF APP0 marker: version %d.%02d, density %dx%d  %d
    JTRC_JFIF_BADTHUMBNAILSIZE,
        // Warning: thumbnail image size does not match data length %u
    JTRC_JFIF_EXTENSION, // JFIF extension marker: type 0x%02x, length %u
    JTRC_JFIF_THUMBNAIL, //     with %d x %d thumbnail image
    JTRC_MISC_MARKER, // Miscellaneous marker 0x%02x, length %u
    JTRC_PARMLESS_MARKER, // Unexpected marker 0x%02x
    JTRC_QUANTVALS, //         %4u %4u %4u %4u %4u %4u %4u %4u
    JTRC_RECOVERY_ACTION, // At marker 0x%02x, recovery action %d
    JTRC_RST, // RST%d
    JTRC_SOF, // Start Of Frame 0x%02x: width=%u, height=%u, components=%d
    JTRC_SOF_COMPONENT, //     Component %d: %dhx%dv q=%d
    JTRC_SOI, // Start of Image
    JTRC_SOS, // Start Of Scan: %d components
    JTRC_SOS_COMPONENT, //     Component %d: dc=%d ac=%d
    JTRC_SOS_PARAMS, //   Ss=%d, Se=%d, Ah=%d, Al=%d
    JTRC_THUMB_JPEG,
        // JFIF extension marker: JPEG-compressed thumbnail image, length %u
    JTRC_THUMB_PALETTE,
        // JFIF extension marker: palette thumbnail image, length %u
    JTRC_THUMB_RGB, // JFIF extension marker: RGB thumbnail image, length %u
    JTRC_UNKNOWN_IDS, // Unrecognized component IDs %d %d %d, assuming YCbCr

    JWRN_ADOBE_XFORM, // Unknown Adobe color transform code %d
    JWRN_EXTRANEOUS_DATA,
        // Corrupt JPEG data: %u extraneous bytes before marker 0x%02x
    JWRN_HIT_MARKER, // Corrupt JPEG data: premature end of data segment
    JWRN_HUFF_BAD_CODE, // Corrupt JPEG data: bad Huffman code
    JWRN_JFIF_MAJOR, // Warning: unknown JFIF revision number %d.%02d
    JWRN_MUST_RESYNC, // Corrupt JPEG data: found marker 0x%02x instead of RST%d
    JWRN_NOT_SEQUENTIAL, // Invalid SOS parameters for sequential JPEG
    JWRN_TOO_MUCH_DATA, // Application transferred too many scanlines

    JMSG_LASTMSGCODE
} J_MESSAGE_CODE;


#endif /* JERROR_H */
