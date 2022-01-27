#include <stdio.h>

//#define STATIC

#include "gtest/gtest.h"
#include <time.h>
#include <sys/time.h>

// ensure symbols are not mangled by C++
extern "C" {
#include "jsc/jpeglib.h"
#include "jsc/jsc_types_pub.h"
#include "jsc/jsc_pub.h"
#include "imageio.h"
#include "basic.h"
}

#define ABS(x) ( (x < 0) ? -(x) : (x) )
#define MIN(a,b) ( ( (a) < (b)) ? (a) : (b) )
#define MAX(a,b) ( ( (a) > (b)) ? (a) : (b) )

#define MAKESTMT(stuff)     do { stuff } while (0)

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
          V = ((unsigned int) GETJOCTET(*next_input_byte++)) << 8; \
          MAKE_BYTE_AVAIL(cinfo,action); \
          bytes_in_buffer--; \
          V += GETJOCTET(*next_input_byte++); )

typedef enum {
    JSC_TEST_SAMP_FACTOR_DEFAULT = 0, // leave as configured
    JSC_TEST_SAMP_FACTOR_111,
    JSC_TEST_SAMP_FACTOR_H2V2,
    JSC_TEST_SAMP_FACTOR_H2V1,
    JSC_TEST_SAMP_FACTOR_H1V2,
    JSC_TEST_SAMP_FACTOR_H3V1,
    JSC_TEST_SAMP_FACTOR_H1V3,
    JSC_TEST_SAMP_FACTOR_H4V4, // too much
} JscTestSampFactor;
enum {JSC_TEST_NUM_SAMP_FACTORS = JSC_TEST_SAMP_FACTOR_H4V4 + 1};

typedef enum {
    JSC_TEST_MARKER_NONE,
    JSC_TEST_MARKER_WRITE,
    JSC_TEST_MARKER_WRITE_SKIP,
    JSC_TEST_MARKER_PIECEMEAL,
} JscTestMarker;

// struct for storing the performance of compression
typedef struct {
    int n_input_pixels; // number of pixels (whatever the size)
    int n_input_values; // number of pixels * components per pixel
    int bits_per_pixel; // number of bits per pixel
    int n_input_bits; // n_pixels x bits per pixels
    int n_compressed_bits;
    double compression_ratio;

    int inexact_vals;
    double rmserr;
    int max_err;

    double compression_dur_cpu_s;
    double compression_dur_wall_s;
    double decompression_dur_cpu_s;
    double decompression_dur_wall_s;

} JpegCompStats;

typedef struct {
//    J_DCT_METHOD dct_method;  /* DCT algorithm selector */
    unsigned int restart_interval; /* MCUs per restart, or 0 for no restart */
    int restart_in_rows;      /* if > 0, MCU rows per restart interval */
} JpegAlternateParams;

double time_rgb_unoptimized = 0;
double time_rgb_optimized = 0;
double time_mono_unoptimized = 0;
double time_mono_optimized = 0;

// whether to print image buffers
bool print_images = true;

int performance_iters = 5;

struct jpeg_compress_struct img_cinfo;
struct jpeg_decompress_struct img_dinfo;
JscTestSampFactor samp_factor = JSC_TEST_SAMP_FACTOR_DEFAULT;
int current_quality;

JpegCompStats comp_stats;

bool use_alt_params = false;
JpegAlternateParams alt_params;
bool force_jpeg_color_space = false;
J_COLOR_SPACE forced_color_space = JCS_GRAYSCALE;
//bool do_subtract_green = false;
JscTestMarker marker_test = JSC_TEST_MARKER_NONE;

JSIZE input_img_bytes;         /* Number of bytes in image */
JSIZE output_img_bytes;         /* Number of bytes in image */

/* Image buffers */
JSAMPLE * img_buffer_truth = NULL; // ground truth
JSAMPLE * img_buffer_input = NULL; // input to compression
JSAMPLE * img_buffer_output = NULL; // output from decompression

/* compressed data output */
unsigned char * comp_buffer = NULL;
JSIZE comp_buffer_bytes;
JSIZE comp_bytes;

/* static working memory */
U8 * working_mem_buffer = NULL;
JSIZE working_mem_bytes;
JSIZE max_working_bytes_used_comp = 0;
JSIZE max_working_bytes_used_decomp = 0;

bool write_output_files = true;
int bmp_out_ctr = 0;
int jpeg_out_ctr = 0;

int trace_level = 0;


bool trust_cpu_time = true;

bool have_compressed_image = false;
int do_one_shot_comp_ctr = 20;
JINT n_restart_sections = 1;


// FIXME get rid of two times
double get_wall_time(void) {
//    struct timeval time;
//    int ret = gettimeofday(&time,NULL);
//    EXPECT_EQ(ret, 0);
//    return (double)time.tv_sec + (double)time.tv_usec * .000001;
    timespec time;
    int ret = clock_gettime(CLOCK_REALTIME, &time);
    EXPECT_EQ(ret, 0);
    return (double)time.tv_sec + (double)time.tv_nsec * .000000001;


}
double get_cpu_time(void) {
//    return (double)clock() / CLOCKS_PER_SEC;
    return get_wall_time();
}
// see jccolor.c:jinit_color_deconverter()
bool color_conv_supported_bak(J_COLOR_SPACE out, J_COLOR_SPACE in) {

    switch (out) {
    case JCS_GRAYSCALE:
      switch (in) {
      case JCS_GRAYSCALE:
      case JCS_YCbCr:
      case JCS_BG_YCC:
      case JCS_RGB:
          return true;
      default:
          return false;
      }
    case JCS_RGB:
      switch (in) {
      case JCS_GRAYSCALE:
      case JCS_YCbCr:
      case JCS_BG_YCC:
      case JCS_RGB:
          return true;
      default:
          return false;
      }
    case JCS_BG_RGB:
      return (in == JCS_BG_RGB);
    case JCS_CMYK:
      switch (in) {
      case JCS_YCCK:
      case JCS_CMYK:
          return true;
      default:
          return false;
      }
    default:      /* permit null conversion to same output space */
      return (in == out);
    }
}

// see jdcolor.c:jinit_color_deconverter()
bool color_deconv_supported(J_COLOR_SPACE out, J_COLOR_SPACE jpeg) {

    switch (out) {
    case JCS_GRAYSCALE:
      switch (jpeg) {
      case JCS_GRAYSCALE:
      case JCS_YCbCr:
      case JCS_BG_YCC:
      case JCS_RGB:
          return true;
      default:
          return false;
      }
    case JCS_RGB:
      switch (jpeg) {
      case JCS_GRAYSCALE:
      case JCS_YCbCr:
      case JCS_BG_YCC:
      case JCS_RGB:
          return true;
      default:
          return false;
      }
    case JCS_BG_RGB:
      return (jpeg == JCS_BG_RGB);
    case JCS_CMYK:
      switch (jpeg) {
      case JCS_YCCK:
      case JCS_CMYK:
          return true;
      default:
          return false;
      }
    default:      /* permit null conversion to same output space */
      return (jpeg == out);
    }
}

void jsc_test_apply_samp_factor(j_compress_ptr cinfo)
{
    if(samp_factor == JSC_TEST_SAMP_FACTOR_DEFAULT) {
        return; // no modifications
    }
    // set all factors to 1 to start
    for (int i= 0; i<MAX_COMPONENTS; i++) {
        cinfo->comp_info[i].h_samp_factor = 1;
        cinfo->comp_info[i].v_samp_factor = 1;
    }

    switch (samp_factor) {
        case JSC_TEST_SAMP_FACTOR_111:
            printf("sampling 111\n");
            // redundant
            cinfo->comp_info[0].h_samp_factor = 1;
            cinfo->comp_info[0].v_samp_factor = 1;
            break;
        case JSC_TEST_SAMP_FACTOR_H2V2:
            printf("sampling h2v2\n");
            cinfo->comp_info[0].h_samp_factor = 2;
            cinfo->comp_info[0].v_samp_factor = 2;
            break;
        case JSC_TEST_SAMP_FACTOR_H1V2:
            printf("sampling h1v2\n");
            cinfo->comp_info[0].h_samp_factor = 1;
            cinfo->comp_info[0].v_samp_factor = 2;
            break;
        case JSC_TEST_SAMP_FACTOR_H2V1:
            printf("sampling h2v1\n");
            cinfo->comp_info[0].h_samp_factor = 2;
            cinfo->comp_info[0].v_samp_factor = 1;
            break;
        case JSC_TEST_SAMP_FACTOR_H3V1:
            printf("sampling h3v1\n");
            cinfo->comp_info[0].h_samp_factor = 3;
            cinfo->comp_info[0].v_samp_factor = 1;

            break;
        case JSC_TEST_SAMP_FACTOR_H1V3:
            printf("sampling h1v3\n");
            cinfo->comp_info[0].h_samp_factor = 1;
            cinfo->comp_info[0].v_samp_factor = 3;
            break;
        case JSC_TEST_SAMP_FACTOR_H4V4:
            printf("sampling h4v4\n");
            cinfo->comp_info[0].h_samp_factor = 4;
            cinfo->comp_info[0].v_samp_factor = 4;
            break;
        default:
            break;
    }
}

JSC_METHODDEF(void)
jsc_test_progmon (j_common_ptr cinfo)
{
    if(cinfo->progress->pass_counter == cinfo->progress->pass_limit/2) {
        printf("Halfway: counter: %d limit: %d\n",
                cinfo->progress->pass_counter,
                cinfo->progress->pass_limit);
    }
}

JSC_METHODDEF(boolean)
read_com (j_decompress_ptr cinfo)
{
    INT32 length;
    INPUT_VARS(cinfo);

    INPUT_2BYTES(cinfo, length, return FALSE);
    length -= 2;

    unsigned char buf[100];
    memset(buf,0, 100);
    for (int i = 0; i < MIN(length, 100); i++) {
        INPUT_BYTE(cinfo, buf[i], return FALSE);
    }

    printf("COM, len %d: %s\n", length, buf);
    INPUT_SYNC(cinfo);

    return TRUE;
}


void set_global_params(J_COLOR_SPACE input_color_space,
        J_COLOR_SPACE out_color_space = JCS_UNKNOWN, int rows = 512, int cols =
                512) {
    img_cinfo.image_height = rows;
    img_cinfo.image_width = cols;
    if (samp_factor == JSC_TEST_SAMP_FACTOR_H4V4) {
        // need a bigger image
        img_cinfo.image_height = MAX(img_cinfo.image_height, 1024);
        img_cinfo.image_width = MAX(img_cinfo.image_width, 1024);
    }
    img_cinfo.in_color_space = input_color_space;
    switch (input_color_space) {
    case JCS_UNKNOWN:
        img_cinfo.input_components = 4;
        break;
    case JCS_GRAYSCALE:
        img_cinfo.input_components = 1;
        break;
    case JCS_RGB:
    case JCS_YCbCr:
    case JCS_BG_RGB:
    case JCS_BG_YCC:
        img_cinfo.input_components = 3;
        break;
    case JCS_CMYK:
    case JCS_YCCK:
        img_cinfo.input_components = 4;
        break;
    default:
        EXPECT_TRUE(false);
        break;
    }

//    if(out_color_space == JCS_UNKNOWN) {
//        img_dinfo.out_color_space = img_cinfo.in_color_space;
//    } else {
    img_dinfo.out_color_space = out_color_space;
//    }
    switch (img_dinfo.out_color_space) {
    case JCS_GRAYSCALE:
        img_dinfo.out_color_components = 1;
        break;
    case JCS_RGB:
    case JCS_YCbCr:
    case JCS_BG_RGB:
    case JCS_BG_YCC:
        img_dinfo.out_color_components = 3;
        break;
    case JCS_CMYK:
    case JCS_YCCK:
        img_dinfo.out_color_components = 4;
        break;
    default:
        img_dinfo.out_color_components = 10;
        break;

        //        default:
//            EXPECT_TRUE(false);
//            break;
    }

    printf("rows: %d, cols: %d, components: %d\n",
            img_cinfo.image_height, img_cinfo.image_width, img_cinfo.input_components);
}

void assert_global_buffs_alloced(void)
{
    ASSERT_TRUE(img_buffer_truth != NULL);
    ASSERT_TRUE(img_buffer_input != NULL);
    ASSERT_TRUE(img_buffer_output != NULL);
    ASSERT_TRUE(comp_buffer != NULL);
    ASSERT_TRUE(working_mem_buffer != NULL);
}

