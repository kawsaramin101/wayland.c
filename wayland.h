#pragma once

#include <stdint.h>

typedef struct wl_app wl_app_t;

typedef struct wl_canvas {
    uint32_t *data;
    int       width;
    int       height;
} wl_canvas_t;

typedef void (*wl_draw_fn)(wl_canvas_t *canvas);

wl_app_t *wl_app_create(const char *title, int width, int height);
void      wl_app_on_draw(wl_app_t *app, wl_draw_fn fn);
void      wl_app_redraw(wl_app_t *app);
void      wl_app_run(wl_app_t *app);
void      wl_app_destroy(wl_app_t *app);

void wl_draw_fill(wl_canvas_t *canvas, uint32_t color);
void wl_draw_rect(wl_canvas_t *canvas, int x, int y, int w, int h, uint32_t color);
void wl_draw_pixel(wl_canvas_t *canvas, int x, int y, uint32_t color);
