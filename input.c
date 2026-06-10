#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include "wayland.h"
#include "input.h"

/* -------------------------------------------------- */
/* Access to wl_app internals                         */
/* -------------------------------------------------- */

/* wl_app is defined in wayland.c — we access its fields
   via this forward declaration of the full struct.
   Both files are part of the same library so this is fine. */

struct wl_app {
    struct wl_display                 *display;
    struct wl_registry                *registry;
    struct wl_shm                     *shm;
    struct wl_compositor              *compositor;
    struct xdg_wm_base                *xdg_wm_base;
    struct zxdg_decoration_manager_v1 *decoration_manager;
    struct wl_surface                 *surface;
    struct xdg_surface                *xdg_surface;
    struct xdg_toplevel               *xdg_toplevel;
    struct wl_seat                    *seat;
    struct wl_pointer                 *pointer;
    struct wl_keyboard                *keyboard;
    double                             mouse_x;
    double                             mouse_y;
    struct xkb_context                *xkb_context;
    struct xkb_keymap                 *xkb_keymap;
    struct xkb_state                  *xkb_state;
    wl_key_fn                          key_fn;
    void                              *key_userdata;
    wl_mouse_move_fn                   mouse_move_fn;
    void                              *mouse_move_userdata;
    wl_mouse_button_fn                 mouse_button_fn;
    void                              *mouse_button_userdata;
    const char                        *title;
    int                                width;
    int                                height;
    int                                running;
    void                              *draw_fn;
};

/* -------------------------------------------------- */
/* Pointer callbacks                                  */
/* -------------------------------------------------- */

static void pointer_enter(void *data, struct wl_pointer *pointer,
        uint32_t serial, struct wl_surface *surface,
        wl_fixed_t sx, wl_fixed_t sy)
{
    (void)pointer; (void)serial; (void)surface;
    struct wl_app *app = data;
    app->mouse_x = wl_fixed_to_double(sx);
    app->mouse_y = wl_fixed_to_double(sy);
}

static void pointer_leave(void *data, struct wl_pointer *pointer,
        uint32_t serial, struct wl_surface *surface)
{
    (void)data; (void)pointer; (void)serial; (void)surface;
}

static void pointer_motion(void *data, struct wl_pointer *pointer,
        uint32_t time, wl_fixed_t sx, wl_fixed_t sy)
{
    (void)pointer; (void)time;
    struct wl_app *app = data;
    app->mouse_x = wl_fixed_to_double(sx);
    app->mouse_y = wl_fixed_to_double(sy);

    if (app->mouse_move_fn) {
        wl_mouse_move_event_t e = {
            .x = app->mouse_x,
            .y = app->mouse_y,
        };
        app->mouse_move_fn(&e, app->mouse_move_userdata);
    }
}

static void pointer_button(void *data, struct wl_pointer *pointer,
        uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
{
    (void)pointer; (void)serial; (void)time;
    struct wl_app *app = data;

    if (app->mouse_button_fn) {
        /* Linux input event codes: BTN_LEFT=0x110, BTN_RIGHT=0x111, BTN_MIDDLE=0x112 */
        uint32_t btn = 1;
        if      (button == 0x110) btn = 1;
        else if (button == 0x111) btn = 3;
        else if (button == 0x112) btn = 2;

        wl_mouse_button_event_t e = {
            .x       = app->mouse_x,
            .y       = app->mouse_y,
            .button  = btn,
            .pressed = (state == WL_POINTER_BUTTON_STATE_PRESSED),
        };
        app->mouse_button_fn(&e, app->mouse_button_userdata);
    }
}

static void pointer_axis(void *data, struct wl_pointer *pointer,
        uint32_t time, uint32_t axis, wl_fixed_t value)
{
    (void)data; (void)pointer; (void)time; (void)axis; (void)value;
}

static void pointer_frame(void *data, struct wl_pointer *pointer)
{
    (void)data; (void)pointer;
}

static void pointer_axis_source(void *data, struct wl_pointer *pointer, uint32_t source)
{
    (void)data; (void)pointer; (void)source;
}

static void pointer_axis_stop(void *data, struct wl_pointer *pointer,
        uint32_t time, uint32_t axis)
{
    (void)data; (void)pointer; (void)time; (void)axis;
}

static void pointer_axis_discrete(void *data, struct wl_pointer *pointer,
        uint32_t axis, int32_t discrete)
{
    (void)data; (void)pointer; (void)axis; (void)discrete;
}

const struct wl_pointer_listener wl_pointer_listener = {
    .enter         = pointer_enter,
    .leave         = pointer_leave,
    .motion        = pointer_motion,
    .button        = pointer_button,
    .axis          = pointer_axis,
    .frame         = pointer_frame,
    .axis_source   = pointer_axis_source,
    .axis_stop     = pointer_axis_stop,
    .axis_discrete = pointer_axis_discrete,
};

/* -------------------------------------------------- */
/* Keyboard callbacks                                 */
/* -------------------------------------------------- */

static void keyboard_keymap(void *data, struct wl_keyboard *keyboard,
        uint32_t format, int32_t fd, uint32_t size)
{
    (void)keyboard;
    struct wl_app *app = data;

    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }

    char *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED) return;

    struct xkb_keymap *keymap = xkb_keymap_new_from_string(
        app->xkb_context, map, XKB_KEYMAP_FORMAT_TEXT_V1,
        XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map, size);
    if (!keymap) return;

    struct xkb_state *state = xkb_state_new(keymap);
    if (!state) { xkb_keymap_unref(keymap); return; }

    if (app->xkb_state)  xkb_state_unref(app->xkb_state);
    if (app->xkb_keymap) xkb_keymap_unref(app->xkb_keymap);

    app->xkb_keymap = keymap;
    app->xkb_state  = state;
}