void alloc_global_bufs(void)
{
    // assert not abandoning memory
    ASSERT_TRUE(img_buffer_truth == NULL);
    ASSERT_TRUE(img_buffer_input == NULL);
    ASSERT_TRUE(img_buffer_output == NULL);
    ASSERT_TRUE(comp_buffer == NULL);
    ASSERT_TRUE(working_mem_buffer == NULL);

    input_img_bytes = img_cinfo.image_height * img_cinfo.image_width * sizeof(JSAMPLE)
                * img_cinfo.input_components;
    output_img_bytes = img_cinfo.image_height * img_cinfo.image_width * sizeof(JSAMPLE)
                * img_dinfo.out_color_components;

    comp_buffer_bytes = 20000 + input_img_bytes * 1.1; // FIXME handwavy
//    working_mem_bytes = 20000 + input_img_bytes; // FIXME handwavy
    comp_buffer_bytes = input_img_bytes;

    working_mem_bytes = JSC_WORKING_MEM_SIZE(
            MAX(img_cinfo.input_components,img_dinfo.out_color_components),
            img_cinfo.image_width);

    // alternative sampling factors -> jack up the memory
    if(use_alt_params || samp_factor != JSC_TEST_SAMP_FACTOR_DEFAULT) {
        working_mem_bytes *= 2;
    }


    img_buffer_truth  = (JSAMPLE *) malloc(input_img_bytes);
    img_buffer_input  = (JSAMPLE *) malloc(input_img_bytes);
    img_buffer_output = (JSAMPLE *) malloc(output_img_bytes);
    comp_buffer = (U8 *)malloc(comp_buffer_bytes);
    working_mem_buffer = (U8 *)malloc(working_mem_bytes);

    assert_global_buffs_alloced();

    memset(img_buffer_truth, 0x42, input_img_bytes);
    memset(img_buffer_input, 0x42, input_img_bytes);
    memset(img_buffer_output, 0x42, output_img_bytes);
    memset(comp_buffer, 0x42, comp_buffer_bytes);
    memset(working_mem_buffer, 0x42, working_mem_bytes);

    have_compressed_image = false;

}

void free_global_bufs(void)
{
    assert_global_buffs_alloced();

    free(img_buffer_truth);
    free(img_buffer_input);
    free(img_buffer_output);
    free(comp_buffer);
    free(working_mem_buffer);

    img_buffer_truth = NULL;
    img_buffer_input = NULL;
    img_buffer_output = NULL;
    comp_buffer = NULL;
    working_mem_buffer = NULL;

    have_compressed_image = false;
}

// write current comp buffer to file
void write_jpeg_out(void)
{
    assert_global_buffs_alloced();
    ASSERT_TRUE(have_compressed_image);

    if(write_output_files && jpeg_out_ctr < 1000) {
        FILE* file;
        char filename_out[80];
        sprintf(filename_out, "Testing/Temporary/jpeg_out%03d_cs%d_q%d.jpeg",
                jpeg_out_ctr++, img_cinfo.in_color_space, current_quality);
        file = fopen(filename_out,"wb");
        if(file != NULL) {
            fwrite(comp_buffer, comp_bytes,1,file);
            fclose(file);
            printf("Wrote image %s.\n", filename_out);
        }
    }
}

bool do_compress(int quality)
{
    printf("do_compress()!\n");
    current_quality = quality;

    assert_global_buffs_alloced();

    double start_cpu;
    double start_wall;
    double end_cpu;
    double end_wall;

    comp_stats.n_input_pixels = img_cinfo.image_width * img_cinfo.image_height;
    comp_stats.n_input_values = comp_stats.n_input_pixels * img_cinfo.input_components;
    comp_stats.bits_per_pixel = sizeof(JSAMPLE)*8*img_cinfo.input_components;
    comp_stats.n_input_bits = comp_stats.n_input_pixels * comp_stats.bits_per_pixel;

    start_cpu = get_cpu_time();
    start_wall = get_wall_time();
    // FIXME would be nice if there was one operation for time getting to sandwich

    if(do_one_shot_comp_ctr) {
        printf("compress with one function\n");
        do_one_shot_comp_ctr--;

        JscImage image;
        image.width = img_cinfo.image_width;  /* image width and height, in pixels */
        image.height = img_cinfo.image_height;
        image.n_components = img_cinfo.input_components;   /* # of color components per pixel */
        image.color_space = img_cinfo.in_color_space;       /* colorspace of input image */
        image.data = img_buffer_input;

        JscBuf output_mem;
        output_mem.size_bytes = comp_buffer_bytes;
        output_mem.n_bytes_used = 0;
        output_mem.data = comp_buffer;

        JscBuf working_mem;
        working_mem.size_bytes = working_mem_bytes;
        working_mem.n_bytes_used = 0;
        working_mem.data = working_mem_buffer;

        I32 ret;
        if(n_restart_sections > 1) {
            ret = jsc_compress_rst(&image, &output_mem, &working_mem,
                    quality, n_restart_sections);
        } else {
            ret = jsc_compress(&image, &output_mem, &working_mem, quality);
        }
        if (ret != 0) {
            printf("one shot compress failure\n");
            printf("used %u / %u output bytes\n",
                    output_mem.n_bytes_used, output_mem.size_bytes);
            return false;
        }

        comp_bytes = output_mem.n_bytes_used;

        end_cpu = get_cpu_time();
        end_wall = get_wall_time();
        if(end_cpu == start_cpu) {
            trust_cpu_time = false;
        }

        printf("Used %u working_bytes, %u output bytes\n",
                working_mem.n_bytes_used, output_mem.n_bytes_used);

    } else {


        struct jpeg_compress_struct cinfo;
        struct jpeg_static_memory statmem;
        jpeg_progress_mgr jprog;
        JSAMPROW row_pointer[1];      /* pointer to JSAMPLE row[s] */
        int row_stride;               /* physical row width in image buffer */


        /* Step 1: allocate and initialize JPEG compression object */
        cinfo.statmem = jpeg_give_static_mem(&statmem, working_mem_buffer, working_mem_bytes);
        jpeg_create_compress(&cinfo);

        jprog.progress_monitor = jsc_test_progmon;
        cinfo.progress = &jprog;
        cinfo.trace_level = trace_level;

        /* Step 2: specify data destination */
        comp_bytes = comp_buffer_bytes;
        jpeg_mem_dest(&cinfo, &comp_buffer, &comp_bytes);

        /* Step 3: set parameters for compression */
        cinfo.image_width = img_cinfo.image_width;  /* image width and height, in pixels */
        cinfo.image_height = img_cinfo.image_height;
        cinfo.input_components = img_cinfo.input_components;   /* # of color components per pixel */
        cinfo.in_color_space = img_cinfo.in_color_space;       /* colorspace of input image */

        jpeg_set_defaults(&cinfo);

        jpeg_set_quality(&cinfo, quality, TRUE /* limit to baseline-JPEG values */);

        if(use_alt_params) {
            cinfo.restart_interval = alt_params.restart_interval;
            cinfo.restart_in_rows = alt_params.restart_in_rows;
        }
        if(force_jpeg_color_space) {
            printf("setting jpeg colorspace to %d\n", forced_color_space);
            jpeg_set_colorspace(&cinfo, forced_color_space);
        }

        // apply alternative sampling factors
        jsc_test_apply_samp_factor(&cinfo);

        /* Step 4: Start compressor */
        jpeg_start_compress(&cinfo, TRUE);

        // write special marker
        const char* text = "Compressed with JSC.";
        const unsigned char* utext =
                reinterpret_cast<const unsigned char *>( text );
        int stridx;
        switch (marker_test) {
            case JSC_TEST_MARKER_WRITE:
            case JSC_TEST_MARKER_WRITE_SKIP:
                printf("writing marker, len %ld\n", strlen(text));
                jpeg_write_marker(&cinfo, JPEG_COM, utext, strlen(text));
                break;
            case JSC_TEST_MARKER_PIECEMEAL:
                printf("writing marker piecemeal\n");
                jpeg_write_m_header(&cinfo, JPEG_COM, strlen(text));
                for (stridx = 0; stridx < strlen(text); stridx++) {
                    jpeg_write_m_byte(&cinfo, text[stridx]);
                }
                break;
            default:
                // do nothing
                break;
        }

        /* Step 5: while (scan lines remain to be written) */
        /*           jpeg_write_scanlines(...); */

        /* JSAMPLEs per row in image_buffer */
        row_stride = cinfo.image_width * cinfo.input_components;
        int cnt = 0;
        while (cinfo.next_scanline < cinfo.image_height) {
          /* jpeg_write_scanlines expects an array of pointers to scanlines.
           * Here the array is only one element long, but you could pass
           * more than one scanline at a time if that's more convenient.
           */
          row_pointer[0] = & img_buffer_input[cinfo.next_scanline * row_stride];
          JDIMENSION rows_written =  jpeg_write_scanlines(&cinfo, row_pointer, 1);
          if(rows_written != 1) {
              printf("failed to write scanlines, rows_written = %d, "
                      "bytes used: %d / %d\n",
                      rows_written, comp_bytes-cinfo.dest->free_in_buffer, comp_bytes);
              have_compressed_image = false;
              return false;
          }

          cnt++;
          if (cnt == 1) {
              printf("After 1st jpeg_write scanlines, used %u working_bytes, %u output bytes\n",
                cinfo.statmem->bytes_used, comp_bytes-cinfo.dest->free_in_buffer);
          }
        }

        /* Step 6: Finish compression */
        jpeg_finish_compress(&cinfo);

        /* Step 7: release JPEG compression object */
        jpeg_destroy_compress(&cinfo);

        end_cpu = get_cpu_time();
        end_wall = get_wall_time();
        if(end_cpu == start_cpu) {
            trust_cpu_time = false;
        }

        if(max_working_bytes_used_comp < cinfo.statmem->bytes_used) {
            max_working_bytes_used_comp = cinfo.statmem->bytes_used;
        }
        printf("Compression used %u / %u working_mem_bytes, max %u \n",
                cinfo.statmem->bytes_used, working_mem_bytes,
                max_working_bytes_used_comp);
    }

    comp_stats.compression_dur_cpu_s = end_cpu - start_cpu;
    comp_stats.compression_dur_wall_s = end_wall - start_wall;

    comp_stats.n_compressed_bits = comp_bytes * 8;
    comp_stats.compression_ratio =
            (double)comp_stats.n_input_bits / (double)comp_stats.n_compressed_bits;

    printf("comp_buffer_bytes = %u\n", comp_buffer_bytes);

    printf("input size: %d * %d * %d = %d B, "
            "compressed size: %u B, ratio: %f, "
            "time: %f s, compression rate: %f Mb/s\n",
            img_cinfo.image_width, img_cinfo.image_height,
            img_cinfo.input_components, comp_stats.n_input_bits/8,
            comp_bytes,
            comp_stats.compression_ratio,
            trust_cpu_time ? comp_stats.compression_dur_cpu_s :
                             comp_stats.compression_dur_wall_s,
            1e-6 * comp_stats.n_input_bits / (
                    trust_cpu_time ? comp_stats.compression_dur_cpu_s :
                                     comp_stats.compression_dur_wall_s));

    have_compressed_image = true;

    write_jpeg_out();

    return true;
}

