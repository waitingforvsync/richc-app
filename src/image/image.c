/*
 * image.c - PNG decoder.
 *
 * Uses miniz for DEFLATE decompression.  Supports colour types 0 (greyscale),
 * 2 (RGB), 3 (indexed/palette → RGB8), 4 (greyscale+alpha → RGBA8), and 6
 * (RGBA), all at 8 bits per channel, non-interlaced only.
 *
 * Memory layout:
 *   inflate buffer (scratch)  — height × (1 + width × src_bpp) bytes
 *   pixel output  (arena)     — height × width × dst_bpp bytes
 */

#include "richc/image/image.h"
#include "richc/file.h"
#include "richc/debug.h"
#include <miniz.h>
#include <string.h>

/* ---- big-endian read ---- */

static uint32_t u32be_(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}

/* ---- PNG chunk type codes ---- */

#define PNG_IHDR_ 0x49484452u   /* 'IHDR' */
#define PNG_PLTE_ 0x504C5445u   /* 'PLTE' */
#define PNG_IDAT_ 0x49444154u   /* 'IDAT' */
#define PNG_IEND_ 0x49454E44u   /* 'IEND' */

/* ---- Paeth predictor (PNG spec section 9.4) ---- */

static uint8_t paeth_(uint8_t a, uint8_t b, uint8_t c)
{
    int p  = (int)a + (int)b - (int)c;
    int pa = p > (int)a ? p - (int)a : (int)a - p;
    int pb = p > (int)b ? p - (int)b : (int)b - p;
    int pc = p > (int)c ? p - (int)c : (int)c - p;
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc)             return b;
    return c;
}

/* ---- row defiltering ---- */

/*
 * Apply PNG filter reconstruction to one row in-place.
 * row  — pointer to the filter byte, followed by width*bpp pixel bytes.
 * prev — already-defiltered pixel bytes of the previous row, or NULL for row 0.
 * The filter byte is left in place; only the pixel bytes are modified.
 * Returns false for an unknown filter type.
 */
static bool defilter_row_(uint8_t *row, const uint8_t *prev,
                           uint32_t width, uint32_t bpp)
{
    uint8_t  filter = row[0];
    uint8_t *px     = row + 1;
    uint32_t n      = width * bpp;

    switch (filter) {
        case 0: /* None */
            break;

        case 1: /* Sub */
            for (uint32_t x = bpp; x < n; x++)
                px[x] = (uint8_t)(px[x] + px[x - bpp]);
            break;

        case 2: /* Up */
            if (prev)
                for (uint32_t x = 0; x < n; x++)
                    px[x] = (uint8_t)(px[x] + prev[x]);
            break;

        case 3: /* Average */
            for (uint32_t x = 0; x < n; x++) {
                uint8_t a = (x >= bpp) ? px[x - bpp] : 0;
                uint8_t b = prev       ? prev[x]      : 0;
                px[x] = (uint8_t)(px[x] + ((uint32_t)a + b) / 2);
            }
            break;

        case 4: /* Paeth */
            for (uint32_t x = 0; x < n; x++) {
                uint8_t a = (x >= bpp)         ? px[x - bpp]   : 0;
                uint8_t b = prev               ? prev[x]        : 0;
                uint8_t c = (prev && x >= bpp) ? prev[x - bpp] : 0;
                px[x] = (uint8_t)(px[x] + paeth_(a, b, c));
            }
            break;

        default:
            return false;
    }
    return true;
}

/* ---- rc_image_from_png ---- */

