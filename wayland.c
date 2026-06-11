#define _POSIX_C_SOURCE 200112L
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-client-protocol.h"
#include "wayland.h"
#include "input.h"

struct wl_canvas {
    uint32_t *data;
    int       width;
    int       height;
    int       clip_x, clip_y;
    int       clip_w, clip_h;
};

struct wl_app {
    /* Wayland globals */
    struct wl_display                 *display;
    struct wl_registry                *registry;
    struct wl_shm                     *shm;
    struct wl_compositor              *compositor;
    struct xdg_wm_base                *xdg_wm_base;
    struct zxdg_decoration_manager_v1 *decoration_manager;

    /* Wayland objects */
    struct wl_surface   *surface;
    struct xdg_surface  *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;

    /* Input objects */
    struct wl_seat     *seat;
    struct wl_pointer  *pointer;
    struct wl_keyboard *keyboard;

    /* Mouse state */
    double mouse_x;
    double mouse_y;

    /* XKB state for key translation */
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

    /* App state */
    const char *title;
    int         width;
    int         height;
    bool        running;
    wl_draw_fn  draw_fn;
};

/* -------------------------------------------------- */
/* SHM helpers                                        */
/* -------------------------------------------------- */

static void randname(char *buf) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long r = ts.tv_nsec;
    for (int i = 0; i < 6; ++i) {
        buf[i] = 'A' + (r & 15) + (r & 16) * 2;
        r >>= 5;
    }
}

static int create_shm_file(void) {
    int retries = 100;
    do {
        char name[] = "/wl_shm-XXXXXX";
        randname(name + sizeof(name) - 7);
        --retries;
        int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) {
            shm_unlink(name);
            return fd;
        }
    } while (retries > 0 && errno == EEXIST);
    return -1;
}