bool do_decompress(void) {
    printf("do_decompress()!\n");

    assert_global_buffs_alloced();

    struct jpeg_decompress_struct dinfo;
    struct jpeg_static_memory statmem;
    JSAMPROW row_pointer[1]; /* pointer to JSAMPLE row[s] */
    int row_stride; /* physical row width in output buffer */

    double start_cpu;
    double start_wall;
    double end_cpu;
    double end_wall;

    int ret;

    start_cpu = get_cpu_time();
    start_wall = get_wall_time();

    /* Step 1: allocate and initialize JPEG decompression object */
    dinfo.statmem = jpeg_give_static_mem(&statmem, working_mem_buffer, working_mem_bytes);
    jpeg_create_decompress(&dinfo);

    jpeg_progress_mgr jprog;
    jprog.progress_monitor = jsc_test_progmon;
    dinfo.progress = &jprog;
    dinfo.trace_level = trace_level;

    if(marker_test == JSC_TEST_MARKER_WRITE
            || marker_test == JSC_TEST_MARKER_PIECEMEAL) {
        jpeg_set_marker_processor(&dinfo, JPEG_COM, read_com);
    }

    /* Step 2: specify data source */
    jpeg_mem_src(&dinfo, comp_buffer, comp_bytes);
    int input_complete = jpeg_input_complete(&dinfo);

    /* Step 3: read file parameters with jpeg_read_header() */
    ret = jpeg_read_header(&dinfo, TRUE);
    boolean multiple_scans = jpeg_has_multiple_scans(&dinfo);

    /* Step 4: set parameters for decompression */



    if(img_dinfo.out_color_space == JCS_UNKNOWN) {
        img_dinfo.out_color_space = dinfo.out_color_space;
    } else {
        // change from default
        dinfo.out_color_space = img_dinfo.out_color_space;
    }
    printf("jpeg color space: %d, output: %d\n",
            dinfo.jpeg_color_space, dinfo.out_color_space);

    if (!color_deconv_supported(dinfo.out_color_space,dinfo.jpeg_color_space)) {
        printf("Can't decompress from color space %d to %d\n",
                dinfo.jpeg_color_space,dinfo.out_color_space);
        return false;
    }

    /* Step 5: Start decompressor */
    boolean bret = jpeg_start_decompress(&dinfo);
    row_stride = dinfo.output_width * dinfo.output_components;

    /* Step 6: while (scan lines remain to be read) */
    /*           jpeg_read_scanlines(...); */
    int cnt = 0;
    while (dinfo.output_scanline < dinfo.output_height) {
        /* jpeg_read_scanlines expects an array of pointers to scanlines.
         * Here the array is only one element long, but you could ask for
         * more than one scanline at a time if that's more convenient.
         */
        row_pointer[0] = &img_buffer_output[dinfo.output_scanline * row_stride];
        (void) jpeg_read_scanlines(&dinfo, row_pointer, 1);
//        /* Assume put_scanline_someplace wants a pointer and sample count. */
//        put_scanline_someplace(buffer[0], row_stride);
        cnt++;
        if(cnt == 1) {
            printf("After jpeg_read_scanlines 1, used %u working_bytes, %u output bytes\n",
                    dinfo.statmem->bytes_used, comp_bytes-dinfo.src->bytes_in_buffer);
        }
    }

    printf("After jpeg_read_scanlines, used %u working_bytes, %u output bytes\n",
            dinfo.statmem->bytes_used, comp_bytes-dinfo.src->bytes_in_buffer);

    /* Step 7: Finish decompression */
    (void) jpeg_finish_decompress(&dinfo);
    input_complete = jpeg_input_complete(&dinfo);

    /* Step 8: Release JPEG decompression object */
    jpeg_destroy_decompress(&dinfo);

    printf("After jpeg_destroy_decompress, used %u working_bytes, %u output bytes\n",
            dinfo.statmem->bytes_used, comp_bytes-dinfo.src->bytes_in_buffer);


    end_cpu = get_cpu_time();
    end_wall = get_wall_time();
    if(end_cpu == start_cpu) {
         trust_cpu_time = false;
    }

    comp_stats.decompression_dur_cpu_s = end_cpu - start_cpu;
    comp_stats.decompression_dur_wall_s = end_wall - start_wall;

    printf("decompression time: %f s, rate: %f Mb/s\n",
            trust_cpu_time ? comp_stats.decompression_dur_cpu_s :
                             comp_stats.decompression_dur_wall_s,
            1e-6 * comp_stats.n_input_bits / (
                    trust_cpu_time ?
                                     comp_stats.decompression_dur_cpu_s :
                                     comp_stats.decompression_dur_wall_s));

    printf("saved info rate: %f Mb/s\n",
            1e-6 * (comp_stats.n_input_bits - comp_stats.n_compressed_bits) / (
                    trust_cpu_time ?
                            (comp_stats.compression_dur_cpu_s
                                    + comp_stats.decompression_dur_cpu_s) :
                            (comp_stats.compression_dur_wall_s
                                    + comp_stats.decompression_dur_wall_s)));

    if(max_working_bytes_used_decomp < dinfo.statmem->bytes_used) {
        max_working_bytes_used_decomp = dinfo.statmem->bytes_used;
    }
    printf("Decompression used %u / %u working_mem_bytes, max %u \n",
            dinfo.statmem->bytes_used, working_mem_bytes,
            max_working_bytes_used_decomp);


    return true;
}

void check_error(double allowed_rmserr = 0)
        {
    assert_global_buffs_alloced();

    if(img_cinfo.in_color_space != img_dinfo.out_color_space) {
        printf("input and output color spaces do not match"
                " - can't check error\n");
        return;
    }

    printf("Check error\n");
    int n_not_exact = 0;
    double rmserr = 0;
    int max_err = 0;
    int n_vals = img_cinfo.image_height * img_cinfo.image_width
                * img_cinfo.input_components;

    for (int idx = 0; idx < n_vals; idx++) {
        int diff = (int)img_buffer_truth[idx] - (int)img_buffer_output[idx];
        if (diff != 0) {
            n_not_exact++;
            if (ABS(diff) > max_err) {
                max_err = ABS(diff);
//                max_err_row = row;
//                max_err_col = col;
            }
            rmserr += diff * diff;
        }

    }

    rmserr = sqrt(rmserr / n_vals);

    printf("RMS err = %f, allowed = %f, n_not_exact = %d / %d = %f\n",
            rmserr, allowed_rmserr, n_not_exact, n_vals,
            (double) n_not_exact / (double) n_vals);
    EXPECT_LE(rmserr, allowed_rmserr);

    if (max_err > 0) {
        printf("Maximum error of %d.\n", max_err);
    }

    int middle_idx = ((img_cinfo.image_height / 2 * img_cinfo.image_width) + img_cinfo.image_width / 2) * img_cinfo.input_components;
    printf("truth middle: ");
    for (int i = 0; i < img_cinfo.input_components; i++) {
        printf("%d ", img_buffer_truth[middle_idx + i]);
    }
    printf("decompressed middle: ");
    for (int i = 0; i < img_cinfo.input_components; i++) {
        printf("%d ", img_buffer_output[middle_idx + i]);
    }
    printf("\n");

//
//    printf("truth middle: %d decompressed middle: %d\n",
//            img_buffer_truth[(img_cinfo.image_height / 2 * img_cinfo.image_width) + img_cinfo.image_width / 2],
//            img_buffer_output[(img_cinfo.image_height / 2 * img_cinfo.image_width) + img_cinfo.image_width / 2]);



     comp_stats.inexact_vals = n_not_exact;
     comp_stats.rmserr = rmserr;
     comp_stats.max_err = max_err;
}

void test_compress_decompress_error(int quality, double allowed_rmserr=0xFF)
{
    assert_global_buffs_alloced();
    bool compressed = do_compress(quality);
    if(!compressed) {
        return;
    }
    bool decompressed = do_decompress();
    if(decompressed) {
        check_error(allowed_rmserr);
    }
}

void make_single_color_input(unsigned char color[4])
{
    assert_global_buffs_alloced();
    int n_vals = img_cinfo.image_height * img_cinfo.image_width
                * img_cinfo.input_components;

    for (int idx = 0; idx < n_vals; idx+=img_cinfo.input_components) {
        for (int component = 0; component < img_cinfo.input_components; component++) {
            img_buffer_truth[idx+component] =
                    img_buffer_input[idx+component] = color[component];
        }
    }
}

void make_random_input(unsigned char max_val = 0xFF)
{
    assert_global_buffs_alloced();
    int n_pix = img_cinfo.image_height * img_cinfo.image_width
                * img_cinfo.input_components;

    for (int idx = 0; idx < n_pix; idx++) {
        int color = rand() % max_val;
        img_buffer_truth[idx] = color;
        img_buffer_input[idx] = color;
    }
}

void test_random(J_COLOR_SPACE in_color_space, int quality = 85,
        unsigned char max_val = 0xFF,
        J_COLOR_SPACE out_color_space = JCS_UNKNOWN,
        int rows = 512, int cols = 512)
{
    printf("\nTest compression of random with input color space %d, "
            "output color space %d, quality = %d.\n",
            in_color_space, out_color_space, quality);
    set_global_params(in_color_space, out_color_space, rows, cols);
    alloc_global_bufs();
    make_random_input(max_val);
    test_compress_decompress_error(quality);
    free_global_bufs();
}

void test_single_color(J_COLOR_SPACE in_color_space, unsigned char color[4],
        J_COLOR_SPACE out_color_space = JCS_UNKNOWN, int quality = 85,
        int rows = 512, int cols = 512)
{
    printf("\nTest compression of single color with input color space %d, "
            "output color space %d,quality = %d.\n",
            in_color_space, out_color_space, quality);
    set_global_params(in_color_space, out_color_space, rows, cols);
    alloc_global_bufs();
    make_single_color_input(color);
    test_compress_decompress_error(quality, 8);
    free_global_bufs();
}

void load_bmp(J_COLOR_SPACE in_color_space,
        J_COLOR_SPACE out_color_space = JCS_UNKNOWN,
        const char * filename = "../test/frog.bmp")
{
    int n_cols = 0;
    int n_rows = 0;
    uint32_t* image = NULL;

    image = (uint32_t*) ReadImage(&n_cols, &n_rows,
            filename, IMAGEIO_U8 | IMAGEIO_RGBA);
    ASSERT_TRUE(image != NULL);

    set_global_params(in_color_space, out_color_space, n_rows, n_cols);

    alloc_global_bufs();

    int n_pix = img_cinfo.image_height * img_cinfo.image_width;
    JSAMPLE val;
    for (int pixel = 0; pixel < n_pix; pixel++) {
        uint8_t* u8p = (uint8_t*)(&image[pixel]);
        uint8_t red = u8p[0];
        uint8_t green = u8p[1];
        uint8_t blue = u8p[2];

        switch (in_color_space) {
        case JCS_RGB:
            img_buffer_input[pixel*3] = img_buffer_truth[pixel*3] = red;
            img_buffer_input[pixel*3+1] = img_buffer_truth[pixel*3+1] = green;
            img_buffer_input[pixel*3+2] = img_buffer_truth[pixel*3+2] = blue;
            break;
        case JCS_GRAYSCALE:
            val = (JSAMPLE)(red * .3 + green * .6 + blue * .1);
            img_buffer_input[pixel] = img_buffer_truth[pixel] = val;
            break;
        default:
            ASSERT_TRUE(false);
            break;
        }
    }

    free(image);
}

void write_output_bmp(char * filename_out)
{
    uint32_t *out_rgba = (uint32_t*) malloc(
            img_cinfo.image_height * img_cinfo.image_width * 4);
    ASSERT_TRUE(out_rgba != NULL);

    int n_pix = img_cinfo.image_height * img_cinfo.image_width;
    for (int pixel = 0; pixel < n_pix; pixel++) {
        uint8_t* u8p = (uint8_t*)(&out_rgba[pixel]);
        switch (img_dinfo.out_color_space) {
        case JCS_RGB:
            u8p[0] = img_buffer_output[pixel*3] & 0xFF;
            u8p[1] = img_buffer_output[pixel*3+1] & 0xFF;
            u8p[2] = img_buffer_output[pixel*3+2] & 0xFF;
            u8p[3] = 0xFF;
            break;
        case JCS_GRAYSCALE:
            u8p[0] = img_buffer_output[pixel] & 0xFF;
            u8p[1] = img_buffer_output[pixel] & 0xFF;
            u8p[2] = img_buffer_output[pixel] & 0xFF;
            u8p[3] = 0xFF;
            break;
        default:
            ASSERT_TRUE(false);
            break;
        }
    }

    int ret = WriteImage(out_rgba,
            img_cinfo.image_width, img_cinfo.image_height,
            filename_out,  IMAGEIO_U8 | IMAGEIO_RGBA, 85);
    EXPECT_EQ(ret,1);
    printf("Wrote image %s.\n", filename_out);

    free(out_rgba);
}

void test_image_bmp(J_COLOR_SPACE in_color_space,
        int quality = 85,
        J_COLOR_SPACE out_color_space = JCS_UNKNOWN,
        const char * filename = "../test/frog.bmp")
{
    printf("\nTest compression with image %s, input color space %d, "
            "output color space %d, quality = %d.\n",
            filename, in_color_space, out_color_space, quality);

    load_bmp(in_color_space, out_color_space, filename);
    int n_pix = img_cinfo.image_height * img_cinfo.image_width;

    test_compress_decompress_error(quality, 25);

    if(write_output_files && bmp_out_ctr < 100
            && (img_dinfo.out_color_space == JCS_RGB
                    || img_dinfo.out_color_space == JCS_GRAYSCALE)) {
        char filename_out[80];
        sprintf(filename_out, "Testing/Temporary/bmp_out%03d_cs%d_q%d.bmp",
                bmp_out_ctr++, in_color_space, quality);
        write_output_bmp(filename_out);
    }

    free_global_bufs();
}


