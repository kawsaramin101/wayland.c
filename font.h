#pragma once

#include <stdint.h>
#include "wayland.h"

typedef struct wl_font wl_font_t;

wl_font_t *wl_font_load(const char *path, int size_px);
void       wl_font_destroy(wl_font_t *font);
void       wl_draw_text(wl_canvas_t *canvas, wl_font_t *font,
                        int x, int y, const char *text, uint32_t color);
int        wl_text_width(wl_font_t *font, const char *text);