static int allocate_shm_file(size_t size) {
    int fd = create_shm_file();
    if (fd < 0) return -1;
    int ret;
    do {
        ret = ftruncate(fd, size);
    } while (ret < 0 && errno == EINTR);
    if (ret < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* -------------------------------------------------- */
/* Buffer + Draw                                      */
/* -------------------------------------------------- */

static void buffer_release(void *data, struct wl_buffer *buffer) {
    (void)data;
    wl_buffer_destroy(buffer);
}

static const struct wl_buffer_listener buffer_listener = {
    .release = buffer_release,
};

static void do_draw(struct wl_app *app) {
    int stride = app->width * 4;
    int size   = stride * app->height;

    int fd = allocate_shm_file(size);
    if (fd < 0) return;

    uint32_t *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) { close(fd); return; }

    struct wl_shm_pool *pool   = wl_shm_create_pool(app->shm, fd, size);
    struct wl_buffer   *buffer = wl_shm_pool_create_buffer(
        pool, 0, app->width, app->height, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    wl_buffer_add_listener(buffer, &buffer_listener, NULL);

    if (app->draw_fn) {
        struct wl_canvas canvas = {
            .data   = data,
            .width  = app->width,
            .height = app->height,
            /* clip defaults to full canvas */
            .clip_x = 0,
            .clip_y = 0,
            .clip_w = app->width,
            .clip_h = app->height,
        };
        app->draw_fn(&canvas);
    }

    munmap(data, size);

    wl_surface_attach(app->surface, buffer, 0, 0);
    wl_surface_damage_buffer(app->surface, 0, 0, app->width, app->height);
    wl_surface_commit(app->surface);
}

/* -------------------------------------------------- */
/* XDG toplevel listener — close + resize             */
/* -------------------------------------------------- */

static void xdg_toplevel_configure(void *data,
        struct xdg_toplevel *toplevel, int32_t width, int32_t height,
        struct wl_array *states)
{
    (void)toplevel;
    (void)states;
    struct wl_app *app = data;
    if (width > 0)  app->width  = width;
    if (height > 0) app->height = height;
}

static void xdg_toplevel_close(void *data, struct xdg_toplevel *toplevel) {
    (void)toplevel;
    struct wl_app *app = data;
    app->running = false;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_configure,
    .close     = xdg_toplevel_close,
};

/* -------------------------------------------------- */
/* XDG surface listener                               */
/* -------------------------------------------------- */

static void xdg_surface_configure(void *data,
        struct xdg_surface *xdg_surface, uint32_t serial)
{
    struct wl_app *app = data;
    xdg_surface_ack_configure(xdg_surface, serial);
    do_draw(app);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

/* -------------------------------------------------- */
/* XDG wm base ping/pong                              */
/* -------------------------------------------------- */

static void xdg_wm_base_ping(void *data,
        struct xdg_wm_base *xdg_wm_base, uint32_t serial)
{
    (void)data;
    xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

/* -------------------------------------------------- */
/* Seat capability listener                           */
/* -------------------------------------------------- */

extern const struct wl_pointer_listener  wl_pointer_listener;
extern const struct wl_keyboard_listener wl_keyboard_listener;

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
    struct wl_app *app = data;

    if (caps & WL_SEAT_CAPABILITY_POINTER) {
        app->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(app->pointer, &wl_pointer_listener, app);
    }
    if (caps & WL_SEAT_CAPABILITY_KEYBOARD) {
        app->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(app->keyboard, &wl_keyboard_listener, app);
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name) {
    (void)data; (void)seat; (void)name;
}

static const struct wl_seat_listener wl_seat_listener = {
    .capabilities = seat_capabilities,
    .name         = seat_name,
};

/* -------------------------------------------------- */
/* Registry                                           */
/* -------------------------------------------------- */

static void registry_global(void *data, struct wl_registry *registry,
        uint32_t name, const char *interface, uint32_t version)
{
    (void)version;
    struct wl_app *app = data;

    if (strcmp(interface, wl_shm_interface.name) == 0) {
        app->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);

    } else if (strcmp(interface, wl_compositor_interface.name) == 0) {
        app->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);

    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        app->xdg_wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(app->xdg_wm_base, &xdg_wm_base_listener, app);

    } else if (strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0) {
        app->decoration_manager = wl_registry_bind(
            registry, name, &zxdg_decoration_manager_v1_interface, 1);

    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        app->seat = wl_registry_bind(registry, name, &wl_seat_interface, 4);
        wl_seat_add_listener(app->seat, &wl_seat_listener, app);
    }
}

static void registry_global_remove(void *data,
        struct wl_registry *registry, uint32_t name)
{
    (void)data; (void)registry; (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global        = registry_global,
    .global_remove = registry_global_remove,
};

/* -------------------------------------------------- */
/* Public API                                         */
/* -------------------------------------------------- */

wl_app_t *wl_app_create(const char *title, int width, int height) {
    struct wl_app *app = calloc(1, sizeof(*app));
    if (!app) return NULL;

    app->title   = title;
    app->width   = width;
    app->height  = height;
    app->running = true;

    app->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

    app->display = wl_display_connect(NULL);
    if (!app->display) { free(app); return NULL; }

    app->registry = wl_display_get_registry(app->display);
    wl_registry_add_listener(app->registry, &registry_listener, app);
    wl_display_roundtrip(app->display);

    app->surface     = wl_compositor_create_surface(app->compositor);
    app->xdg_surface = xdg_wm_base_get_xdg_surface(app->xdg_wm_base, app->surface);
    xdg_surface_add_listener(app->xdg_surface, &xdg_surface_listener, app);

    app->xdg_toplevel = xdg_surface_get_toplevel(app->xdg_surface);
    xdg_toplevel_set_title(app->xdg_toplevel, title);
    xdg_toplevel_add_listener(app->xdg_toplevel, &xdg_toplevel_listener, app);

    if (app->decoration_manager) {
        struct zxdg_toplevel_decoration_v1 *decoration =
            zxdg_decoration_manager_v1_get_toplevel_decoration(
                app->decoration_manager, app->xdg_toplevel);
        zxdg_toplevel_decoration_v1_set_mode(decoration,
            ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }

    wl_surface_commit(app->surface);
    return app;
}

void wl_app_on_draw(wl_app_t *app, wl_draw_fn fn) {
    app->draw_fn = fn;
}

void wl_app_redraw(wl_app_t *app) {
    do_draw(app);
}

void wl_app_run(wl_app_t *app) {
    while (app->running && wl_display_dispatch(app->display) != -1) {}
}

void wl_app_destroy(wl_app_t *app) {
    if (!app) return;
    if (app->xkb_state)    xkb_state_unref(app->xkb_state);
    if (app->xkb_keymap)   xkb_keymap_unref(app->xkb_keymap);
    if (app->xkb_context)  xkb_context_unref(app->xkb_context);
    if (app->pointer)      wl_pointer_destroy(app->pointer);
    if (app->keyboard)     wl_keyboard_destroy(app->keyboard);
    if (app->seat)         wl_seat_destroy(app->seat);
    if (app->xdg_toplevel) xdg_toplevel_destroy(app->xdg_toplevel);
    if (app->xdg_surface)  xdg_surface_destroy(app->xdg_surface);
    if (app->surface)      wl_surface_destroy(app->surface);
    if (app->xdg_wm_base)  xdg_wm_base_destroy(app->xdg_wm_base);
    if (app->shm)          wl_shm_destroy(app->shm);
    if (app->compositor)   wl_compositor_destroy(app->compositor);
    if (app->registry)     wl_registry_destroy(app->registry);
    if (app->display)      wl_display_disconnect(app->display);
    free(app);
}

/* -------------------------------------------------- */
/* Clipping                                           */
/* -------------------------------------------------- */

void wl_canvas_set_clip(wl_canvas_t *canvas, int x, int y, int w, int h) {
    /* intersect requested clip with canvas bounds */
    int x1 = x < 0 ? 0 : x;
    int y1 = y < 0 ? 0 : y;
    int x2 = x + w > canvas->width  ? canvas->width  : x + w;
    int y2 = y + h > canvas->height ? canvas->height : y + h;
    canvas->clip_x = x1;
    canvas->clip_y = y1;
    canvas->clip_w = x2 - x1 < 0 ? 0 : x2 - x1;
    canvas->clip_h = y2 - y1 < 0 ? 0 : y2 - y1;
}

void wl_canvas_reset_clip(wl_canvas_t *canvas) {
    canvas->clip_x = 0;
    canvas->clip_y = 0;
    canvas->clip_w = canvas->width;
    canvas->clip_h = canvas->height;
}

/* -------------------------------------------------- */
/* Drawing primitives                                 */
/* -------------------------------------------------- */

/* check if pixel is inside clip region */
static inline int in_clip(wl_canvas_t *canvas, int x, int y) {
    return x >= canvas->clip_x && x < canvas->clip_x + canvas->clip_w &&
           y >= canvas->clip_y && y < canvas->clip_y + canvas->clip_h;
}

void wl_draw_fill(wl_canvas_t *canvas, uint32_t color) {
    for (int row = canvas->clip_y; row < canvas->clip_y + canvas->clip_h; row++)
        for (int col = canvas->clip_x; col < canvas->clip_x + canvas->clip_w; col++)
            canvas->data[row * canvas->width + col] = color;
}

void wl_draw_rect(wl_canvas_t *canvas, int x, int y, int w, int h, uint32_t color) {
    for (int row = y; row < y + h; row++)
        for (int col = x; col < x + w; col++)
            if (in_clip(canvas, col, row))
                canvas->data[row * canvas->width + col] = color;
}

void wl_draw_pixel(wl_canvas_t *canvas, int x, int y, uint32_t color) {
    if (in_clip(canvas, x, y))
        canvas->data[y * canvas->width + x] = color;
}