TEST(JSCTest, Misc) {
    printf("Hello World\n");

    I32 pos = 42;
    I32 neg = -42;

    I32 pos_rs = pos >> 1;
    I32 neg_rs = neg >> 1;

    printf("pos:    %d %x\n", pos, pos);
    printf("neg:    %d %x\n", neg, neg);
    printf("pos_rs: %d %x\n", pos_rs, pos_rs);
    printf("neg_rs: %d %x\n", neg_rs, neg_rs);


    printf("sizeof(size_t)=%lu\n", sizeof(size_t));
    printf("sizeof(JSIZE)=%lu\n", sizeof(JSIZE));
    printf("sizeof(int)=%lu\n", sizeof(int));
    printf("sizeof(long)=%lu\n", sizeof(long));


}

TEST(JSCTest, Random) {

    test_random(JCS_GRAYSCALE);
    test_random(JCS_RGB);
    test_random(JCS_YCbCr);
    test_random(JCS_CMYK);

    // test big
    printf("rgb0064\n");
    test_random(JCS_RGB,       75, 0xFF, JCS_UNKNOWN,  64, 64);
    printf("rgb0128\n");
    test_random(JCS_RGB,       75, 0xFF, JCS_UNKNOWN,  128, 128);
    printf("rgb0256\n");
    test_random(JCS_RGB,       75, 0xFF, JCS_UNKNOWN,  256, 256);
    printf("rgb0512\n");
    test_random(JCS_RGB,       75, 0xFF, JCS_UNKNOWN,  512, 512);
    printf("rgb1024\n");
    test_random(JCS_RGB,       75, 0xFF, JCS_UNKNOWN, 1024, 1024);
    printf("rgb2048\n");
    test_random(JCS_RGB,       75, 0xFF, JCS_UNKNOWN, 2048, 2048);
    printf("rgb4096\n");
    test_random(JCS_RGB,       75, 0xFF, JCS_UNKNOWN, 4096, 4096);

    printf("\nhigh quality random should fill buffer\n");
    test_random(JCS_GRAYSCALE,       95, 0xFF, JCS_UNKNOWN,  64, 64);
    test_random(JCS_GRAYSCALE,       95, 0xFF, JCS_UNKNOWN,  128, 128);
    test_random(JCS_GRAYSCALE,       95, 0xFF, JCS_UNKNOWN,  256, 256);
    test_random(JCS_GRAYSCALE,       95, 0xFF, JCS_UNKNOWN,  512, 512);
    test_random(JCS_GRAYSCALE,       95, 0xFF, JCS_UNKNOWN, 1024, 1024);
    test_random(JCS_GRAYSCALE,       95, 0xFF, JCS_UNKNOWN, 2048, 2048);

    test_random(JCS_GRAYSCALE,      100, 0xFF, JCS_UNKNOWN,  64, 64);
    test_random(JCS_GRAYSCALE,      100, 0xFF, JCS_UNKNOWN,  128, 128);
    test_random(JCS_GRAYSCALE,      100, 0xFF, JCS_UNKNOWN,  256, 256);
    test_random(JCS_GRAYSCALE,      100, 0xFF, JCS_UNKNOWN,  512, 512);
    test_random(JCS_GRAYSCALE,      100, 0xFF, JCS_UNKNOWN, 1024, 1024);
    test_random(JCS_GRAYSCALE,      100, 0xFF, JCS_UNKNOWN, 2048, 2048);

    test_random(JCS_RGB,       95, 0xFF, JCS_UNKNOWN,  64, 64);
    test_random(JCS_RGB,       95, 0xFF, JCS_UNKNOWN,  128, 128);
    test_random(JCS_RGB,       95, 0xFF, JCS_UNKNOWN,  256, 256);
    test_random(JCS_RGB,       95, 0xFF, JCS_UNKNOWN,  512, 512);
    test_random(JCS_RGB,       95, 0xFF, JCS_UNKNOWN, 1024, 1024);
    test_random(JCS_RGB,       95, 0xFF, JCS_UNKNOWN, 2048, 2048);

    test_random(JCS_RGB,      100, 0xFF, JCS_UNKNOWN,  64, 64);
    test_random(JCS_RGB,      100, 0xFF, JCS_UNKNOWN,  128, 128);
    test_random(JCS_RGB,      100, 0xFF, JCS_UNKNOWN,  256, 256);
    test_random(JCS_RGB,      100, 0xFF, JCS_UNKNOWN,  512, 512);
    test_random(JCS_RGB,      100, 0xFF, JCS_UNKNOWN, 1024, 1024);
    test_random(JCS_RGB,      100, 0xFF, JCS_UNKNOWN, 2048, 2048);


    printf("rgb1023\n");
    test_random(JCS_RGB,       75, 0xFF, JCS_UNKNOWN, 1023, 1023);

    printf("rgb1025\n");
    test_random(JCS_RGB,       75, 0xFF, JCS_UNKNOWN, 1025, 1025);


    printf("rgb1024x512\n");
    test_random(JCS_RGB,       75, 0xFF, JCS_UNKNOWN, 1024, 512);
    printf("rgb512x1024\n");
    test_random(JCS_RGB,       75, 0xFF, JCS_UNKNOWN, 512, 1024);

    printf("gray0064\n");
    test_random(JCS_GRAYSCALE,       75, 0xFF, JCS_UNKNOWN,  64, 64);
    printf("gray0128\n");
    test_random(JCS_GRAYSCALE,       75, 0xFF, JCS_UNKNOWN,  128, 128);
    printf("gray0256\n");
    test_random(JCS_GRAYSCALE,       75, 0xFF, JCS_UNKNOWN,  256, 256);
    printf("gray0512\n");
    test_random(JCS_GRAYSCALE,       75, 0xFF, JCS_UNKNOWN,  512, 512);
    printf("gray1024\n");
    test_random(JCS_GRAYSCALE,       75, 0xFF, JCS_UNKNOWN, 1024, 1024);
    printf("gray2048\n");
    test_random(JCS_GRAYSCALE,       75, 0xFF, JCS_UNKNOWN, 2048, 2048);
    printf("gray4096\n");
    test_random(JCS_GRAYSCALE,       75, 0xFF, JCS_UNKNOWN, 4096, 4096);

    printf("gray1023\n");
    test_random(JCS_GRAYSCALE,       75, 0xFF, JCS_UNKNOWN, 1023, 1023);

    printf("gray1025\n");
    test_random(JCS_GRAYSCALE,       75, 0xFF, JCS_UNKNOWN, 1025, 1025);


    printf("gray1024x512\n");
    test_random(JCS_GRAYSCALE,       75, 0xFF, JCS_UNKNOWN, 1024, 512);
    printf("gray512x1024\n");
    test_random(JCS_GRAYSCALE,       75, 0xFF, JCS_UNKNOWN, 512, 1024);


    // test sizes not divisible by 8
    test_random(JCS_GRAYSCALE, 75, 0xFF, JCS_UNKNOWN, 513, 513);
    test_random(JCS_RGB,       75, 0xFF, JCS_UNKNOWN, 514, 514);
    test_random(JCS_GRAYSCALE, 75, 0xFF, JCS_UNKNOWN, 513, 514);
    test_random(JCS_GRAYSCALE, 75, 0xFF, JCS_UNKNOWN, 514, 513);
    test_random(JCS_GRAYSCALE, 75, 0xFF, JCS_UNKNOWN, 511, 511);
    test_random(JCS_GRAYSCALE, 75, 0xFF, JCS_UNKNOWN, 510, 510);
    test_random(JCS_RGB,       75, 0xFF, JCS_UNKNOWN, 510, 511);
    test_random(JCS_GRAYSCALE, 75, 0xFF, JCS_UNKNOWN, 511, 510);




    use_alt_params = true;
//    alt_params.dct_method = JDCT_ISLOW;
//    alt_params.restart_interval = 0;
//    alt_params.restart_in_rows = 0;
//    test_random(JCS_GRAYSCALE);
//    test_random(JCS_RGB);
//
//    alt_params.dct_method = JDCT_IFAST;
//    test_random(JCS_GRAYSCALE);
//    test_random(JCS_RGB);


//    alt_params.dct_method = JDCT_FLOAT;
    printf("\n~10 restarts per image\n");
    alt_params.restart_interval = 512 * 512 / (8*8*10);
    alt_params.restart_in_rows = 0;
    test_random(JCS_GRAYSCALE);
    test_random(JCS_RGB);
    alt_params.restart_interval = 0;
    alt_params.restart_in_rows = 512/10;
    test_random(JCS_GRAYSCALE);
    test_random(JCS_RGB);

    use_alt_params = false;

    printf("\nforce jpeg color space gray\n");
    force_jpeg_color_space = true;
    forced_color_space = JCS_GRAYSCALE;
    test_random(JCS_GRAYSCALE);
    test_random(JCS_RGB);
    force_jpeg_color_space = false;

    printf("\nforce rgb->bg_ycc\n");
    force_jpeg_color_space = true;
    forced_color_space = JCS_BG_YCC;
    test_random(JCS_RGB);

    for (int i = JSC_TEST_SAMP_FACTOR_111; i <= JSC_TEST_SAMP_FACTOR_H1V3; i++) {
        printf("\nforce rgb->bg_ycc, samp factor %d\n", i);
        samp_factor = (JscTestSampFactor)i;
        test_random(JCS_RGB);
    }
    samp_factor = JSC_TEST_SAMP_FACTOR_DEFAULT;

    force_jpeg_color_space = false;



    printf("\nforce ycbcr->bg_ycc\n");
    force_jpeg_color_space = true;
    forced_color_space = JCS_BG_YCC;
    test_random(JCS_YCbCr);
    force_jpeg_color_space = false;

    printf("\nforce jpeg color space rgb\n");
    force_jpeg_color_space = true;
    forced_color_space = JCS_RGB;
    test_random(JCS_RGB);
    force_jpeg_color_space = false;

    printf("\nsubtract green\n");
    force_jpeg_color_space = true;
    forced_color_space = JCS_RGB;
//    do_subtract_green = true;
//    test_random(JCS_RGB, 85, 0xFF);
//    do_subtract_green = false;
    force_jpeg_color_space = false;


    printf("\nforce jpeg color space rgb -> decompress to gray\n");
    force_jpeg_color_space = true;
    forced_color_space = JCS_RGB;
    test_random(JCS_RGB, 75,0xFF,JCS_GRAYSCALE);
    force_jpeg_color_space = false;

    printf("\nforce jpeg color space rgb (subtract green) -> decompress to gray\n");
    force_jpeg_color_space = true;
    forced_color_space = JCS_RGB;
//    do_subtract_green = true;
//    test_random(JCS_RGB, 75,0xFF,JCS_GRAYSCALE);
//    do_subtract_green = false;
    force_jpeg_color_space = false;






    marker_test = JSC_TEST_MARKER_WRITE;
    test_random(JCS_GRAYSCALE);
    marker_test = JSC_TEST_MARKER_WRITE_SKIP;
    test_random(JCS_GRAYSCALE);
    marker_test = JSC_TEST_MARKER_PIECEMEAL;
    test_random(JCS_GRAYSCALE);
    marker_test = JSC_TEST_MARKER_NONE;



    for (int in = JCS_UNKNOWN; in <= JCS_BG_YCC; in++) {
        for (int out = JCS_UNKNOWN; out <= JCS_BG_YCC; out++) {
//            if (!color_conv_supported((J_COLOR_SPACE)out,(J_COLOR_SPACE)in)) {
//                continue;
//            }
            test_random((J_COLOR_SPACE)in, 85, 0xFF, (J_COLOR_SPACE)out);
        }
    }



    for (int i = JSC_TEST_SAMP_FACTOR_111; i <= JSC_TEST_SAMP_FACTOR_H1V3; i++) {
        samp_factor = (JscTestSampFactor)i;
        test_random(JCS_RGB);
    }
    samp_factor = JSC_TEST_SAMP_FACTOR_DEFAULT;


    printf("\nTest two-channel unknown colorspace\n");
    set_global_params(JCS_UNKNOWN);
    img_cinfo.input_components = 2;
    alloc_global_bufs();
    make_random_input(0xFF);
    test_compress_decompress_error(75);
    free_global_bufs();




}

