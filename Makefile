CC      = cc
CFLAGS  = -Wall -Wextra -O2
CFLAGS_PIC = $(CFLAGS) -fPIC
LIBS    = -lwayland-client -lrt -lxkbcommon -lfreetype

BUILD   = build
LIB_A   = $(BUILD)/libwl.a
LIB_SO  = $(BUILD)/libwl.so

XDG_SHELL_XML = /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml
XDG_DECO_XML  = /usr/share/wayland-protocols/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml

# -------------------------------------------------- #
# Generated protocol files                           #
# -------------------------------------------------- #

$(BUILD)/xdg-shell-client-protocol.h: $(XDG_SHELL_XML) | $(BUILD)
	wayland-scanner client-header < $< > $@

$(BUILD)/xdg-shell-protocol.c: $(XDG_SHELL_XML) | $(BUILD)
	wayland-scanner private-code < $< > $@

$(BUILD)/xdg-decoration-client-protocol.h: $(XDG_DECO_XML) | $(BUILD)
	wayland-scanner client-header < $< > $@

$(BUILD)/xdg-decoration-protocol.c: $(XDG_DECO_XML) | $(BUILD)
	wayland-scanner private-code < $< > $@

$(BUILD)/xdg-shell-protocol.o: $(BUILD)/xdg-shell-protocol.c | $(BUILD)
	$(CC) -w -fPIC -c $< -o $@

$(BUILD)/xdg-decoration-protocol.o: $(BUILD)/xdg-decoration-protocol.c | $(BUILD)
	$(CC) -w -fPIC -c $< -o $@

PROTO_OBJS = $(BUILD)/xdg-shell-protocol.o $(BUILD)/xdg-decoration-protocol.o
PROTO_HEADERS = $(BUILD)/xdg-shell-client-protocol.h $(BUILD)/xdg-decoration-client-protocol.h

# -------------------------------------------------- #
# Library object files (PIC for both static+dynamic) #
# -------------------------------------------------- #

$(BUILD)/wayland.o: wayland.c wayland.h input.h $(PROTO_HEADERS) | $(BUILD)
	$(CC) $(CFLAGS_PIC) -I$(BUILD) -c wayland.c -o $@

$(BUILD)/input.o: input.c input.h wayland.h | $(BUILD)
	$(CC) $(CFLAGS_PIC) -I$(BUILD) -c input.c -o $@

$(BUILD)/font.o: font.c font.h wayland.h | $(BUILD)
	$(CC) $(CFLAGS_PIC) $(shell pkg-config --cflags freetype2) -c font.c -o $@

LIB_OBJS = $(BUILD)/wayland.o $(BUILD)/input.o $(BUILD)/font.o $(PROTO_OBJS)

# -------------------------------------------------- #
# Static library                                     #
# -------------------------------------------------- #

$(LIB_A): $(LIB_OBJS)
	ar rcs $@ $^

# -------------------------------------------------- #
# Dynamic library                                    #
# -------------------------------------------------- #

$(LIB_SO): $(LIB_OBJS)
	$(CC) -shared -o $@ $^ $(LIBS)

# -------------------------------------------------- #
# Example (links against static lib)                 #
# -------------------------------------------------- #

$(BUILD)/example.o: example.c wayland.h input.h font.h | $(BUILD)
	$(CC) $(CFLAGS) -c example.c -o $@

$(BUILD)/example: $(BUILD)/example.o $(LIB_A)
	$(CC) $(CFLAGS) -o $@ $< $(LIB_A) $(LIBS)

# -------------------------------------------------- #
# Targets                                            #
# -------------------------------------------------- #

$(BUILD):
	mkdir -p $(BUILD)

all: lib example

lib: $(LIB_A) $(LIB_SO)

example: $(BUILD)/example

clean:
	rm -rf $(BUILD)

.PHONY: all lib example clean
