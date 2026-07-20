#define _POSIX_C_SOURCE 200112L
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>
#include <math.h>
#include <stdint.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "fractional-scale-v1-client-protocol.h"
#include "wayland.h"
#include "wayland_internal.h"

struct wl_canvas {
    uint32_t *data;
    int       width;
    int       height;
    int       clip_x, clip_y;
    int       clip_w, clip_h;
    double    scale;
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
    int phys_w = (int)ceil(app->width  * app->scale);
    int phys_h = (int)ceil(app->height * app->scale);

    int stride = phys_w * 4;
    int size   = stride * phys_h;

    int fd = allocate_shm_file(size);
    if (fd < 0) return;

    uint32_t *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) { close(fd); return; }

    struct wl_shm_pool *pool   = wl_shm_create_pool(app->shm, fd, size);
    struct wl_buffer   *buffer = wl_shm_pool_create_buffer(
        pool, 0, phys_w, phys_h, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    wl_buffer_add_listener(buffer, &buffer_listener, NULL);

    if (app->draw_fn) {
        struct wl_canvas canvas = {
            .data   = data,
            .width  = phys_w,
            .height = phys_h,
            .clip_x = 0,
            .clip_y = 0,
            .clip_w = phys_w,
            .clip_h = phys_h,
            .scale  = app->scale,
        };
        app->draw_fn(&canvas);
    }

    munmap(data, size);

    if (app->viewport)
        wp_viewport_set_destination(app->viewport, app->width, app->height);

    wl_surface_attach(app->surface, buffer, 0, 0);
    wl_surface_damage_buffer(app->surface, 0, 0, phys_w, phys_h);
    wl_surface_commit(app->surface);
}

/* -------------------------------------------------- */
/* Fractional scale listener                          */
/* -------------------------------------------------- */

static void fractional_scale_preferred(void *data,
        struct wp_fractional_scale_v1 *fs, uint32_t scale_120)
{
    (void)fs;
    struct wl_app *app = data;
    app->scale = scale_120 / 120.0;
}

static const struct wp_fractional_scale_v1_listener fractional_scale_listener = {
    .preferred_scale = fractional_scale_preferred,
};

/* -------------------------------------------------- */
/* XDG toplevel listener                              */
/* -------------------------------------------------- */

static void xdg_toplevel_configure(void *data,
        struct xdg_toplevel *toplevel, int32_t width, int32_t height,
        struct wl_array *states)
{
    (void)toplevel; (void)states;
    struct wl_app *app = data;
    if (width > 0)  app->width  = width;
    if (height > 0) app->height = height;
    if (app->resize_fn)
        app->resize_fn(app->width, app->height, app->resize_userdata);
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

    } else if (strcmp(interface, wp_viewporter_interface.name) == 0) {
        app->viewporter = wl_registry_bind(registry, name, &wp_viewporter_interface, 1);

    } else if (strcmp(interface, wp_fractional_scale_manager_v1_interface.name) == 0) {
        app->fractional_scale_manager = wl_registry_bind(
            registry, name, &wp_fractional_scale_manager_v1_interface, 1);
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
    app->scale      = 1.0;
    app->running    = true;
    app->repeat_fd  = -1;
    app->repeat_rate  = 25;
    app->repeat_delay = 400;

    app->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

    app->display = wl_display_connect(NULL);
    if (!app->display) { free(app); return NULL; }

    app->registry = wl_display_get_registry(app->display);
    wl_registry_add_listener(app->registry, &registry_listener, app);
    wl_display_roundtrip(app->display);

    app->surface = wl_compositor_create_surface(app->compositor);

    if (app->fractional_scale_manager) {
        app->fractional_scale = wp_fractional_scale_manager_v1_get_fractional_scale(
            app->fractional_scale_manager, app->surface);
        wp_fractional_scale_v1_add_listener(app->fractional_scale,
            &fractional_scale_listener, app);
    }

    if (app->viewporter)
        app->viewport = wp_viewporter_get_viewport(app->viewporter, app->surface);

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
    wl_display_roundtrip(app->display);

    return app;
}