TEST(JSCTest, SingleColor) {

    do_one_shot_comp_ctr += 6;

    unsigned char color[4];
    color[0] = 0;
    test_single_color(JCS_GRAYSCALE, color);
    n_restart_sections = 2;
    test_single_color(JCS_GRAYSCALE, color);
    n_restart_sections = 32;
    test_single_color(JCS_GRAYSCALE, color);

    color[0] = 255;
    test_single_color(JCS_GRAYSCALE, color);
    color[0] = 128;
    test_single_color(JCS_GRAYSCALE, color);

    use_alt_params = true;
//    alt_params.dct_method = JDCT_ISLOW;
//    alt_params.restart_interval = 0;
//    alt_params.restart_in_rows = 0;
//    color[0] = 0;
//    test_single_color(JCS_GRAYSCALE, color);
//    color[0] = 255;
//    test_single_color(JCS_GRAYSCALE, color);
//    color[0] = 128;
//    test_single_color(JCS_GRAYSCALE, color);
//
//    alt_params.dct_method = JDCT_IFAST;
//    color[0] = 0;
//    test_single_color(JCS_GRAYSCALE, color);
//    color[0] = 255;
//    test_single_color(JCS_GRAYSCALE, color);
//    color[0] = 128;
//    test_single_color(JCS_GRAYSCALE, color);

//    alt_params.dct_method = JDCT_FLOAT;
    alt_params.restart_in_rows = 50;
    color[0] = 0;
    test_single_color(JCS_GRAYSCALE, color);
    color[0] = 255;
    test_single_color(JCS_GRAYSCALE, color);
    color[0] = 128;
    test_single_color(JCS_GRAYSCALE, color);
    use_alt_params = false;

    color[0] = 0;
    color[1] = 0;
    color[2] = 0;
    test_single_color(JCS_RGB, color);
    test_single_color(JCS_YCbCr, color);
    color[0] = 255;
    color[1] = 255;
    color[2] = 255;
    test_single_color(JCS_RGB, color);
    test_single_color(JCS_YCbCr, color);
    color[0] = 128;
    color[1] = 128;
    color[2] = 128;
    test_single_color(JCS_RGB, color);
    test_single_color(JCS_YCbCr, color);
    color[0] = 255;
    color[1] = 0;
    color[2] = 0;
    test_single_color(JCS_RGB, color);
    test_single_color(JCS_YCbCr, color);
    color[0] = 0;
    color[1] = 255;
    color[2] = 0;
    test_single_color(JCS_RGB, color);
    test_single_color(JCS_YCbCr, color);
    color[0] = 0;
    color[1] = 0;
    color[2] = 255;
    test_single_color(JCS_RGB, color);
    test_single_color(JCS_YCbCr, color);
    color[0] = 255;
    color[1] = 255;
    color[2] = 0;
    test_single_color(JCS_RGB, color);
    test_single_color(JCS_YCbCr, color);
    color[0] = 0;
    color[1] = 255;
    color[2] = 255;
    test_single_color(JCS_RGB, color);
    test_single_color(JCS_YCbCr, color);
    color[0] = 255;
    color[1] = 0;
    color[2] = 255;
    test_single_color(JCS_RGB, color);
    test_single_color(JCS_YCbCr, color);

    printf("\nforce jpeg color space gray\n");
    force_jpeg_color_space = true;
    forced_color_space = JCS_GRAYSCALE;
    color[0] = 0;
    color[1] = 255;
    color[2] = 255;
    test_single_color(JCS_RGB, color);
    test_single_color(JCS_YCbCr, color);
    force_jpeg_color_space = false;


    color[0] = 100;
    color[1] = 200;
    color[2] = 128;
    color[3] = 042;
    test_single_color(JCS_CMYK, color);
    test_single_color(JCS_YCCK, color);

    printf("\nforce jpeg color space ycck\n");
    force_jpeg_color_space = true;
    forced_color_space = JCS_YCCK;
    color[0] = 100;
    color[1] = 200;
    color[2] = 128;
    color[3] = 042;
    test_single_color(JCS_CMYK, color);
    force_jpeg_color_space = false;

    printf("\nsubtract green\n");
    force_jpeg_color_space = true;
    forced_color_space = JCS_RGB;
//    do_subtract_green = true;
//    color[0] = 100;
//    color[1] = 200;
//    color[2] = 128;
//    test_single_color(JCS_RGB, color);
//    do_subtract_green = false;
    force_jpeg_color_space = false;

    marker_test = JSC_TEST_MARKER_WRITE;
    test_single_color(JCS_RGB, color);
    marker_test = JSC_TEST_MARKER_WRITE_SKIP;
    test_single_color(JCS_RGB, color);
    marker_test = JSC_TEST_MARKER_PIECEMEAL;
    test_single_color(JCS_RGB, color);
    marker_test = JSC_TEST_MARKER_NONE;


    for (int in = JCS_UNKNOWN; in <= JCS_BG_YCC; in++) {
        for (int out = JCS_UNKNOWN; out <= JCS_BG_YCC; out++) {
//            if (!color_conv_supported((J_COLOR_SPACE)out,(J_COLOR_SPACE)in)) {
//                continue;
//            }
            color[0] = rand() % 255;
            color[1] = rand() % 255;
            color[2] = rand() % 255;
            color[3] = rand() % 255;
            test_single_color((J_COLOR_SPACE)in, color, (J_COLOR_SPACE)out);
        }
    }

    for (int i = JSC_TEST_SAMP_FACTOR_111; i <= JSC_TEST_SAMP_FACTOR_H1V3; i++) {
        samp_factor = (JscTestSampFactor)i;
        color[0] = rand() % 255;
        color[1] = rand() % 255;
        color[2] = rand() % 255;
        color[3] = rand() % 255;
        test_single_color(JCS_RGB, color);
    }

    // test random sizes
    for (int j = 0; j< 20; j++) {
        for (int i = JSC_TEST_SAMP_FACTOR_111; i <= JSC_TEST_SAMP_FACTOR_H1V3; i++) {
             samp_factor = (JscTestSampFactor)i;
             color[0] = rand() % 255;
             color[1] = rand() % 255;
             color[2] = rand() % 255;
             color[3] = rand() % 255;
             test_single_color(JCS_RGB, color, JCS_UNKNOWN, 75,
                     40 + (rand() & 0xFF),40 + (rand() & 0xFF));
         }
    }

    samp_factor = JSC_TEST_SAMP_FACTOR_DEFAULT;



}

TEST(JSCTest, ImageFile) {
    do_one_shot_comp_ctr = 10;
    test_image_bmp(JCS_GRAYSCALE, 85);
    n_restart_sections = 2;
    test_image_bmp(JCS_GRAYSCALE, 85);
    n_restart_sections = 32;
    test_image_bmp(JCS_GRAYSCALE, 85);

    test_image_bmp(JCS_GRAYSCALE, 75);
    test_image_bmp(JCS_GRAYSCALE, 50);
    test_image_bmp(JCS_GRAYSCALE, 30);
    test_image_bmp(JCS_GRAYSCALE, 20);
    test_image_bmp(JCS_GRAYSCALE, 10);

    do_one_shot_comp_ctr = 10;
    test_image_bmp(JCS_RGB, 85);
    n_restart_sections = 2;
    test_image_bmp(JCS_RGB, 85);
    n_restart_sections = 32;
    test_image_bmp(JCS_RGB, 85);

    test_image_bmp(JCS_RGB, 75);
    test_image_bmp(JCS_RGB, 50);
    test_image_bmp(JCS_RGB, 30);
    test_image_bmp(JCS_RGB, 20);
    test_image_bmp(JCS_RGB, 10);

    for (int in = JCS_GRAYSCALE; in <= JCS_RGB; in++) {
        for (int out = JCS_GRAYSCALE; out <= JCS_BG_YCC; out++) {
//            if (!color_conv_supported((J_COLOR_SPACE)out,(J_COLOR_SPACE)in)) {
//                continue;
//            }
            test_image_bmp((J_COLOR_SPACE)in, 85, (J_COLOR_SPACE)out);
        }
    }

    for (int i = JSC_TEST_SAMP_FACTOR_111; i <= JSC_TEST_SAMP_FACTOR_H1V2; i++) {
        samp_factor = (JscTestSampFactor)i;
        test_image_bmp(JCS_RGB, 75);
    }
    samp_factor = JSC_TEST_SAMP_FACTOR_DEFAULT;

    printf("\nforce jpeg color space gray\n");
    force_jpeg_color_space = true;
    forced_color_space = JCS_GRAYSCALE;
    test_image_bmp(JCS_RGB, 75);
    force_jpeg_color_space = false;
}

TEST(JSCTest, Corruption) {

    J_COLOR_SPACE in_color_space = JCS_RGB;
    int quality = 75;
    J_COLOR_SPACE out_color_space = JCS_UNKNOWN;
    const char * filename = "../test/frog.bmp";
    int n_pix;
    unsigned int corruption_idx;
    unsigned int corruption_bytes;

    printf("\ncorrupt 1 percent of compressed image.\n");

    load_bmp(in_color_space, out_color_space, filename);
    n_pix = img_cinfo.image_height * img_cinfo.image_width;


    assert_global_buffs_alloced();
    do_compress(quality);

    corruption_idx = comp_bytes/2;
    corruption_bytes = comp_bytes/100;

    printf("\ncorrupt %u of %u bytes at idx %u.\n",
            corruption_bytes, comp_bytes, corruption_idx);

    for (int i = 0; i < corruption_bytes; i++) {
        comp_buffer[i+corruption_idx] = rand()%0xFF;
    }

    write_jpeg_out();

    do_decompress();
    check_error(0xFF);

    if(write_output_files) {
        char filename_out[80];
        sprintf(filename_out, "Testing/Temporary/corrupted_out%02d_cs%d_q%d.bmp",
                bmp_out_ctr++, in_color_space, quality);
        write_output_bmp(filename_out);
    }

    free_global_bufs();

    printf("\ncorrupt 1 percent of compressed image, restart markers.\n");

    load_bmp(in_color_space, out_color_space, filename);
    n_pix = img_cinfo.image_height * img_cinfo.image_width;

    do_one_shot_comp_ctr = 1;
    n_restart_sections = 8;

    assert_global_buffs_alloced();
    do_compress(quality);

    corruption_idx = comp_bytes/2;
    corruption_bytes = comp_bytes/100;

    printf("\ncorrupt %u of %u bytes at idx %u.\n",
            corruption_bytes, comp_bytes, corruption_idx);

    for (int i = 0; i < corruption_bytes; i++) {
        comp_buffer[i+corruption_idx] = rand()%0xFF;
    }

    write_jpeg_out();

    do_decompress();
    check_error(0xFF);

    if(write_output_files) {
        char filename_out[80];
        sprintf(filename_out, "Testing/Temporary/corrupted_out%02d_cs%d_q%d.bmp",
                bmp_out_ctr++, in_color_space, quality);
        write_output_bmp(filename_out);
    }

    free_global_bufs();

    use_alt_params = false;

    printf("\ncorrupt 1 percent of compressed image with random, restart markers.\n");

    load_bmp(in_color_space, out_color_space, filename);
    n_pix = img_cinfo.image_height * img_cinfo.image_width;
    use_alt_params = true;
//    alt_params.dct_method = JDCT_FLOAT;
    alt_params.restart_interval = 0;
    alt_params.restart_in_rows = 5;
    printf("restart rows = %u\n",
            alt_params.restart_in_rows);
    assert_global_buffs_alloced();
    do_compress(quality);
    corruption_idx = comp_bytes/2;
    corruption_bytes = comp_bytes/100;

    printf("corrupt %u of %u bytes at idx %u with random.\n",
            corruption_bytes, comp_bytes, corruption_idx);
    for (int i = 0; i < corruption_bytes; i++) {
        comp_buffer[i+corruption_idx] = rand()%0xFF;
    }
    write_jpeg_out();

    do_decompress();
    check_error(0xFF);
    if(write_output_files) {
        char filename_out[80];
        sprintf(filename_out, "Testing/Temporary/corrupted_out%02d_cs%d_q%d.bmp",
                bmp_out_ctr++, in_color_space, quality);
        write_output_bmp(filename_out);
    }
    free_global_bufs();
    use_alt_params = false;

    printf("\ncorrupt 1 percent of compressed image with 0s, restart markers.\n");

    load_bmp(in_color_space, out_color_space, filename);
    n_pix = img_cinfo.image_height * img_cinfo.image_width;
    use_alt_params = true;
//    alt_params.dct_method = JDCT_FLOAT;
    alt_params.restart_interval = 0;
    alt_params.restart_in_rows = 5;
    printf("restart rows = %u\n",
            alt_params.restart_in_rows);
    assert_global_buffs_alloced();
    do_compress(quality);
    corruption_idx = comp_bytes/2;
    corruption_bytes = comp_bytes/100;

    printf("corrupt %u of %u bytes at idx %u with 0s.\n",
            corruption_bytes, comp_bytes, corruption_idx);
    for (int i = 0; i < corruption_bytes; i++) {
        comp_buffer[i+corruption_idx] = 0;
    }
    write_jpeg_out();

    do_decompress();
    check_error(0xFF);
    if(write_output_files) {
        char filename_out[80];
        sprintf(filename_out, "Testing/Temporary/corrupted_out%02d_cs%d_q%d.bmp",
                bmp_out_ctr++, in_color_space, quality);
        write_output_bmp(filename_out);
    }
    free_global_bufs();
    use_alt_params = false;

    printf("\ncorrupt 1 percent of compressed image with 0xFFs, restart markers.\n");

    load_bmp(in_color_space, out_color_space, filename);
    n_pix = img_cinfo.image_height * img_cinfo.image_width;
    use_alt_params = true;
//    alt_params.dct_method = JDCT_FLOAT;
    alt_params.restart_interval = 0;
    alt_params.restart_in_rows = 5;
    printf("restart rows = %u\n",
            alt_params.restart_in_rows);
    assert_global_buffs_alloced();
    do_compress(quality);

    corruption_idx = comp_bytes/2;
    corruption_bytes = comp_bytes/100;

    printf("corrupt %u of %u bytes at idx %u with FFs.\n",
            corruption_bytes, comp_bytes, corruption_idx);
    for (int i = 0; i < corruption_bytes; i++) {
        comp_buffer[i+corruption_idx] = 0xFF;
    }
    write_jpeg_out();

    do_decompress();
    check_error(0xFF);
    if(write_output_files) {
        char filename_out[80];
        sprintf(filename_out, "Testing/Temporary/corrupted_out%02d_cs%d_q%d.bmp",
                bmp_out_ctr++, in_color_space, quality);
        write_output_bmp(filename_out);
    }
    free_global_bufs();
    use_alt_params = false;

    printf("\ncorrupt 1 percent of compressed image with 0x42s, restart markers.\n");

    load_bmp(in_color_space, out_color_space, filename);
    n_pix = img_cinfo.image_height * img_cinfo.image_width;
    use_alt_params = true;
//    alt_params.dct_method = JDCT_FLOAT;
    alt_params.restart_interval = 0;
    alt_params.restart_in_rows = 5;
    printf("restart rows = %u\n",
            alt_params.restart_in_rows);
    assert_global_buffs_alloced();
    do_compress(quality);
    corruption_idx = comp_bytes/2;
    corruption_bytes = comp_bytes/10;

    printf("corrupt %u of %u bytes at idx %u with 0x42s.\n",
            corruption_bytes, comp_bytes, corruption_idx);
    for (int i = 0; i < corruption_bytes; i++) {
        comp_buffer[i+corruption_idx] = 0x42;
    }
    write_jpeg_out();

    do_decompress();
    check_error(0xFF);
    if(write_output_files) {
        char filename_out[80];
        sprintf(filename_out, "Testing/Temporary/corrupted_out%02d_cs%d_q%d.bmp",
                bmp_out_ctr++, in_color_space, quality);
        write_output_bmp(filename_out);
    }
    free_global_bufs();
    use_alt_params = false;
}