rc_image_result rc_image_from_png(rc_view_bytes png, rc_arena *arena, rc_arena scratch)
{
#define FAIL(e)  return (rc_image_result) { {0}, (e) }

    /* PNG signature */
    static const uint8_t k_sig[8] = { 0x89,'P','N','G',0x0D,0x0A,0x1A,0x0A };
    if (png.num < 8 || memcmp(png.data, k_sig, 8) != 0)
        FAIL(RC_IMAGE_ERROR_NOT_PNG);

    const uint8_t *p   = png.data + 8;
    uint32_t       rem = png.num  - 8;

    /* IHDR must be the first chunk: length(4) type(4) data(13) crc(4) = 25 bytes */
    if (rem < 25 || u32be_(p) != 13 || u32be_(p + 4) != PNG_IHDR_)
        FAIL(RC_IMAGE_ERROR_CORRUPT);

    const uint8_t *ihdr = p + 8;
    uint32_t width  = u32be_(ihdr);
    uint32_t height = u32be_(ihdr + 4);
    uint8_t  bit_depth  = ihdr[8];
    uint8_t  color_type = ihdr[9];
    uint8_t  interlace  = ihdr[12];

    if (width == 0 || height == 0) FAIL(RC_IMAGE_ERROR_CORRUPT);
    if (bit_depth  != 8)           FAIL(RC_IMAGE_ERROR_UNSUPPORTED);
    if (interlace  != 0)           FAIL(RC_IMAGE_ERROR_UNSUPPORTED);

    /* Limit dimensions so all byte-count arithmetic stays within uint32_t */
    if (width > 16384 || height > 16384) FAIL(RC_IMAGE_ERROR_UNSUPPORTED);

    /*
     * src_bpp: bytes per pixel in the filtered/compressed stream.
     * dst_bpp: bytes per pixel in the decoded output image.
     */
    uint32_t src_bpp, dst_bpp;
    rc_pixel_format format;
    switch (color_type) {
        case 0: src_bpp = 1; dst_bpp = 1; format = RC_PIXEL_FORMAT_R8;    break;
        case 2: src_bpp = 3; dst_bpp = 3; format = RC_PIXEL_FORMAT_RGB8;  break;
        case 3: src_bpp = 1; dst_bpp = 3; format = RC_PIXEL_FORMAT_RGB8;  break;
        case 4: src_bpp = 2; dst_bpp = 4; format = RC_PIXEL_FORMAT_RGBA8; break;
        case 6: src_bpp = 4; dst_bpp = 4; format = RC_PIXEL_FORMAT_RGBA8; break;
        default: FAIL(RC_IMAGE_ERROR_UNSUPPORTED);
    }

    p   += 4 + 4 + 13 + 4;
    rem -= 4 + 4 + 13 + 4;

    /*
     * Allocate inflate output buffer in scratch.
     * Each row: 1 filter byte + width*src_bpp pixel bytes.
     */
    uint32_t row_bytes    = 1 + width * src_bpp;
    uint32_t inflate_size = height * row_bytes;
    uint8_t *inflate_buf  = rc_arena_alloc_type(&scratch, uint8_t, inflate_size);

    /* Initialise inflate stream — miniz uses its own internal heap for state */
    mz_stream stream = {0};
    stream.next_out  = inflate_buf;
    stream.avail_out = inflate_size;
    if (mz_inflateInit(&stream) != MZ_OK)
        FAIL(RC_IMAGE_ERROR_CORRUPT);

    /* Palette for indexed colour (color_type == 3) */
    uint8_t palette[256 * 3];
    bool    has_palette = false;

    /* Scan chunks: collect PLTE, feed IDAT to inflate, stop at IEND */
    while (rem >= 12) {
        uint32_t       len  = u32be_(p);
        uint32_t       type = u32be_(p + 4);
        const uint8_t *cdata = p + 8;

        /* Guard against a length that would reach outside the buffer */
        if (len > rem - 12) {
            mz_inflateEnd(&stream);
            FAIL(RC_IMAGE_ERROR_CORRUPT);
        }

        if (type == PNG_IEND_) break;

        if (type == PNG_PLTE_) {
            if (len > 256 * 3 || len % 3 != 0) {
                mz_inflateEnd(&stream);
                FAIL(RC_IMAGE_ERROR_CORRUPT);
            }
            memcpy(palette, cdata, len);
            has_palette = true;

        } else if (type == PNG_IDAT_) {
            stream.next_in  = (const unsigned char *)cdata;
            stream.avail_in = len;
            int r = mz_inflate(&stream, MZ_NO_FLUSH);
            if (r != MZ_OK && r != MZ_STREAM_END) {
                mz_inflateEnd(&stream);
                FAIL(RC_IMAGE_ERROR_CORRUPT);
            }
        }

        p   += 4 + 4 + len + 4;
        rem -= 4 + 4 + len + 4;
    }

    mz_uint leftover = stream.avail_out;
    mz_inflateEnd(&stream);

    if (leftover != 0)                      FAIL(RC_IMAGE_ERROR_CORRUPT);
    if (color_type == 3 && !has_palette)    FAIL(RC_IMAGE_ERROR_CORRUPT);

    /* Defilter each row in-place in the inflate buffer */
    for (uint32_t y = 0; y < height; y++) {
        uint8_t       *row  = inflate_buf + y * row_bytes;
        const uint8_t *prev = (y > 0) ? inflate_buf + (y - 1) * row_bytes + 1 : NULL;
        if (!defilter_row_(row, prev, width, src_bpp)) {
            FAIL(RC_IMAGE_ERROR_CORRUPT);
        }
    }

    /* Allocate pixel buffer in the persistent arena */
    uint32_t stride = width * dst_bpp;
    uint8_t *pixels = rc_arena_alloc_type(arena, uint8_t, height * stride);

    /* Copy or expand from inflate_buf into pixels, skipping the filter byte */
    for (uint32_t y = 0; y < height; y++) {
        const uint8_t *src = inflate_buf + y * row_bytes + 1;
        uint8_t       *dst = pixels      + y * stride;

        switch (color_type) {
            case 0:  /* greyscale → R8 */
            case 2:  /* RGB → RGB8 */
            case 6:  /* RGBA → RGBA8 */
                memcpy(dst, src, stride);
                break;

            case 3:  /* indexed → RGB8 */
                for (uint32_t x = 0; x < width; x++) {
                    const uint8_t *c = palette + src[x] * 3;
                    dst[x * 3 + 0]   = c[0];
                    dst[x * 3 + 1]   = c[1];
                    dst[x * 3 + 2]   = c[2];
                }
                break;

            case 4:  /* greyscale+alpha → RGBA8 */
                for (uint32_t x = 0; x < width; x++) {
                    uint8_t g        = src[x * 2 + 0];
                    uint8_t a        = src[x * 2 + 1];
                    dst[x * 4 + 0]   = g;
                    dst[x * 4 + 1]   = g;
                    dst[x * 4 + 2]   = g;
                    dst[x * 4 + 3]   = a;
                }
                break;
        }
    }

    return (rc_image_result) {
        .image = {
            .data   = { pixels, height * stride },
            .width  = (int32_t)width,
            .height = (int32_t)height,
            .stride = (int32_t)stride,
            .format = format,
        },
        .error = RC_IMAGE_OK,
    };

#undef FAIL
}

/* ---- rc_image_load_png ---- */

rc_image_result rc_image_load_png(rc_str path, rc_arena *arena, rc_arena scratch)
{
    /* Null-terminate path in scratch for rc_load_binary */
    char *path_cstr = rc_arena_alloc_type(&scratch, char, path.len + 1);
    memcpy(path_cstr, path.data, path.len);
    path_cstr[path.len] = '\0';

    /* Load raw PNG bytes into scratch (disposable) */
    rc_load_binary_result file = rc_load_binary(path_cstr, &scratch);
    if (file.error != RC_FILE_OK)
        return (rc_image_result) { {0}, RC_IMAGE_ERROR_IO };

    /* Decode; inflate buffer also goes into scratch, pixels go into arena */
    return rc_image_from_png(file.data, arena, scratch);
}
