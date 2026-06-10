#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <xkbcommon/xkbcommon.h>

typedef struct wl_app wl_app_t;

/* -------------------------------------------------- */
/* Event types                                        */
/* -------------------------------------------------- */

typedef struct {
    uint32_t        sym;      /* XKB keysym e.g. XKB_KEY_Return */
    uint32_t        codepoint; /* unicode codepoint e.g. 'a', 'A', 0 if non-printable */
    bool            pressed;
} wl_key_event_t;

typedef struct {
    double          x;
    double          y;
} wl_mouse_move_event_t;

typedef struct {
    double          x;
    double          y;
    uint32_t        button;   /* 1=left, 2=middle, 3=right */
    bool            pressed;
} wl_mouse_button_event_t;

/* -------------------------------------------------- */
/* Callback types                                     */
/* -------------------------------------------------- */

typedef void (*wl_key_fn)          (wl_key_event_t *e,          void *userdata);
typedef void (*wl_mouse_move_fn)   (wl_mouse_move_event_t *e,   void *userdata);
typedef void (*wl_mouse_button_fn) (wl_mouse_button_event_t *e, void *userdata);

/* -------------------------------------------------- */
/* Registration                                       */
/* -------------------------------------------------- */

void wl_app_on_key         (wl_app_t *app, wl_key_fn fn,          void *userdata);
void wl_app_on_mouse_move  (wl_app_t *app, wl_mouse_move_fn fn,   void *userdata);
void wl_app_on_mouse_button(wl_app_t *app, wl_mouse_button_fn fn, void *userdata);
