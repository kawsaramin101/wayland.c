#pragma once

/* Private header shared between wayland.c and input.c only.
   Never include this from outside the library. */

#include <stdbool.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "fractional-scale-v1-client-protocol.h"
#include "input.h"
#include "wayland.h"

#define WL_MAX_TIMERS 16

typedef struct {
    int         fd;
    wl_timer_fn fn;
    void       *userdata;
    bool        active;
} wl_timer_t;

struct wl_app {
    /* Wayland globals */
    struct wl_display                     *display;
    struct wl_registry                    *registry;
    struct wl_shm                         *shm;
    struct wl_compositor                  *compositor;
    struct xdg_wm_base                    *xdg_wm_base;
    struct zxdg_decoration_manager_v1     *decoration_manager;
    struct wp_viewporter                  *viewporter;
    struct wp_fractional_scale_manager_v1 *fractional_scale_manager;

    /* Wayland objects */
    struct wl_surface             *surface;
    struct xdg_surface            *xdg_surface;
    struct xdg_toplevel           *xdg_toplevel;
    struct wp_viewport            *viewport;
    struct wp_fractional_scale_v1 *fractional_scale;

    /* Input objects */
    struct wl_seat     *seat;
    struct wl_pointer  *pointer;
    struct wl_keyboard *keyboard;

    /* Mouse state */
    double mouse_x;
    double mouse_y;

    /* XKB state */
    struct xkb_context *xkb_context;
    struct xkb_keymap  *xkb_keymap;
    struct xkb_state   *xkb_state;

    /* Input callbacks */
    wl_key_fn           key_fn;
    void               *key_userdata;
    wl_mouse_move_fn    mouse_move_fn;
    void               *mouse_move_userdata;
    wl_mouse_button_fn  mouse_button_fn;
    void               *mouse_button_userdata;
    wl_scroll_fn        scroll_fn;
    void               *scroll_userdata;

    /* Resize callback */
    wl_resize_fn        resize_fn;
    void               *resize_userdata;

    /* Key repeat */
    int          repeat_rate;       /* repeats per second, 0 = disabled */
    int          repeat_delay;      /* ms before repeat starts */
    int          repeat_fd;         /* timerfd, -1 if inactive */
    xkb_keysym_t repeat_sym;        /* sym of held key */
    uint32_t     repeat_codepoint;  /* codepoint of held key */

    /* Timers */
    wl_timer_t timers[WL_MAX_TIMERS];

    /* App state */
    const char *title;
    int         width;
    int         height;
    double      scale;
    bool        running;
    wl_draw_fn  draw_fn;
};
