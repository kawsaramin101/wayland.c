# Wayland.c

A minimal Wayland client library for Linux desktops. Sits between the very low level `libwayland-client` and full toolkits like SDL or GLFW.

It is written by Claude. Use at your own responsibility.

---

## What it is

A thin, readable wrapper over the Wayland protocol that handles the boilerplate of creating a window, receiving input, and drawing pixels — so you can get to your actual application code faster.

It provides:

- **Window management** — create a window, handle resize, close button, maximize, server-side decorations (title bar drawn by the compositor)
- **Software rendering** — a pixel buffer you can draw into directly, with primitive drawing functions (fill, rect, pixel)
- **Input handling** — keyboard events with full Unicode/UTF-8 translation via xkbcommon, mouse motion and button events
- **Text rendering** — FreeType-based font loading and LCD subpixel rendering for crisp text at small sizes

---

## What it is not

- **Not a UI toolkit** — there are no buttons, text fields, layouts, or widgets. That is intentionally left to a higher layer built on top of this library.
- **Not GPU accelerated** — rendering is pure CPU software rasterization into a shared memory buffer. Fine for UI, not suitable for games or real-time graphics.
- **Not cross-platform** — Wayland is Linux only. This library will not work on Windows, macOS, or X11.
- **Not production hardened** — error handling is minimal, there is no test suite, and the API may change. Review the code before shipping anything serious.
- **Not a replacement for SDL/GLFW** — those libraries are mature, cross-platform, and battle tested. Use them if that is what you need.

---

## Dependencies

- `libwayland-client`
- `libxkbcommon`
- `libfreetype2`
- `wayland-scanner` (build time)
- `wayland-protocols` (build time)

On Debian/Ubuntu:
```sh
sudo apt install libwayland-dev libxkbcommon-dev libfreetype-dev wayland-protocols
```

On Arch:
```sh
sudo pacman -S wayland libxkbcommon freetype2 wayland-protocols
```

---

## Building

```sh
make lib      # builds build/libwl.a and build/libwl.so
make example  # builds build/example
make all      # both
make clean
```

---

## Usage

```c
#include "wayland.h"
#include "input.h"
#include "font.h"

static wl_app_t  *app;
static wl_font_t *font;

void draw(wl_canvas_t *canvas) {
    wl_draw_fill(canvas, 0xFF181818);
    wl_draw_text(canvas, font, 20, 40, "Hello!", 0xFFEEEEEE);
}

void on_key(wl_key_event_t *e, void *userdata) {
    (void)userdata;
    if (e->pressed)
        wl_app_redraw(app);
}

int main(void) {
    app  = wl_app_create("My App", 640, 480);
    font = wl_font_load("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 13);

    wl_app_on_draw(app, draw);
    wl_app_on_key(app, on_key, NULL);

    wl_app_run(app);

    wl_font_destroy(font);
    wl_app_destroy(app);
    return 0;
}
```

Colors are `0xAARRGGBB`. The draw callback is called on first show and on every resize. Call `wl_app_redraw()` manually when your state changes — there is no render loop.

---

## Files

| File | Purpose |
|------|---------|
| `wayland.c/h` | Window creation, event loop, drawing primitives |
| `input.c/h` | Keyboard and mouse input |
| `font.c/h` | Font loading and text rendering |
| `example.c` | Minimal usage example |

---

## License

Do whatever you want with it.