//TEST(JSCTest, JSCErrors) {
//
//    J_COLOR_SPACE in_color_space = JCS_RGB;
//    int quality = 75;
//    J_COLOR_SPACE out_color_space = JCS_UNKNOWN;
//    const char * filename = "../test/frog.bmp";
//    int n_pix;
//    unsigned int corruption_idx;
//    unsigned int corruption_bytes;
//
//    printf("\nBad decompress buffer size.\n");
//
//    load_bmp(in_color_space, out_color_space, filename);
//    n_pix = img_cinfo.image_height * img_cinfo.image_width;
//
//    assert_global_buffs_alloced();
//    do_compress(quality);
//
//    comp_bytes = comp_bytes * 0.9;
//
//    do_decompress();
//    check_error(0xFF);
//
//    if(write_output_files) {
//        char filename_out[80];
//        sprintf(filename_out, "img_out%02d_cs%d_q%d.bmp",
//                img_out_ctr++, in_color_space, quality);
//        write_output_bmp(filename_out);
//    }
//
//    free_global_bufs();
//}

TEST(JSCTest, SmallCompOutput) {

    printf("Small compression output\n");
    set_global_params(JCS_RGB);
    alloc_global_bufs();
    // make output buffer size relatively small
    comp_buffer_bytes = input_img_bytes * 0.1;
    make_random_input(0xFF);

    struct jpeg_compress_struct cinfo;
    struct jpeg_static_memory statmem;
    JINT quality = 95;

    /* Step 1: allocate and initialize JPEG compression object */
    printf("Provided %u working_mem_bytes\n", working_mem_bytes);
    cinfo.statmem = jpeg_give_static_mem(&statmem, working_mem_buffer, working_mem_bytes);
    jpeg_create_compress(&cinfo);
    cinfo.trace_level = trace_level;

    /* Step 2: specify data destination */
    comp_bytes = comp_buffer_bytes;
    jpeg_mem_dest(&cinfo, &comp_buffer, &comp_bytes);

    /* Step 3: set parameters for compression */
    cinfo.image_width = img_cinfo.image_width;  /* image width and height, in pixels */
    cinfo.image_height = img_cinfo.image_height;
    cinfo.input_components = img_cinfo.input_components;   /* # of color components per pixel */
    cinfo.in_color_space = img_cinfo.in_color_space;       /* colorspace of input image */

    jpeg_set_defaults(&cinfo);

    jpeg_set_quality(&cinfo, quality, TRUE);

    /* Step 4: Start compressor */
    jpeg_start_compress(&cinfo, TRUE);

    /* Step 5: while (scan lines remain to be written) */
    /*           jpeg_write_scanlines(...); */

    /* JSAMPLEs per row in image_buffer */
    int row_stride = cinfo.image_width * cinfo.input_components;
    JSAMPROW row_pointer[1];      /* pointer to JSAMPLE row[s] */
    bool bad_write = FALSE;
    while (cinfo.next_scanline < cinfo.image_height) {
      /* jpeg_write_scanlines expects an array of pointers to scanlines.
       * Here the array is only one element long, but you could pass
       * more than one scanline at a time if that's more convenient.
       */
      row_pointer[0] = & img_buffer_input[cinfo.next_scanline * row_stride];
      JDIMENSION rows_written = jpeg_write_scanlines(&cinfo, row_pointer, 1);

      if (rows_written < 1) {
          bad_write = TRUE;
          break;
      }
    }

    ASSERT_TRUE(bad_write);

    free_global_bufs();

}

TEST(JSCTest, SmallDecompInput) {


    printf("Small decompression input\n");

    set_global_params(JCS_RGB);
    alloc_global_bufs();
    make_random_input(0xFF);
    JINT quality = 95;
    do_compress(quality);

    struct jpeg_decompress_struct dinfo;
    struct jpeg_static_memory statmem;
    JSAMPROW row_pointer[1]; /* pointer to JSAMPLE row[s] */
    int row_stride; /* physical row width in output buffer */
    JINT ret;
    boolean bret;
    bool read_err = FALSE;
    int loop_limit = dinfo.output_height + 5;
    int loops = 0;

    dinfo.statmem = jpeg_give_static_mem(&statmem, working_mem_buffer, working_mem_bytes);
    jpeg_create_decompress(&dinfo);
    dinfo.trace_level = trace_level;
    // give only 1 byte!
    jpeg_mem_src(&dinfo, comp_buffer, 1);
    ret = jpeg_read_header(&dinfo, TRUE);
    ASSERT_EQ(ret, JPEG_SUSPENDED);


    dinfo.statmem = jpeg_give_static_mem(&statmem, working_mem_buffer, working_mem_bytes);
    jpeg_create_decompress(&dinfo);
    // give only half the data!
    jpeg_mem_src(&dinfo, comp_buffer, comp_bytes/2);
    ret = jpeg_read_header(&dinfo, TRUE);
    ASSERT_EQ(ret, JPEG_HEADER_OK);

    bret = jpeg_start_decompress(&dinfo);
    row_stride = dinfo.output_width * dinfo.output_components;
    read_err = FALSE;
    loop_limit = dinfo.output_height + 5;
    loops = 0;
    while (dinfo.output_scanline < dinfo.output_height) {
        row_pointer[0] = &img_buffer_output[dinfo.output_scanline * row_stride];
        JDIMENSION lines_read = jpeg_read_scanlines(&dinfo, row_pointer, 1);
        if(lines_read < 1) {
            read_err = TRUE;
            break;
        }
        loops++;
        ASSERT_TRUE(loops < loop_limit);
    }
    ASSERT_EQ(read_err, TRUE);


    free_global_bufs();


}

void test_image_corrupt_byte(int bi, U8 garbage_byte,
        int bi2 = -1, U8 garbage_byte2 = 0) {

    if(!have_compressed_image) {
        load_bmp(JCS_RGB, JCS_RGB, "../test/frog.bmp");
        do_compress(95);
    }

    printf("\n\ncorrupt byte %d from %#02x to %#02x\n",
            bi, comp_buffer[bi], garbage_byte);


    ASSERT_LT(bi, comp_bytes);
    if(garbage_byte == comp_buffer[bi] ) {
        printf("same byte!\n");
        return;
    }
    U8 saved_byte = comp_buffer[bi];
    comp_buffer[bi] = garbage_byte;

    U8 saved_byte2;
    if(bi2 >= 0) {
        printf("\n\ncorrupt byte %d from %#02x to %#02x\n",
                bi2, comp_buffer[bi2], garbage_byte2);
        ASSERT_LT(bi2, comp_bytes);
        ASSERT_NE(bi, bi2);
        saved_byte2 = comp_buffer[bi2];
        comp_buffer[bi2] = garbage_byte2;
    }


    struct jpeg_decompress_struct dinfo;
    struct jpeg_static_memory statmem;
    JSAMPROW row_pointer[1]; /* pointer to JSAMPLE row[s] */
    int row_stride; /* physical row width in output buffer */
    JINT ret;
    boolean bret;
    bool read_err;
    int loop_limit;
    int loops;
    bool bad_size = false;
    boolean started;
    boolean finished;
    boolean input_complete;

    dinfo.statmem = jpeg_give_static_mem(&statmem, working_mem_buffer, working_mem_bytes);
    jpeg_create_decompress(&dinfo);
    jpeg_mem_src(&dinfo, comp_buffer, comp_bytes);
    dinfo.trace_level = 3;

    ret = jpeg_read_header(&dinfo, TRUE);
    if(ret != JPEG_REACHED_SOS) {
        printf("jpeg_read_header() failed.\n");
        goto cleanup;
    }


    started = jpeg_start_decompress(&dinfo);
    if(!started) {
        printf("jpeg_start_decompress() failed.\n");
        goto cleanup;
    }

    if(dinfo.output_width != 432) {
        // FIXME a ginormous width blows memory allocation, need a graceful fix
        printf("bad width %d!\n", dinfo.output_width);
//                continue;
        bad_size = true;
    }
    if(dinfo.output_height != 400) {
        // FIXME a ginormous width blows memory allocation, need a graceful fix
        printf("bad height %d!\n", dinfo.output_height);
        //continue;
        bad_size = true;
    }

    if (bad_size) {
        JSIZE new_output_img_bytes = dinfo.output_width * dinfo.output_height * sizeof(JSAMPLE)
                    * dinfo.output_components;
        printf("desired new size %d\n",output_img_bytes);
        if(new_output_img_bytes > output_img_bytes && new_output_img_bytes < 1e6) {
            output_img_bytes = new_output_img_bytes;
            free(img_buffer_output);
            img_buffer_output = (JSAMPLE *) malloc(output_img_bytes);
        } else {
            printf("too large a buffer, continue\n");
            goto cleanup;
        }
    }

    row_stride = dinfo.output_width * dinfo.output_components;
    read_err = FALSE;
    loop_limit = dinfo.output_height + 5;
    loops = 0;
    while (dinfo.output_scanline < dinfo.output_height) {
        if(loops>= 398) printf("loops %d, output scanline %d\n",
                loops, dinfo.output_scanline);
        row_pointer[0] = &img_buffer_output[dinfo.output_scanline * row_stride];
        ASSERT_TRUE(row_pointer != NULL);
        //ASSERT_LT((long)(row_pointer-img_buffer_output[0]), output_img_bytes);
        JDIMENSION lines_read = jpeg_read_scanlines(&dinfo, row_pointer, 1);
        if(lines_read < 1) {
            read_err = TRUE;
            break;
        }
        loops++;
        ASSERT_TRUE(loops < loop_limit);
    }
    if(read_err) {
        printf("jpeg_read_scanlines() failed.\n");
        goto cleanup;
    }

    finished = jpeg_finish_decompress(&dinfo);
    if(!finished) {
        printf("jpeg_finish_decompress() failed.\n");
        goto cleanup;
    }
    input_complete = jpeg_input_complete(&dinfo);
    if(!input_complete) {
        printf("jpeg input not complete.\n");
        goto cleanup;
    }


    printf("jpeg decompressed.\n");;
    jpeg_destroy_decompress(&dinfo);


    cleanup:

    comp_buffer[bi] = saved_byte;
    if(bi2 >= 0) {
        comp_buffer[bi2] = saved_byte2;
    }

}

