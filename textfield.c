#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include "textfield.h"
#include "wayland.h"
#include "input.h"
#include "font.h"

#define TF_MAX     1024
#define TF_PAD     6      /* inner padding pixels */

/* colors */
#define COL_BG         0xFF1E1E1E
#define COL_BG_FOCUS   0xFF262626
#define COL_BORDER     0xFF555555
#define COL_BORDER_FOC 0xFF88AAFF
#define COL_TEXT       0xFFEEEEEE
#define COL_PLACEHOLDER 0xFF888888
#define COL_CURSOR     0xFFFFFFFF

struct wl_textfield {
    char        buf[TF_MAX];
    int         len;
    int         cursor;       /* index into buf */
    int         x, y, w, h;
    bool        focused;
    const char *placeholder;
};

/* -------------------------------------------------- */
/* Lifecycle                                          */
/* -------------------------------------------------- */

wl_textfield_t *wl_textfield_create(int x, int y, int w, int h, const char *placeholder) {
    wl_textfield_t *tf = calloc(1, sizeof(*tf));
    if (!tf) return NULL;
    tf->x           = x;
    tf->y           = y;
    tf->w           = w;
    tf->h           = h;
    tf->placeholder = placeholder;
    return tf;
}

void wl_textfield_destroy(wl_textfield_t *tf) {
    free(tf);
}

/* -------------------------------------------------- */
/* Drawing                                            */
/* -------------------------------------------------- */

void wl_textfield_draw(wl_textfield_t *tf, wl_canvas_t *canvas, wl_font_t *font) {
    /* background */
    wl_draw_rect(canvas, tf->x, tf->y, tf->w, tf->h,
                 tf->focused ? COL_BG_FOCUS : COL_BG);

    /* border — draw 1px border manually as 4 rects */
    uint32_t border_col = tf->focused ? COL_BORDER_FOC : COL_BORDER;
    wl_draw_rect(canvas, tf->x,              tf->y,              tf->w, 1,      border_col); /* top */
    wl_draw_rect(canvas, tf->x,              tf->y + tf->h - 1,  tf->w, 1,      border_col); /* bottom */
    wl_draw_rect(canvas, tf->x,              tf->y,              1,     tf->h,  border_col); /* left */
    wl_draw_rect(canvas, tf->x + tf->w - 1,  tf->y,              1,     tf->h,  border_col); /* right */

    /* text baseline: vertically centered */
    int baseline = tf->y + tf->h / 2 + 5; /* +5 is approx half cap-height */

    if (tf->len == 0 && !tf->focused && tf->placeholder) {
        /* placeholder */
        wl_draw_text(canvas, font, tf->x + TF_PAD, baseline,
                     tf->placeholder, COL_PLACEHOLDER);
    } else {
        /* actual text */
        wl_draw_text(canvas, font, tf->x + TF_PAD, baseline,
                     tf->buf, COL_TEXT);

        /* cursor */
        if (tf->focused) {
            /* measure width of text up to cursor position */
            char tmp[TF_MAX];
            memcpy(tmp, tf->buf, tf->cursor);
            tmp[tf->cursor] = '\0';
            int cursor_x = tf->x + TF_PAD + wl_text_width(font, tmp);
            int cursor_y = tf->y + TF_PAD;
            int cursor_h = tf->h - TF_PAD * 2;
            wl_draw_rect(canvas, cursor_x, cursor_y, 1, cursor_h, COL_CURSOR);
        }
    }
}

/* -------------------------------------------------- */
/* Helpers                                            */
/* -------------------------------------------------- */

