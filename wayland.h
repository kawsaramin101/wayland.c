#pragma once

#include <stdint.h>

typedef struct wl_app wl_app_t;

typedef struct wl_canvas {
    uint32_t *data;
    int       width;
    int       height;
    int       clip_x, clip_y;
    int       clip_w, clip_h;
    double    scale;
} wl_canvas_t;

typedef void (*wl_draw_fn)  (wl_canvas_t *canvas);
typedef void (*wl_timer_fn) (void *userdata);
typedef void (*wl_resize_fn)(int w, int h, void *userdata);

/* App lifecycle */
wl_app_t *wl_app_create  (const char *title, int width, int height);
void      wl_app_on_draw  (wl_app_t *app, wl_draw_fn fn);
void      wl_app_on_resize(wl_app_t *app, wl_resize_fn fn, void *userdata);
void      wl_app_redraw   (wl_app_t *app);
void      wl_app_run      (wl_app_t *app);
void      wl_app_destroy  (wl_app_t *app);
double    wl_app_scale    (wl_app_t *app);
void      wl_app_size     (wl_app_t *app, int *w, int *h);

/* Timers */
int  wl_app_add_timer   (wl_app_t *app, int interval_ms, wl_timer_fn fn, void *userdata);
void wl_app_remove_timer(wl_app_t *app, int timer_id);

/* Clipping */
void wl_canvas_set_clip  (wl_canvas_t *canvas, int x, int y, int w, int h);
void wl_canvas_reset_clip(wl_canvas_t *canvas);

/* Drawing primitives */
void wl_draw_fill (wl_canvas_t *canvas, uint32_t color);
void wl_draw_rect (wl_canvas_t *canvas, int x, int y, int w, int h, uint32_t color);
void wl_draw_pixel(wl_canvas_t *canvas, int x, int y, uint32_t color);
void wl_draw_line  (wl_canvas_t *canvas, int x0, int y0, int x1, int y1, uint32_t color);
void wl_draw_circle(wl_canvas_t *canvas, int cx, int cy, int radius, uint32_t color);
void wl_draw_circle_filled(wl_canvas_t *canvas, int cx, int cy, int radius, uint32_t color);