void short_garbage_test(void)
{
    // run the random gamut
    static int n_std_bytes = 9;
    U8 garbage_bytes[n_std_bytes] = {0x00, 0xFF,
            JPEG_RST0, JPEG_EOI, JPEG_APP0, JPEG_COM,
            0xA5, 0x5A, 0x42};

    int test_rounds = n_std_bytes + 10;

    int num_tests = 0;
    for(int gidx = 0; gidx < test_rounds; gidx++) {
        for(int bidx = comp_bytes-1; bidx >= 0; bidx--) {
            // if bi is more than 100 bytes from ending, ffwd to beginning
            if(bidx == comp_bytes-100) {
                bidx = MIN(comp_bytes-100, 1000);
            }

            // corrupt byte bi in compressed buffer, then try to compress
            // should be handled gracefully
            U8 garbage_byte;
            if(gidx < n_std_bytes) {
                garbage_byte = garbage_bytes[gidx];
            } else {
                garbage_byte = rand() & 0xFF;
            }
            test_image_corrupt_byte(bidx, garbage_byte);
            num_tests++;
        }
    }

    printf("ran %d garbage tests\n", num_tests);

}

// test setting EACH byte to EVERY possible byte
void long_garbage_test(void)
{
    int num_tests = 0;
     for(int gidx = 0; gidx <= 255 ; gidx++) {
         for(int bidx = comp_bytes-1; bidx >= 0; bidx--) {
             // if bi is more than 100 bytes from ending, ffwd to beginning
             if(bidx == comp_bytes-100) {
                 bidx = MIN(comp_bytes-100, 1000);
             }

             U8 garbage_byte = (U8) gidx;
             test_image_corrupt_byte(bidx, garbage_byte);
             num_tests++;
         }
     }

     printf("ran %d garbage tests\n", num_tests);

}


TEST(JSCTest, GarbageDecompInput)
{
    printf("Garbage decompression input\n");

    load_bmp(JCS_RGB, JCS_RGB, "../test/frog.bmp");
    JINT quality = 95;
    do_compress(quality);

    // test significant examples

    printf("corrupt component to different valid value\n");
    test_image_corrupt_byte(169, 0x13);
    test_image_corrupt_byte(169, 0x23);
    test_image_corrupt_byte(169, 0x14);
    test_image_corrupt_byte(169, 0x41);
    test_image_corrupt_byte(175, 0x13);
    test_image_corrupt_byte(175, 0x31);
    test_image_corrupt_byte(175, 0x14);
    test_image_corrupt_byte(175, 0x41);
    test_image_corrupt_byte(175, 0x23);

    printf("corrupt component with too many blocks\n");
    test_image_corrupt_byte(175, 0x24);
    test_image_corrupt_byte(175, 0x42);

    printf("corrupt with early EOI\n");
    test_image_corrupt_byte(90, 0xd9);

    printf("bogus marker length\n");
    test_image_corrupt_byte(613, 0);

    printf("corrupt but same component id in SOF and SOS\n");
    test_image_corrupt_byte(174, 0xd3, 618, 0xd3);

    printf("corrupt duplicate component id in SOF and SOS\n");
    test_image_corrupt_byte(174, 0x1, 618, 0x1);

    printf("corrupt marker means unallocated quantization table\n");
    test_image_corrupt_byte(90, 0xe0);



    // run the random gamut
    static int n_std_bytes = 9;
    U8 garbage_bytes[n_std_bytes] = {0x00, 0xFF,
            JPEG_RST0, JPEG_EOI, JPEG_APP0, JPEG_COM,
            0xA5, 0x5A, 0x42};

    int test_rounds;
    if (getenv("JSC_ENABLE_LONG_GARBAGE_TESTS")) {
        long_garbage_test();
//        test_rounds = 300;
        printf("Did long tests\n");
    } else {
        short_garbage_test();

//        test_rounds = n_std_bytes + 3;
        printf("Did short tests\n");
    }

//    int num_tests = 0;
//    for(int gi = 0; gi < test_rounds; gi++) {
//        for(int bi = comp_bytes-1; bi >= 0; bi--) {
//            // if bi is more than 100 bytes from ending, ffwd to beginning
//            if(bi == comp_bytes-100) {
//                bi = MIN(comp_bytes-100, 1000);
//            }
//
//            // corrupt byte bi in compressed buffer, then try to compress
//            // should be handled gracefully
//            U8 garbage_byte;
//            if(gi < n_std_bytes) {
//                garbage_byte = garbage_bytes[gi];
//            } else {
//                garbage_byte = rand() & 0xFF;
//            }
//            test_image_corrupt_byte(bi, garbage_byte);
//            num_tests++;
//        }
//    }

    free_global_bufs();

//    printf("ran %d garbage tests\n", num_tests);
}




TEST(JSCTest, Deaths) {


    if (getenv("JSC_DISABLE_DEATH_TESTS")) {
        printf("disabling death tests... they don't play well with valgrind.\n");
        return;
    } else {
        printf("death tests enabled.\n");
    }

    J_COLOR_SPACE in_color_space = JCS_RGB;
    int quality = 85;
    J_COLOR_SPACE out_color_space = JCS_UNKNOWN;
    const char *filename = "../test/frog.bmp";

    load_bmp(in_color_space, out_color_space, filename);
    int n_pix = img_cinfo.image_height * img_cinfo.image_width;

    struct jpeg_compress_struct cinfo;
    struct jpeg_compress_struct cinfo2;
    struct jpeg_static_memory statmem;
    jpeg_progress_mgr jprog;
    JSAMPROW row_pointer[1];      /* pointer to JSAMPLE row[s] */
    int row_stride;               /* physical row width in image buffer */

    /* Step 1: allocate and initialize JPEG compression object */
    printf("jpeg_give_static_mem()\n");

    ASSERT_DEATH(jpeg_give_static_mem(NULL, working_mem_buffer, working_mem_bytes),
            "Assertion.+statmem.+void");
    ASSERT_DEATH(jpeg_give_static_mem(&statmem, NULL, working_mem_bytes),
            "Assertion.+buffer.+void");
    ASSERT_DEATH(jpeg_give_static_mem(&statmem, working_mem_buffer, 0),
            "Assertion.+buffer_size_bytes > 0");

    cinfo.statmem = jpeg_give_static_mem(&statmem,
            working_mem_buffer, working_mem_bytes);


    printf("jpeg_CreateCompress()\n");

    // FIXME make asserts
    ASSERT_DEATH(
            jpeg_CreateCompress(&cinfo, JPEG_LIB_VERSION - 1,
                    (JSIZE) sizeof(struct jpeg_compress_struct)),
            "Assertion.+version");
    ASSERT_DEATH(
            jpeg_CreateCompress(&cinfo, JPEG_LIB_VERSION,
                    (JSIZE) sizeof(struct jpeg_compress_struct) - 1),
            "Assertion.+structsize");

    jpeg_create_compress(&cinfo);

    jprog.progress_monitor = jsc_test_progmon;
    cinfo.progress = &jprog;

    /* Step 2: specify data destination (eg, a file) */
    comp_bytes = comp_buffer_bytes; // FIXME

    printf("jpeg_mem_dest()\n");

    ASSERT_DEATH(
            jpeg_mem_dest(NULL, &comp_buffer, &comp_bytes),
            "Assertion.+cinfo.+void");
    ASSERT_DEATH(
            jpeg_mem_dest(&cinfo, NULL, &comp_bytes),
            "Assertion.+outbuffer.+void");
    ASSERT_DEATH(
            jpeg_mem_dest(&cinfo, &comp_buffer, NULL),
            "Assertion.+outsize.+void");

    jpeg_mem_dest(&cinfo, &comp_buffer, &comp_bytes);

    /* Step 3: set parameters for compression */
    cinfo.image_width = img_cinfo.image_width;  /* image width and height, in pixels */
    cinfo.image_height = img_cinfo.image_height;
    cinfo.input_components = img_cinfo.input_components;   /* # of color components per pixel */
    cinfo.in_color_space = img_cinfo.in_color_space;       /* colorspace of input image */

    comp_stats.n_input_pixels = img_cinfo.image_width * img_cinfo.image_height;
    comp_stats.n_input_values = comp_stats.n_input_pixels * img_cinfo.input_components;
    comp_stats.bits_per_pixel = sizeof(JSAMPLE)*8*img_cinfo.input_components;
    comp_stats.n_input_bits = comp_stats.n_input_pixels * comp_stats.bits_per_pixel;

    printf("jpeg_set_defaults()\n");
    cinfo2 = cinfo;
    cinfo2.global_state = 14589;
    ASSERT_DEATH(
            jpeg_set_defaults(&cinfo2),
            "Assertion.+global_state");

    cinfo2 = cinfo;
    cinfo2.in_color_space = (J_COLOR_SPACE)14589;
    ASSERT_DEATH(
            jpeg_set_defaults(&cinfo2),
            "Assertion");

    jpeg_set_defaults(&cinfo);

    printf("jpeg_set_quality()\n");
    cinfo2 = cinfo;
    cinfo2.global_state = 14589;
    ASSERT_DEATH(
            jpeg_set_quality(&cinfo2, quality, TRUE),
            "Assertion.+global_state");

    // PROPER
    jpeg_set_quality(&cinfo, quality, TRUE /* limit to baseline-JPEG values */);

    printf("jpeg_set_colorspace()\n");
    cinfo2 = cinfo;
    cinfo2.global_state = 14589;
    ASSERT_DEATH(
            jpeg_set_colorspace(&cinfo2, (J_COLOR_SPACE)9567),
            "Assertion.+global_state");

    cinfo2 = cinfo;
    ASSERT_DEATH(
            jpeg_set_colorspace(&cinfo2, (J_COLOR_SPACE)9567),
            "Assertion");
    cinfo2 = cinfo;
    cinfo2.input_components = MAX_COMPONENTS + 1;
    ASSERT_DEATH(
            jpeg_set_colorspace(&cinfo2, JCS_UNKNOWN),
            "Assertion.+num_components");

    // too early
    const char* text = "Compressed with JSC.";
    const unsigned char* utext =
            reinterpret_cast<const unsigned char *>( text );
    ASSERT_DEATH(
            jpeg_write_marker(&cinfo, JPEG_COM, utext, strlen(text)),
            "Assertion.+global_state");
    ASSERT_DEATH(
            jpeg_write_m_header(&cinfo, JPEG_COM, strlen(text)),
            "Assertion.+global_state");
    ASSERT_DEATH(
            jpeg_finish_compress(&cinfo),
            "Assertion");
    ASSERT_DEATH(
            jpeg_write_scanlines(&cinfo, row_pointer, 1),
            "Assertion.+global_state");



    /* Step 4: Start compressor */
    printf("jpeg_start_compress()\n");

    ASSERT_DEATH(
            jpeg_start_compress(NULL, TRUE),
            "Assert.+cinfo.+void");

    cinfo2 = cinfo;
    cinfo2.global_state = 14589;
    ASSERT_DEATH(
            jpeg_start_compress(&cinfo2, TRUE),
            "Assertion.+global_state");

    // PROPER
    jpeg_start_compress(&cinfo, TRUE);

    // too late / already done
    ASSERT_DEATH(
            jpeg_start_compress(&cinfo, TRUE),
            "Assertion.+global_state");
    ASSERT_DEATH(
            jpeg_set_defaults(&cinfo),
            "Assertion.+global_state");
    ASSERT_DEATH(
            jpeg_set_quality(&cinfo, quality, TRUE),
            "Assertion.+global_state");


    /* Step 5: while (scan lines remain to be written) */
    /*           jpeg_write_scanlines(...); */

    row_stride = cinfo.image_width * cinfo.input_components;

    // 1 line
    row_pointer[0] = & img_buffer_input[cinfo.next_scanline * row_stride];
    (void) jpeg_write_scanlines(&cinfo, row_pointer, 1);

    cinfo2 = cinfo;
    ASSERT_DEATH(
            jpeg_finish_compress(&cinfo2),
            "Assertion.+next_scanline.+image_height");

    // rest of lines
    while (cinfo.next_scanline < cinfo.image_height) {
      row_pointer[0] = & img_buffer_input[cinfo.next_scanline * row_stride];
      (void) jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }

    // too many lines warning
    printf("cinfo.next_scanline=%d, cinfo.image_height=%d\n",
            cinfo.next_scanline, cinfo.image_height);
    cinfo2 = cinfo;
    jpeg_write_scanlines(&cinfo2, row_pointer, 1);


    /* Step 6: Finish compression */
    jpeg_finish_compress(&cinfo);

    /* Step 7: release JPEG compression object */
    jpeg_destroy_compress(&cinfo);



    // DECOMPRESS
    int ret;

    assert_global_buffs_alloced();
    struct jpeg_decompress_struct dinfo;
    struct jpeg_decompress_struct dinfo2;

    /* Step 1: allocate and initialize JPEG decompression object */
    dinfo.statmem = jpeg_give_static_mem(&statmem,
            working_mem_buffer, working_mem_bytes);

    printf("jpeg_CreateDecompress()\n");

    ASSERT_DEATH(
            jpeg_CreateDecompress(&dinfo, JPEG_LIB_VERSION - 1,
                    (JSIZE) sizeof(struct jpeg_compress_struct)),
            "Assertion.+version");
    ASSERT_DEATH(
            jpeg_CreateDecompress(&dinfo, JPEG_LIB_VERSION,
                    (JSIZE) sizeof(struct jpeg_decompress_struct) - 1),
                    "Assertion.+structsize");

    jpeg_create_decompress(&dinfo);
    dinfo.trace_level = trace_level;

    // too early
    ASSERT_DEATH(
            jpeg_finish_decompress(&dinfo),
            "Assertion.+state");
    ASSERT_DEATH(
            jpeg_read_scanlines(&dinfo, row_pointer, 1),
            "Assertion.+state");


    jprog.progress_monitor = jsc_test_progmon;
    dinfo.progress = &jprog;

    jpeg_set_marker_processor(&dinfo, JPEG_COM, read_com);

    /* Step 2: specify data source */
    jpeg_mem_src(&dinfo, comp_buffer, comp_bytes);
    int input_complete = jpeg_input_complete(&dinfo);

    /* Step 3: read file parameters with jpeg_read_header() */
    ret = jpeg_read_header(&dinfo, TRUE);
    boolean multiple_scans = jpeg_has_multiple_scans(&dinfo);

    /* Step 4: set parameters for decompression */
    img_dinfo.out_color_space = dinfo.out_color_space;


    /* Step 5: Start decompressor */
    boolean bret = jpeg_start_decompress(&dinfo);

    // too late / already done
    ASSERT_DEATH(
            jpeg_start_decompress(&dinfo),
            "Assertion.+state");
    ASSERT_DEATH(
            jpeg_read_header(&dinfo, TRUE),
            "Assertion.+state");



    row_stride = dinfo.output_width * dinfo.output_components;

    /* Step 6: while (scan lines remain to be read) */
    /*           jpeg_read_scanlines(...); */

    // 1 line
    row_pointer[0] = &img_buffer_output[dinfo.output_scanline * row_stride];
    (void) jpeg_read_scanlines(&dinfo, row_pointer, 1);

    // too few
    dinfo2 = dinfo;
    ASSERT_DEATH(
            jpeg_finish_decompress(&dinfo),
            "Assertion.+scanline");

    // remainder
    while (dinfo.output_scanline < dinfo.output_height) {
        row_pointer[0] = &img_buffer_output[dinfo.output_scanline * row_stride];
        (void) jpeg_read_scanlines(&dinfo, row_pointer, 1);
    }

    // too many
    (void) jpeg_read_scanlines(&dinfo, row_pointer, 1);


    /* Step 7: Finish decompression */
    (void) jpeg_finish_decompress(&dinfo);
    input_complete = jpeg_input_complete(&dinfo);

    /* Step 8: Release JPEG decompression object */
    jpeg_destroy_decompress(&dinfo);

    free_global_bufs();


    // test too little working memory

    load_bmp(in_color_space, out_color_space, filename);
    n_pix = img_cinfo.image_height * img_cinfo.image_width;

    cinfo.statmem = jpeg_give_static_mem(&statmem,
            working_mem_buffer, 1);
    ASSERT_DEATH(jpeg_create_compress(&cinfo),
            "Assertion.+mem");

    cinfo.statmem = jpeg_give_static_mem(&statmem,
            working_mem_buffer, working_mem_bytes/8);
    jpeg_create_compress(&cinfo);
    cinfo.trace_level = trace_level;
    comp_bytes = comp_buffer_bytes;
    jpeg_mem_dest(&cinfo, &comp_buffer, &comp_bytes);
    cinfo.image_width = img_cinfo.image_width;  /* image width and height, in pixels */
    cinfo.image_height = img_cinfo.image_height;
    cinfo.input_components = img_cinfo.input_components;   /* # of color components per pixel */
    cinfo.in_color_space = img_cinfo.in_color_space;       /* colorspace of input image */
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);
    ASSERT_DEATH(
            jpeg_start_compress(&cinfo, TRUE),
            "Assert.+void");

    free_global_bufs();



}

