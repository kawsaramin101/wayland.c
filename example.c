#include <stdio.h>
#include "wayland_dot_c.h"

#define FONT_PATH "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
#define STEP      20
#define BOX_SIZE  60

static wl_app_t  *app;
static wl_font_t *font;
static int        box_x = 50;
static int        box_y = 50;

void draw(wl_canvas_t *canvas) {
    /* background */
    wl_draw_fill(canvas, 0xFF1A1A2E);

    /* instructions */
    wl_draw_text(canvas, font, 10, 20,
                 "Arrow keys move the box", 0xFFFFFFFF);

    /* box */
    wl_draw_rect(canvas, box_x, box_y, BOX_SIZE, BOX_SIZE, 0xFF4F8EF7);

    /* position label inside box */
    char label[32];
    snprintf(label, sizeof(label), "%d,%d", box_x, box_y);
    wl_canvas_set_clip(canvas, box_x, box_y, BOX_SIZE, BOX_SIZE);
    wl_draw_text(canvas, font, box_x + 4, box_y + BOX_SIZE - 8, label, 0xFFFFFFFF);
    wl_canvas_reset_clip(canvas);
}

void on_key(wl_key_event_t *e, void *userdata) {
    (void)userdata;
    if (!e->pressed) return;

    switch (e->sym) {
        case 0xFF51: box_x -= STEP; break; /* left  */
        case 0xFF53: box_x += STEP; break; /* right */
        case 0xFF52: box_y -= STEP; break; /* up    */
        case 0xFF54: box_y += STEP; break; /* down  */
        default: return;
    }

    wl_app_redraw(app);
}

void on_mouse_button(wl_mouse_button_event_t *e, void *userdata) {
    (void)userdata;
    if (!e->pressed) return;
    printf("click at %.0f,%.0f\n", e->x, e->y);
}

int main(void) {
    app  = wl_app_create("libwl example", 640, 480);
    font = wl_font_load(FONT_PATH, 13);

    wl_app_on_draw(app, draw);
    wl_app_on_key(app, on_key, NULL);
    wl_app_on_mouse_button(app, on_mouse_button, NULL);

    wl_app_run(app);

    wl_font_destroy(font);
    wl_app_destroy(app);
    return 0;
}
