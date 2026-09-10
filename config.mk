# SPDX-License-Identifier: AGPL-3.0-or-later
# Configuration follows ai-glib/clawtilla's three-file build layout.
PROJECT_NAME = libresite
VERSION = 0.1.0
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
LIBDIR ?= $(PREFIX)/lib
INCLUDEDIR ?= $(PREFIX)/include
DATADIR ?= $(PREFIX)/share
CC = gcc
AR = ar
DEBUG ?= 0
ASAN ?= 0
UBSAN ?= 0
DEPS ?= bundled
PKG_DEPS = glib-2.0 gobject-2.0 gio-2.0 gio-unix-2.0 gmodule-2.0 'json-glib-1.0 >= 1.10' libsoup-3.0 yaml-0.1
ifeq ($(DEPS),bundled)
# Bundled static archives avoid build-tree runtime search paths. ai-glib's
# archive contains its own objects, so its mcp-glib archive is linked explicitly.
PKG_DEPS += libdex-1 libxml-2.0
DEP_CFLAGS = -Ideps/ai-glib/src -Ideps/ai-glib/build/release -Ideps/yaml-glib/src -Ideps/yaml-glib/build/release
DEP_ARCHIVES = deps/ai-glib/build/release/libai-glib-1.0.a deps/yaml-glib/build/release/libyaml-glib-1.0.a deps/ai-glib/deps/mcp-glib/build/libmcp-glib-1.0.a
DEP_LIBS = -Wl,--start-group $(DEP_ARCHIVES) -Wl,--end-group -lm
DEP_TARGET = bundled-deps
else ifeq ($(DEPS),system)
PKG_DEPS += ai-glib-1.0 yaml-glib-1.0
else
$(error DEPS must be bundled or system)
endif
PKG_CFLAGS := $(shell pkg-config --cflags $(PKG_DEPS))
PKG_LIBS := $(shell pkg-config --libs $(PKG_DEPS))
BUILD_TYPE = release
CFLAGS_OPT = -O2
ifeq ($(DEBUG),1)
BUILD_TYPE = debug
CFLAGS_OPT = -O0 -g3
endif
OUTDIR = build/$(BUILD_TYPE)
OBJDIR = $(OUTDIR)/obj
SRCDIR = src
CFLAGS = -std=gnu89 -Wall -Wextra -Werror -Wformat=2 -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wdeclaration-after-statement -fPIC $(CFLAGS_OPT) $(PKG_CFLAGS) $(DEP_CFLAGS) -Isrc -I$(OUTDIR)
LDFLAGS = $(DEP_LIBS) $(PKG_LIBS)
ifeq ($(ASAN),1)
CFLAGS += -fsanitize=address -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address
endif
ifeq ($(UBSAN),1)
CFLAGS += -fsanitize=undefined -fno-omit-frame-pointer
LDFLAGS += -fsanitize=undefined
endif
LIB_STATIC = $(OUTDIR)/liblibresite-1.0.a
LIB_SHARED = $(OUTDIR)/liblibresite-1.0.so.0
CLI = $(OUTDIR)/libresite