void wl_app_on_draw(wl_app_t *app, wl_draw_fn fn) {
    app->draw_fn = fn;
}

void wl_app_on_resize(wl_app_t *app, wl_resize_fn fn, void *userdata) {
    app->resize_fn       = fn;
    app->resize_userdata = userdata;
}

void wl_app_redraw(wl_app_t *app) {
    do_draw(app);
}

/* declared in input.c */
extern void wl_app_dispatch_repeat(struct wl_app *app);

void wl_app_run(wl_app_t *app) {
    struct pollfd fds[2 + WL_MAX_TIMERS];
    fds[0].fd     = wl_display_get_fd(app->display);
    fds[0].events = POLLIN;

    while (app->running) {
        if (wl_display_flush(app->display) < 0 && errno != EAGAIN)
            break;

        int nfds = 1;

        /* slot 1: key repeat fd */
        if (app->repeat_fd >= 0) {
            fds[nfds].fd     = app->repeat_fd;
            fds[nfds].events = POLLIN;
            nfds++;
        }

        /* remaining: app timers */
        for (int i = 0; i < WL_MAX_TIMERS; i++) {
            if (app->timers[i].active) {
                fds[nfds].fd     = app->timers[i].fd;
                fds[nfds].events = POLLIN;
                nfds++;
            }
        }

        if (poll(fds, nfds, -1) < 0) {
            if (errno == EINTR) continue;
            break;
        }

        /* Wayland events */
        if (fds[0].revents & POLLIN)
            if (wl_display_dispatch(app->display) < 0) break;

        /* key repeat */
        int slot = 1;
        if (app->repeat_fd >= 0) {
            if (fds[slot].revents & POLLIN) {
                uint64_t exp;
                read(app->repeat_fd, &exp, sizeof(exp));
                wl_app_dispatch_repeat(app);
            }
            slot++;
        }

        /* app timers */
        for (int i = 0; i < WL_MAX_TIMERS; i++) {
            if (!app->timers[i].active) continue;
            if (fds[slot].revents & POLLIN) {
                uint64_t exp;
                read(app->timers[i].fd, &exp, sizeof(exp));
                app->timers[i].fn(app->timers[i].userdata);
            }
            slot++;
        }
    }
}

int wl_app_add_timer(wl_app_t *app, int interval_ms, wl_timer_fn fn, void *userdata) {
    for (int i = 0; i < WL_MAX_TIMERS; i++) {
        if (app->timers[i].active) continue;

        int fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        if (fd < 0) return -1;

        struct itimerspec ts = {
            .it_interval = { interval_ms / 1000, (interval_ms % 1000) * 1000000L },
            .it_value    = { interval_ms / 1000, (interval_ms % 1000) * 1000000L },
        };
        timerfd_settime(fd, 0, &ts, NULL);

        app->timers[i].fd       = fd;
        app->timers[i].fn       = fn;
        app->timers[i].userdata = userdata;
        app->timers[i].active   = true;
        return i;
    }
    return -1;
}

void wl_app_remove_timer(wl_app_t *app, int timer_id) {
    if (timer_id < 0 || timer_id >= WL_MAX_TIMERS) return;
    if (!app->timers[timer_id].active) return;
    close(app->timers[timer_id].fd);
    app->timers[timer_id].active = false;
}

double wl_app_scale(wl_app_t *app) {
    return app->scale;
}

void wl_app_size(wl_app_t *app, int *w, int *h) {
    if (w) *w = app->width;
    if (h) *h = app->height;
}