/* insert a utf-8 encoded codepoint at cursor position */
static void insert_codepoint(wl_textfield_t *tf, uint32_t codepoint) {
    /* encode codepoint to utf-8 */
    char enc[5] = {0};
    int  enc_len = 0;

    if (codepoint < 0x80) {
        enc[0]  = codepoint;
        enc_len = 1;
    } else if (codepoint < 0x800) {
        enc[0]  = 0xC0 | (codepoint >> 6);
        enc[1]  = 0x80 | (codepoint & 0x3F);
        enc_len = 2;
    } else if (codepoint < 0x10000) {
        enc[0]  = 0xE0 | (codepoint >> 12);
        enc[1]  = 0x80 | ((codepoint >> 6) & 0x3F);
        enc[2]  = 0x80 | (codepoint & 0x3F);
        enc_len = 3;
    } else {
        enc[0]  = 0xF0 | (codepoint >> 18);
        enc[1]  = 0x80 | ((codepoint >> 12) & 0x3F);
        enc[2]  = 0x80 | ((codepoint >> 6) & 0x3F);
        enc[3]  = 0x80 | (codepoint & 0x3F);
        enc_len = 4;
    }

    if (tf->len + enc_len >= TF_MAX) return;

    /* shift everything after cursor right */
    memmove(tf->buf + tf->cursor + enc_len,
            tf->buf + tf->cursor,
            tf->len - tf->cursor + 1);

    memcpy(tf->buf + tf->cursor, enc, enc_len);
    tf->len    += enc_len;
    tf->cursor += enc_len;
}

/* move cursor left by one utf-8 character */
static void cursor_left(wl_textfield_t *tf) {
    if (tf->cursor == 0) return;
    tf->cursor--;
    /* step back over utf-8 continuation bytes (10xxxxxx) */
    while (tf->cursor > 0 && (tf->buf[tf->cursor] & 0xC0) == 0x80)
        tf->cursor--;
}

/* move cursor right by one utf-8 character */
static void cursor_right(wl_textfield_t *tf) {
    if (tf->cursor >= tf->len) return;
    tf->cursor++;
    while (tf->cursor < tf->len && (tf->buf[tf->cursor] & 0xC0) == 0x80)
        tf->cursor++;
}

/* delete one utf-8 character before cursor */
static void backspace(wl_textfield_t *tf) {
    if (tf->cursor == 0) return;
    int old_cursor = tf->cursor;
    cursor_left(tf);
    int char_len = old_cursor - tf->cursor;
    memmove(tf->buf + tf->cursor,
            tf->buf + old_cursor,
            tf->len - old_cursor + 1);
    tf->len -= char_len;
}

/* delete one utf-8 character after cursor */
static void delete_forward(wl_textfield_t *tf) {
    if (tf->cursor >= tf->len) return;
    int next = tf->cursor + 1;
    while (next < tf->len && (tf->buf[next] & 0xC0) == 0x80)
        next++;
    int char_len = next - tf->cursor;
    memmove(tf->buf + tf->cursor,
            tf->buf + next,
            tf->len - next + 1);
    tf->len -= char_len;
}

/* -------------------------------------------------- */
/* Input                                              */
/* -------------------------------------------------- */

bool wl_textfield_key(wl_textfield_t *tf, wl_key_event_t *e) {
    if (!tf->focused || !e->pressed) return false;

    switch (e->sym) {
        case XKB_KEY_Left:       cursor_left(tf);    return true;
        case XKB_KEY_Right:      cursor_right(tf);   return true;
        case XKB_KEY_Home:       tf->cursor = 0;     return true;
        case XKB_KEY_End:        tf->cursor = tf->len; return true;
        case XKB_KEY_BackSpace:  backspace(tf);      return true;
        case XKB_KEY_Delete:     delete_forward(tf); return true;
        default: break;
    }

    /* printable character */
    if (e->codepoint >= 32 && e->codepoint != 127) {
        insert_codepoint(tf, e->codepoint);
        return true;
    }

    return false;
}

bool wl_textfield_click(wl_textfield_t *tf, wl_mouse_button_event_t *e) {
    if (e->button != 1 || !e->pressed) return false;

    bool inside = e->x >= tf->x && e->x <= tf->x + tf->w &&
                  e->y >= tf->y && e->y <= tf->y + tf->h;

    bool was_focused = tf->focused;
    tf->focused = inside;

    if (inside)
        tf->cursor = tf->len; /* place cursor at end for now */

    return inside || was_focused;
}

/* -------------------------------------------------- */
/* Accessor                                           */
/* -------------------------------------------------- */

const char *wl_textfield_text(wl_textfield_t *tf) {
    return tf->buf;
}
