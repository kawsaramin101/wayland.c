#include <stdio.h>
#include "wayland.h"
#include "input.h"
#include "font.h"

static wl_app_t  *app;
static wl_font_t *font;

void draw(wl_canvas_t *canvas) {
    wl_draw_fill(canvas, 0xFF181818);
    wl_draw_text(canvas, font, 20, 40, "Hello from IBM Plex Sans!", 0xFFEEEEEE);
}

void on_key(wl_key_event_t *e, void *userdata) {
    (void)userdata;
    (void)e;
}

void on_mouse_button(wl_mouse_button_event_t *e, void *userdata) {
    (void)userdata;
    if (e->pressed)
        printf("click at %.0f,%.0f button=%d\n", e->x, e->y, e->button);
}

int main(void) {
    app  = wl_app_create("Example", 640, 480);
    font = wl_font_load("/usr/share/fonts/truetype/ibm-plex/IBMPlexSans-Regular.ttf", 13);

    wl_app_on_draw(app, draw);
    wl_app_on_key(app, on_key, NULL);
    wl_app_on_mouse_button(app, on_mouse_button, NULL);

    wl_app_run(app);

    wl_font_destroy(font);
    wl_app_destroy(app);
    return 0;
}
