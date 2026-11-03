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
UNIT_TESTS = $(patsubst tests/%.c,%,$(wildcard tests/test-*.c))
UNIT_BINS = $(addprefix $(OUTDIR)/,$(UNIT_TESTS))
include rules.mk
.PHONY: all tests test stack-tests gate site-gate clean install help bundled-deps
all: $(CLI) $(LIB_SHARED) $(OUTDIR)/libresite-1.0.pc
$(LIB_STATIC): $(LIB_OBJECTS)
	$(AR) rcs $@ $^
$(LIB_SHARED): $(LIB_OBJECTS) $(DEP_ARCHIVES)
	$(CC) -shared -Wl,-soname,liblibresite-1.0.so.0 -o $@ $(LIB_OBJECTS) $(LDFLAGS)
	ln -sf liblibresite-1.0.so.0 $(OUTDIR)/liblibresite-1.0.so
$(CLI): $(OBJDIR)/cli/main.o $(LIB_STATIC) $(DEP_ARCHIVES)
	$(CC) -o $@ $(OBJDIR)/cli/main.o $(LIB_STATIC) $(LDFLAGS)
# Every tests/test-*.c is one GTest binary linked against the static library.
$(OUTDIR)/test-%: tests/test-%.c $(LIB_STATIC) $(DEP_ARCHIVES) $(OUTDIR)/.flags
	$(CC) $(CFLAGS) -MMD -MP -MF $@.d -o $@ $< $(LIB_STATIC) $(LDFLAGS)
tests test: all $(UNIT_BINS)
	@set -e; for t in $(UNIT_BINS); do echo "G_DEBUG=fatal-warnings $$t"; G_DEBUG=fatal-warnings $$t; done
	bash tests/packages.sh $(CLI)
	bash tests/cli.sh $(CLI)
	bash tests/revision.sh $(CLI)
	bash tests/edit.sh $(CLI)
	bash tests/usage.sh $(CLI)
	bash tests/scaffold.sh $(CLI)
	bash tests/qa.sh $(CLI)
# The browser gate (docs/components.org, "Variety gate"): contrast, reduced
# motion and page weight at 390 and 1440 (tests/gate.sh), every option at
# 390, 768 and 1440 (tests/options.sh), and the mobile findings on photo
# sites and every option at 390 and 1440 (tests/mobile.sh).
GATE_OUT = $(OUTDIR)/gate
gate: all $(OUTDIR)/test-gate $(OUTDIR)/test-options $(OUTDIR)/test-mobile $(OUTDIR)/test-showcase $(OUTDIR)/test-bar
	@test -n "$(WEBDRIVER_URL)" || { echo 'make gate needs WEBDRIVER_URL, such as http://localhost:4444 from geckodriver' >&2; exit 2; }
	rm -rf $(GATE_OUT)
	GATE_OUTPUT=$(CURDIR)/$(GATE_OUT)/default G_DEBUG=fatal-warnings $(OUTDIR)/test-gate
	bash tests/gate.sh "$(WEBDRIVER_URL)" $(GATE_OUT)/default
	OPTIONS_OUTPUT=$(CURDIR)/$(GATE_OUT)/options G_DEBUG=fatal-warnings $(OUTDIR)/test-options
	bash tests/options.sh "$(WEBDRIVER_URL)" $(GATE_OUT)/options
	MOBILE_OUTPUT=$(CURDIR)/$(GATE_OUT)/mobile G_DEBUG=fatal-warnings $(OUTDIR)/test-mobile
	bash tests/mobile.sh "$(WEBDRIVER_URL)" $(GATE_OUT)/mobile $(GATE_OUT)/options $(GATE_OUT)/default
	SHOWCASE_OUTPUT=$(CURDIR)/$(GATE_OUT)/showcase G_DEBUG=fatal-warnings $(OUTDIR)/test-showcase
	bash tests/showcase.sh "$(WEBDRIVER_URL)" $(GATE_OUT)/showcase
	BAR_OUTPUT=$(CURDIR)/$(GATE_OUT)/bar G_DEBUG=fatal-warnings $(OUTDIR)/test-bar
	python3 tests/bar.py $(GATE_OUT)/bar --prepare --webdriver "$(WEBDRIVER_URL)"
	bash tests/options.sh "$(WEBDRIVER_URL)" $(GATE_OUT)/bar
	# The bar sites carry every component of the composed fixture (forms,
	# filters, a journal) plus the visual sequence: 180 KB raw for them,
	# the gzip limit unchanged.
	GATE_WEIGHT_LIMIT=184320 bash tests/gate.sh "$(WEBDRIVER_URL)" $(GATE_OUT)/bar
	# The taste gate catches each gimmick on a page built to have them all.
	mkdir -p $(GATE_OUT)/taste-bad && cp -r tests/fixtures/taste-bad/. $(GATE_OUT)/taste-bad/
	! bash tests/gate.sh "$(WEBDRIVER_URL)" $(GATE_OUT)/taste-bad >/dev/null
	jq -e '[.pages[] | (.rotated | length) > 0 and (.blobs | length) > 0 and (.loud | length) > 0 and (.stroked | length) > 0] | all' $(GATE_OUT)/taste-bad/gate.json >/dev/null
# The site gate's own checks (docs/site-gate.org): a good fixture site
# passes and each defect page fails only its check. Needs firefox and
# geckodriver, or WEBDRIVER_URL.
site-gate:
	bash tests/site-gate.sh
# Live visual acceptance is separate from the hermetic fixture gate.
.PHONY: visual-review
visual-review:
	@test -n "$(VISUAL_REVIEW_DIR)" || { echo 'visual-review needs VISUAL_REVIEW_DIR containing six real runs and reviews' >&2; exit 2; }
	python3 tests/bar.py "$(VISUAL_REVIEW_DIR)" --review
# Optional probes compile against pinned generation libraries without making
# database/client development packages mandatory for the builder itself.
stack-tests:
	bash tests/stack-api.sh
$(OUTDIR)/libresite-1.0.pc: config.mk $(OUTDIR)/.flags | $(OUTDIR)
	printf '%s\n' 'prefix=$(PREFIX)' 'libdir=$(LIBDIR)' 'includedir=$(INCLUDEDIR)' 'Name: libresite' 'Description: Recipe-driven GLib AI site builder' 'Version: $(VERSION)' 'Requires: gio-2.0 gobject-2.0 json-glib-1.0' 'Requires.private: ai-glib-1.0 yaml-glib-1.0 yaml-0.1 vips' 'Libs: -L$${libdir} -llibresite-1.0' 'Cflags: -I$${includedir}/libresite-1.0' > $@
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
	@printf '%s\n' 'make [DEBUG=1] [ASAN=1] [UBSAN=1] [DEPS=bundled]' 'make tests | stack-tests | gate WEBDRIVER_URL=... | site-gate | install DESTDIR=... PREFIX=... | clean'
endif
