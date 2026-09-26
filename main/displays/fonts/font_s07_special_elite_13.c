/*******************************************************************************
 * Size: 13 px
 * Bpp: 4
 * Opts: --font ttf/SpecialElite-Regular.ttf -r 0x47,0x49,0x4C-0x4F,0x53 --size 13 --bpp 4 --format lvgl --lv-font-name font_s07_special_elite_13 -o ../../main/displays/fonts/font_s07_special_elite_13.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

#ifndef FONT_S07_SPECIAL_ELITE_13
#define FONT_S07_SPECIAL_ELITE_13 1
#endif

#if FONT_S07_SPECIAL_ELITE_13

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0047 "G" */
    0x1, 0xbf, 0xa1, 0x60, 0x6, 0x97, 0xcf, 0xf0,
    0x11, 0x24, 0xe, 0x48, 0x18, 0xc0, 0x27, 0xe0,
    0x21, 0x0, 0xa3, 0x90, 0x9c, 0x2, 0x15, 0x75,
    0x50, 0x5, 0xe4, 0x62, 0x8e, 0x5, 0xc, 0x0,
    0xbd, 0xfc, 0x60, 0x0,

    /* U+0049 "I" */
    0x0, 0xf9, 0xff, 0xee, 0xd0, 0x6d, 0x90, 0xdc,
    0x1, 0x25, 0x13, 0x10, 0xc, 0x40, 0x1f, 0xfc,
    0x7, 0x10, 0xe, 0x73, 0x0, 0x84, 0xec, 0xc,
    0x82, 0x71, 0xc7, 0x30, 0x0,

    /* U+004C "L" */
    0x7f, 0xf9, 0x80, 0x27, 0x90, 0xc6, 0x0, 0xca,
    0x46, 0x1, 0xc2, 0x20, 0xf, 0x18, 0x80, 0x78,
    0xcc, 0x1, 0x19, 0x1, 0x88, 0x5, 0x6a, 0xa,
    0x6, 0x24, 0x4f, 0x3b, 0xc, 0xed, 0x5c, 0x3f,
    0xfb, 0xbf, 0xc8,

    /* U+004D "M" */
    0x0, 0xfe, 0x1c, 0xb0, 0xf, 0xb, 0x21, 0x82,
    0xff, 0x18, 0x68, 0x20, 0x68, 0x49, 0x80, 0x86,
    0x81, 0x9b, 0x80, 0x28, 0x10, 0x68, 0x0, 0xb8,
    0x2, 0x70, 0xe0, 0x7, 0x81, 0x20, 0x38, 0x6,
    0x1f, 0xac, 0x10, 0x1, 0xc5, 0xa0, 0xac, 0x52,
    0x1f, 0xe9, 0x71, 0xd6, 0xa0,

    /* U+004E "N" */
    0x5f, 0xe2, 0x6f, 0x80, 0x44, 0xd, 0x35, 0x3a,
    0x8e, 0x93, 0x3, 0xa2, 0x81, 0xa2, 0x44, 0x1,
    0x8, 0x4f, 0x83, 0x80, 0x1c, 0x4d, 0xc9, 0x80,
    0x2, 0x41, 0x76, 0x20, 0xe, 0x77, 0x20, 0x1f,
    0x2c, 0x4, 0xb8, 0x1e, 0x7c, 0x3, 0xd8, 0x0,

    /* U+004F "O" */
    0x0, 0x97, 0xec, 0x2, 0x8b, 0x1a, 0xb, 0x2,
    0x7b, 0x45, 0xb4, 0x4, 0x30, 0xc, 0x88, 0x36,
    0x0, 0xc4, 0x47, 0x10, 0xc, 0x66, 0x25, 0x30,
    0x9, 0xd0, 0x1f, 0x58, 0xa5, 0x44, 0x25, 0xa3,
    0x42, 0x40,

    /* U+0053 "S" */
    0x6, 0xfe, 0xff, 0x28, 0x2, 0x6b, 0xbd, 0x88,
    0x0, 0x7a, 0x41, 0x8, 0x0, 0xd1, 0xfe, 0xd0,
    0x1, 0x1c, 0xef, 0x7e, 0x83, 0x50, 0x4, 0x60,
    0x12, 0x98, 0xd, 0xa0, 0x28, 0xff, 0x6e, 0x50,
    0x0
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 128, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 36, .adv_w = 103, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 65, .adv_w = 125, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 100, .adv_w = 145, .box_w = 9, .box_h = 11, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 145, .adv_w = 131, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 185, .adv_w = 128, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 219, .adv_w = 122, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 0}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint8_t glyph_id_ofs_list_0[] = {
    0, 0, 1, 0, 0, 2, 3, 4,
    5, 0, 0, 0, 6
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 71, .range_length = 13, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = glyph_id_ofs_list_0, .list_length = 13, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 1,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t font_s07_special_elite_13 = {
#else
lv_font_t font_s07_special_elite_13 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 11,          /*The maximum line height required by the font*/
    .base_line = 1,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -1,
    .underline_thickness = 0,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if FONT_S07_SPECIAL_ELITE_13*/

