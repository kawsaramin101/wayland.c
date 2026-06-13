#include <stdlib.h>
#include <string.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_LCD_FILTER_H
#include "font.h"
#include "wayland.h"

struct wl_font {
    FT_Library library;
    FT_Face    face;
    int        size_px;
};

/* -------------------------------------------------- */
/* Lifecycle                                          */
/* -------------------------------------------------- */

wl_font_t *wl_font_load(const char *path, int size_px) {
    wl_font_t *font = calloc(1, sizeof(*font));
    if (!font) return NULL;
    if (FT_Init_FreeType(&font->library)) {
        free(font);
        return NULL;
    }
    FT_Library_SetLcdFilter(font->library, FT_LCD_FILTER_DEFAULT);
    if (FT_New_Face(font->library, path, 0, &font->face)) {
        FT_Done_FreeType(font->library);
        free(font);
        return NULL;
    }
    /* convert pixels to points at 96 DPI: points = pixels * 72 / 96
       FT_Set_Char_Size takes 1/64 point units */
    FT_F26Dot6 size_pt = (FT_F26Dot6)(size_px * 72 * 64 / 96);
    FT_Set_Char_Size(font->face, 0, size_pt, 96, 96);
    font->size_px = size_px;
    return font;
}

void wl_font_destroy(wl_font_t *font) {
    if (!font) return;
    FT_Done_Face(font->face);
    FT_Done_FreeType(font->library);
    free(font);
}

/* -------------------------------------------------- */
/* Blending                                           */
/* -------------------------------------------------- */

static void blend_pixel_lcd(wl_canvas_t *canvas, int x, int y,
                             uint32_t color, uint8_t ar, uint8_t ag, uint8_t ab)
{
    /* respect clip region */
    if (x < canvas->clip_x || x >= canvas->clip_x + canvas->clip_w) return;
    if (y < canvas->clip_y || y >= canvas->clip_y + canvas->clip_h) return;

    uint32_t dst = canvas->data[y * canvas->width + x];

    uint8_t src_r = (color >> 16) & 0xFF;
    uint8_t src_g = (color >>  8) & 0xFF;
    uint8_t src_b = (color      ) & 0xFF;

    uint8_t dst_r = (dst >> 16) & 0xFF;
    uint8_t dst_g = (dst >>  8) & 0xFF;
    uint8_t dst_b = (dst      ) & 0xFF;

    uint8_t out_r = (src_r * ar + dst_r * (255 - ar)) / 255;
    uint8_t out_g = (src_g * ag + dst_g * (255 - ag)) / 255;
    uint8_t out_b = (src_b * ab + dst_b * (255 - ab)) / 255;

    canvas->data[y * canvas->width + x] = 0xFF000000 | (out_r << 16) | (out_g << 8) | out_b;
}

/* -------------------------------------------------- */
/* Drawing                                            */
/* -------------------------------------------------- */

void wl_draw_text(wl_canvas_t *canvas, wl_font_t *font,
                  int x, int y, const char *text, uint32_t color)
{
    if (!font || !text) return;

    FT_GlyphSlot slot = font->face->glyph;
    int pen_x = x;

    for (const char *p = text; *p; p++) {
        if (FT_Load_Char(font->face, (unsigned char)*p,
                FT_LOAD_RENDER | FT_LOAD_TARGET_LCD))
            continue;

        FT_Bitmap *bmp = &slot->bitmap;

        int glyph_x = pen_x + slot->bitmap_left;
        int glyph_y = y     - slot->bitmap_top;

        int pixel_width = bmp->width / 3;
        for (int row = 0; row < (int)bmp->rows; row++) {
            for (int col = 0; col < pixel_width; col++) {
                uint8_t ar = bmp->buffer[row * bmp->pitch + col * 3 + 0];
                uint8_t ag = bmp->buffer[row * bmp->pitch + col * 3 + 1];
                uint8_t ab = bmp->buffer[row * bmp->pitch + col * 3 + 2];
                blend_pixel_lcd(canvas, glyph_x + col, glyph_y + row, color, ar, ag, ab);
            }
        }

        pen_x += slot->advance.x >> 6;
    }
}

/* -------------------------------------------------- */
/* Measuring                                          */
/* -------------------------------------------------- */

int wl_text_width(wl_font_t *font, const char *text) {
    if (!font || !text) return 0;

    int width = 0;
    for (const char *p = text; *p; p++) {
        if (FT_Load_Char(font->face, (unsigned char)*p, FT_LOAD_DEFAULT))
            continue;
        width += font->face->glyph->advance.x >> 6;
    }
    return width;
}