void wl_app_destroy(wl_app_t *app) {
    if (!app) return;
    if (app->repeat_fd >= 0) close(app->repeat_fd);
    for (int i = 0; i < WL_MAX_TIMERS; i++)
        if (app->timers[i].active) close(app->timers[i].fd);
    if (app->fractional_scale) wp_fractional_scale_v1_destroy(app->fractional_scale);
    if (app->viewport)         wp_viewport_destroy(app->viewport);
    if (app->viewporter)       wp_viewporter_destroy(app->viewporter);
    if (app->xkb_state)        xkb_state_unref(app->xkb_state);
    if (app->xkb_keymap)       xkb_keymap_unref(app->xkb_keymap);
    if (app->xkb_context)      xkb_context_unref(app->xkb_context);
    if (app->pointer)          wl_pointer_destroy(app->pointer);
    if (app->keyboard)         wl_keyboard_destroy(app->keyboard);
    if (app->seat)             wl_seat_destroy(app->seat);
    if (app->xdg_toplevel)     xdg_toplevel_destroy(app->xdg_toplevel);
    if (app->xdg_surface)      xdg_surface_destroy(app->xdg_surface);
    if (app->surface)          wl_surface_destroy(app->surface);
    if (app->xdg_wm_base)      xdg_wm_base_destroy(app->xdg_wm_base);
    if (app->shm)              wl_shm_destroy(app->shm);
    if (app->compositor)       wl_compositor_destroy(app->compositor);
    if (app->registry)         wl_registry_destroy(app->registry);
    if (app->display)          wl_display_disconnect(app->display);
    free(app);
}

/* -------------------------------------------------- */
/* Clipping                                           */
/* -------------------------------------------------- */

void wl_canvas_set_clip(wl_canvas_t *canvas, int x, int y, int w, int h) {
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

/* -------------------------------------------------- */
/* Line — Bresenham's algorithm                       */
/* -------------------------------------------------- */

void wl_draw_line(wl_canvas_t *canvas, int x0, int y0, int x1, int y1, uint32_t color) {
    int dx  = x1 - x0 >= 0 ? x1 - x0 : x0 - x1;
    int dy  = y1 - y0 >= 0 ? y0 - y1 : y1 - y0;  /* negative magnitude, standard form */
    int sx  = x0 < x1 ? 1 : -1;
    int sy  = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (1) {
        wl_draw_pixel(canvas, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* -------------------------------------------------- */
/* Circle — midpoint circle algorithm                 */
/* -------------------------------------------------- */

/* plot all 8 symmetric points around the circle center */
static void plot_circle_points(wl_canvas_t *canvas, int cx, int cy, int x, int y, uint32_t color) {
    wl_draw_pixel(canvas, cx + x, cy + y, color);
    wl_draw_pixel(canvas, cx - x, cy + y, color);
    wl_draw_pixel(canvas, cx + x, cy - y, color);
    wl_draw_pixel(canvas, cx - x, cy - y, color);
    wl_draw_pixel(canvas, cx + y, cy + x, color);
    wl_draw_pixel(canvas, cx - y, cy + x, color);
    wl_draw_pixel(canvas, cx + y, cy - x, color);
    wl_draw_pixel(canvas, cx - y, cy - x, color);
}

void wl_draw_circle(wl_canvas_t *canvas, int cx, int cy, int radius, uint32_t color) {
    int x = radius;
    int y = 0;
    int err = 1 - radius;

    while (x >= y) {
        plot_circle_points(canvas, cx, cy, x, y, color);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

void wl_draw_circle_filled(wl_canvas_t *canvas, int cx, int cy, int radius, uint32_t color) {
    int x = radius;
    int y = 0;
    int err = 1 - radius;

    while (x >= y) {
        /* draw horizontal spans connecting symmetric points instead of
           individual pixels — fills the circle row by row */
        wl_draw_rect(canvas, cx - x, cy + y, x * 2 + 1, 1, color);
        wl_draw_rect(canvas, cx - x, cy - y, x * 2 + 1, 1, color);
        wl_draw_rect(canvas, cx - y, cy + x, y * 2 + 1, 1, color);
        wl_draw_rect(canvas, cx - y, cy - x, y * 2 + 1, 1, color);

        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}