static void keyboard_enter(void *data, struct wl_keyboard *keyboard,
        uint32_t serial, struct wl_surface *surface, struct wl_array *keys)
{
    (void)data; (void)keyboard; (void)serial; (void)surface; (void)keys;
}

static void keyboard_leave(void *data, struct wl_keyboard *keyboard,
        uint32_t serial, struct wl_surface *surface)
{
    (void)data; (void)keyboard; (void)serial; (void)surface;
}

static void keyboard_key(void *data, struct wl_keyboard *keyboard,
        uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
{
    (void)keyboard; (void)serial; (void)time;
    struct wl_app *app = data;
    if (!app->xkb_state || !app->key_fn) return;

    /* Wayland key codes are evdev codes, XKB expects evdev+8 */
    uint32_t keycode = key + 8;
    xkb_keysym_t sym = xkb_state_key_get_one_sym(app->xkb_state, keycode);

    /* get unicode codepoint */
    uint32_t codepoint = xkb_state_key_get_utf32(app->xkb_state, keycode);

    wl_key_event_t e = {
        .sym       = sym,
        .codepoint = codepoint,
        .pressed   = (state == WL_KEYBOARD_KEY_STATE_PRESSED),
    };
    app->key_fn(&e, app->key_userdata);
}

static void keyboard_modifiers(void *data, struct wl_keyboard *keyboard,
        uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched,
        uint32_t mods_locked, uint32_t group)
{
    (void)keyboard; (void)serial;
    struct wl_app *app = data;
    if (app->xkb_state)
        xkb_state_update_mask(app->xkb_state,
            mods_depressed, mods_latched, mods_locked, 0, 0, group);
}

static void keyboard_repeat_info(void *data, struct wl_keyboard *keyboard,
        int32_t rate, int32_t delay)
{
    (void)data; (void)keyboard; (void)rate; (void)delay;
}

const struct wl_keyboard_listener wl_keyboard_listener = {
    .keymap      = keyboard_keymap,
    .enter       = keyboard_enter,
    .leave       = keyboard_leave,
    .key         = keyboard_key,
    .modifiers   = keyboard_modifiers,
    .repeat_info = keyboard_repeat_info,
};

/* -------------------------------------------------- */
/* Public API                                         */
/* -------------------------------------------------- */

void wl_app_on_key(wl_app_t *app, wl_key_fn fn, void *userdata) {
    app->key_fn       = fn;
    app->key_userdata = userdata;
}

void wl_app_on_mouse_move(wl_app_t *app, wl_mouse_move_fn fn, void *userdata) {
    app->mouse_move_fn       = fn;
    app->mouse_move_userdata = userdata;
}

void wl_app_on_mouse_button(wl_app_t *app, wl_mouse_button_fn fn, void *userdata) {
    app->mouse_button_fn       = fn;
    app->mouse_button_userdata = userdata;
}
