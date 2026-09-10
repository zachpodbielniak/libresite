# SPDX-License-Identifier: AGPL-3.0-or-later
# Serialize mixed clean/build goals, including make -j clean tests.
ifneq ($(filter clean,$(MAKECMDGOALS)),)
ifneq ($(filter-out clean,$(MAKECMDGOALS)),)
.PHONY: $(MAKECMDGOALS) serialize
$(MAKECMDGOALS): serialize
	@:
serialize:
	$(MAKE) clean
	$(MAKE) $(filter-out clean,$(MAKECMDGOALS))
MIXED = 1
endif
endif
ifndef MIXED
include config.mk
.DEFAULT_GOAL := all
LIB_SOURCES = $(wildcard src/core/*.c src/interfaces/*.c src/model/*.c src/providers/*.c src/steps/*.c)
LIB_OBJECTS = $(patsubst src/%.c,$(OBJDIR)/%.o,$(LIB_SOURCES)) $(OBJDIR)/resources.o
PUBLIC_HEADERS = $(wildcard src/*.h src/core/*.h src/interfaces/*.h src/model/*.h src/providers/*.h src/steps/*.h)
include rules.mk
.PHONY: all tests test stack-tests clean install help bundled-deps
all: $(CLI) $(LIB_SHARED) $(OUTDIR)/libresite-1.0.pc
$(LIB_STATIC): $(LIB_OBJECTS)
	$(AR) rcs $@ $^
$(LIB_SHARED): $(LIB_OBJECTS) $(DEP_ARCHIVES)
	$(CC) -shared -Wl,-soname,liblibresite-1.0.so.0 -o $@ $(LIB_OBJECTS) $(LDFLAGS)
	ln -sf liblibresite-1.0.so.0 $(OUTDIR)/liblibresite-1.0.so
$(CLI): $(OBJDIR)/cli/main.o $(LIB_STATIC) $(DEP_ARCHIVES)
	$(CC) -o $@ $(OBJDIR)/cli/main.o $(LIB_STATIC) $(LDFLAGS)
$(OUTDIR)/test-libresite: tests/test-libresite.c $(LIB_STATIC) $(DEP_ARCHIVES) $(OUTDIR)/.flags
	$(CC) $(CFLAGS) -MMD -MP -MF $@.d -o $@ $< $(LIB_STATIC) $(LDFLAGS)
tests test: all $(OUTDIR)/test-libresite
	G_DEBUG=fatal-warnings $(OUTDIR)/test-libresite
	bash tests/cli.sh $(CLI)
# Optional probes compile against pinned generation libraries without making
# database/client development packages mandatory for the builder itself.
stack-tests:
	bash tests/stack-api.sh
$(OUTDIR)/libresite-1.0.pc: config.mk $(OUTDIR)/.flags | $(OUTDIR)
	printf '%s\n' 'prefix=$(PREFIX)' 'libdir=$(LIBDIR)' 'includedir=$(INCLUDEDIR)' 'Name: libresite' 'Description: Recipe-driven GLib AI site builder' 'Version: $(VERSION)' 'Requires: gio-2.0 gobject-2.0 json-glib-1.0' 'Requires.private: ai-glib-1.0 yaml-glib-1.0 yaml-0.1' 'Libs: -L$${libdir} -llibresite-1.0' 'Cflags: -I$${includedir}/libresite-1.0' > $@
install: all
	install -d $(DESTDIR)$(BINDIR) $(DESTDIR)$(LIBDIR)/pkgconfig $(DESTDIR)$(DATADIR)/libresite
	install -m755 $(CLI) $(DESTDIR)$(BINDIR)/
	install -m755 $(LIB_SHARED) $(DESTDIR)$(LIBDIR)/
	ln -sf liblibresite-1.0.so.0 $(DESTDIR)$(LIBDIR)/liblibresite-1.0.so
	install -m644 $(LIB_STATIC) $(DESTDIR)$(LIBDIR)/
	install -m644 $(OUTDIR)/libresite-1.0.pc $(DESTDIR)$(LIBDIR)/pkgconfig/
	@for h in $(PUBLIC_HEADERS); do install -D -m644 $$h $(DESTDIR)$(INCLUDEDIR)/libresite-1.0/$${h#src/}; done
	cp -R data recipes skills docs README.org LICENSE $(DESTDIR)$(DATADIR)/libresite/
# ai-glib records mcp-glib/yaml-glib as nested gitlinks. A non-recursive
# submodule checkout leaves those directories empty, and the inner make
# then fails with "No rule to make target 'lib'".
bundled-deps:
	@if [ ! -f deps/yaml-glib/Makefile ] || [ ! -f deps/ai-glib/deps/mcp-glib/Makefile ] || [ ! -f deps/ai-glib/deps/yaml-glib/Makefile ]; then \
		echo 'Initializing bundled git submodules...'; \
		git submodule update --init --recursive deps/ai-glib deps/yaml-glib; \
	fi
	$(MAKE) -C deps/yaml-glib DEBUG=0 GIR=0 build/release/libyaml-glib-1.0.a
	$(MAKE) -C deps/ai-glib DEBUG=0 GIR=0 static
clean:
	$(RM) -r $(OUTDIR)
help:
	@printf '%s\n' 'make [DEBUG=1] [ASAN=1] [UBSAN=1] [DEPS=bundled]' 'make tests | stack-tests | install DESTDIR=... PREFIX=... | clean'
endif