#if JPEG_ENABLE_PERFORMANCE_TESTS

TEST(JSCTest, Performance) {

    write_output_files = false;

#define NUM_TEST_IMAGES 24
#define NUM_TEST_COLOR_SPACES 2
#define NUM_TEST_QUALITIES 5

    J_COLOR_SPACE test_color_spaces[NUM_TEST_COLOR_SPACES] = {JCS_GRAYSCALE,JCS_RGB};
    int test_qualities[NUM_TEST_QUALITIES] = {95, 85, 50, 15, 5};
    JpegCompStats test_stats[NUM_TEST_IMAGES][NUM_TEST_COLOR_SPACES][NUM_TEST_QUALITIES];

    double test_start_time_wall = get_wall_time();
    double timeout = 1200;
    bool timed_out = false;
    int num_completed_imgs = 0;


    for (int i_img = 0; i_img < NUM_TEST_IMAGES && !timed_out; i_img++) {
        for (int i_cs = 0; i_cs < NUM_TEST_COLOR_SPACES && !timed_out; i_cs++) {
            for (int i_q = 0; i_q < NUM_TEST_QUALITIES && !timed_out; i_q++) {
                int adjusted_img_num =  i_img + 1; // 1-indexed
                char image_name[100];
                memset(image_name, 0, 100);
                sprintf(image_name, "corpus/kodak/bmp/IMG%04d.bmp",
                        adjusted_img_num);
                write_output_files = (adjusted_img_num == 13);
                test_image_bmp(test_color_spaces[i_cs],
                        test_qualities[i_q], JCS_UNKNOWN, image_name);
                test_stats[i_img][i_cs][i_q] = comp_stats;
                if (get_wall_time() - test_start_time_wall > timeout) {
                    timed_out = true;
                    printf("timed out!\n");
                }
            }
        }
        // if not timed out, the image has been tested
        if(!timed_out) {
            num_completed_imgs = i_img + 1;
        }
    }

    // write results to csv
     FILE * csv_ptr = fopen("performance_jpeg.csv", "w");
     ASSERT_TRUE(csv_ptr != NULL);

     fprintf(csv_ptr, "JPEG compression results\n");
     fprintf(csv_ptr, "Timing results and are averaged over %d test images.\n",
                     /*trust_cpu_time ? "CPU" : "wall",*/ num_completed_imgs);

     fprintf(csv_ptr,
             "color_space, quality, "
                     "compression ratio, "
                     "space saving ratio, "
                     "rms error, "
                     "max error, "
                     "compression duration (s), "
                     "decompression duration (s), "
                     "compression rate (Mb/s), "
                     "decompression rate (Mb/s), "
                     "bits saved rate (Mb/s)\n");


     for (int i_cs = 0; i_cs < NUM_TEST_COLOR_SPACES && !timed_out; i_cs++) {
         for (int i_q = 0; i_q < NUM_TEST_QUALITIES && !timed_out; i_q++) {
             double avg_compression_ratio = 0;
             double avg_rms_err = 0;
             double avg_max_err = 0;
             double avg_compression_time = 0;
             double avg_compression_rate = 0;
             double avg_decompression_time = 0;
             double avg_decompression_rate = 0;
             double avg_bit_save_rate = 0;

            for (int i_img = 0; i_img < NUM_TEST_IMAGES && !timed_out; i_img++) {
                avg_compression_ratio +=
                        test_stats[i_img][i_cs][i_q].compression_ratio;
                avg_rms_err += test_stats[i_img][i_cs][i_q].rmserr;
                avg_max_err += test_stats[i_img][i_cs][i_q].max_err;
                double compression_time =
                        trust_cpu_time ?
                                test_stats[i_img][i_cs][i_q].compression_dur_cpu_s :
                                test_stats[i_img][i_cs][i_q].compression_dur_wall_s;
                avg_compression_time += compression_time;
                avg_compression_rate +=
                        test_stats[i_img][i_cs][i_q].n_input_bits
                        / compression_time;
                double decompression_time =
                        trust_cpu_time ?
                                test_stats[i_img][i_cs][i_q].decompression_dur_cpu_s :
                                test_stats[i_img][i_cs][i_q].decompression_dur_wall_s;
                avg_decompression_time += decompression_time;
                avg_decompression_rate +=
                        test_stats[i_img][i_cs][i_q].n_input_bits
                        / decompression_time;

                avg_bit_save_rate +=
                        (test_stats[i_img][i_cs][i_q].n_input_bits
                         - test_stats[i_img][i_cs][i_q].n_compressed_bits)
                        / (compression_time + decompression_time);

            }
            avg_compression_ratio /= num_completed_imgs;
            avg_rms_err /= num_completed_imgs;
            avg_max_err /= num_completed_imgs;
            avg_compression_time /= num_completed_imgs;
            avg_compression_rate /= num_completed_imgs;
            avg_decompression_time /= num_completed_imgs;
            avg_decompression_rate /= num_completed_imgs;
            avg_bit_save_rate /= num_completed_imgs;

            fprintf(csv_ptr, "%s, %d, %f, %f, %f, %f, %f, %f, %f, %f, %f\n",
                    (test_color_spaces[i_cs] == JCS_GRAYSCALE) ? "grayscale" :
                    (test_color_spaces[i_cs] == JCS_RGB) ? "rgb      " :
                            "error", // FIXME string
                    test_qualities[i_q],
                    avg_compression_ratio,
                    1 - 1 / avg_compression_ratio,
                    avg_rms_err,
                    avg_max_err,
                    avg_compression_time,
                    avg_decompression_time,
                    1e-6 * avg_compression_rate,
                    1e-6 * avg_decompression_rate,
                    1e-6 * avg_bit_save_rate);

        }
    }

    fprintf(csv_ptr,"\n");
    fclose(csv_ptr); // close the csv

    // save processor info as well
    system("printf \"processor info:\n\" >> performance_jpeg.csv");
    system("lscpu >> performance_jpeg.csv");

    // regurgitate csv back to stdout
    csv_ptr = fopen("performance_jpeg.csv", "r");
    ASSERT_TRUE(csv_ptr != NULL);

    char buff[1000];
    char *gets_ret;
    while (fgets(buff, 1000, csv_ptr) != NULL) {
        puts(buff);
    }
    fclose(csv_ptr);

}

#endif

