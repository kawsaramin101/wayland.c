#include <stdio.h>
#include <math.h>
#include "wayland_dot_c.h"

#define FONT_PATH "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
#define STEP      10
#define BOX_SIZE  60

static wl_app_t  *app;
static wl_font_t *font;
static int        box_x    = 50;
static int        box_y    = 50;
static double     mouse_x  = 0;
static double     mouse_y  = 0;
static double     scroll_y = 0;

void draw(wl_canvas_t *canvas) {
    double s = canvas->scale;

    wl_draw_fill(canvas, 0xFF1A1A2E);

    /* instructions */
    wl_draw_text(canvas, font, (int)(10*s), (int)(20*s),
        "Arrow keys move box (hold for repeat)  |  Scroll wheel  |  Mouse position",
        0xFF888888);

    /* blue box */
    wl_draw_rect(canvas,
        (int)(box_x * s), (int)(box_y * s),
        (int)(BOX_SIZE * s), (int)(BOX_SIZE * s),
        0xFF4F8EF7);

    /* box label */
    char pos[32];
    snprintf(pos, sizeof(pos), "%d,%d", box_x, box_y);
    wl_canvas_set_clip(canvas,
        (int)(box_x*s), (int)(box_y*s),
        (int)(BOX_SIZE*s), (int)(BOX_SIZE*s));
    wl_draw_text(canvas, font,
        (int)(box_x*s) + 4,
        (int)((box_y + BOX_SIZE)*s) - 8,
        pos, 0xFFFFFFFF);
    wl_canvas_reset_clip(canvas);

    /* mouse position */
    char mbuf[64];
    snprintf(mbuf, sizeof(mbuf), "Mouse: %.0f, %.0f", mouse_x, mouse_y);
    wl_draw_text(canvas, font, (int)(10*s), (int)(canvas->height - 40),
        mbuf, 0xFFCCCCCC);

    /* scroll indicator */
    char sbuf[64];
    snprintf(sbuf, sizeof(sbuf), "Scroll: %.0f", scroll_y);
    wl_draw_text(canvas, font, (int)(10*s), (int)(canvas->height - 20),
        sbuf, 0xFFCCCCCC);

    /* shapes showcase */
    wl_draw_text(canvas, font, (int)(350*s), (int)(20*s), "Shapes:", 0xFF888888);

    /* line */
    wl_draw_line(canvas,
        (int)(350*s), (int)(40*s),
        (int)(550*s), (int)(90*s),
        0xFFFFD700);

    /* circle outline */
    wl_draw_circle(canvas,
        (int)(420*s), (int)(150*s),
        (int)(35*s),
        0xFF44DDFF);

    /* circle filled */
    wl_draw_circle_filled(canvas,
        (int)(520*s), (int)(150*s),
        (int)(35*s),
        0xFFFF66AA);
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

void on_mouse_move(wl_mouse_move_event_t *e, void *userdata) {
    (void)userdata;
    mouse_x = e->x;
    mouse_y = e->y;
    wl_app_redraw(app);
}

void on_scroll(wl_scroll_event_t *e, void *userdata) {
    (void)userdata;
    scroll_y += e->dy;
    wl_app_redraw(app);
}

void on_mouse_button(wl_mouse_button_event_t *e, void *userdata) {
    (void)userdata;
    if (!e->pressed) return;
    /* clicking teleports box to click position */
    box_x = (int)e->x - BOX_SIZE / 2;
    box_y = (int)e->y - BOX_SIZE / 2;
    wl_app_redraw(app);
}

int main(void) {
    app  = wl_app_create("wayland.c demo", 640, 480);
    font = wl_font_load(FONT_PATH, 13);

    wl_app_on_draw        (app, draw);
    wl_app_on_key         (app, on_key,          NULL);
    wl_app_on_mouse_move  (app, on_mouse_move,   NULL);
    wl_app_on_mouse_button(app, on_mouse_button, NULL);
    wl_app_on_scroll      (app, on_scroll,       NULL);

    wl_app_run(app);

    wl_font_destroy(font);
    wl_app_destroy(app);
    return 0;
}
