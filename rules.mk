# SPDX-License-Identifier: AGPL-3.0-or-later
.PHONY: FORCE
FORCE:
$(OUTDIR):
	mkdir -p $@
$(OUTDIR)/.flags: FORCE | $(OUTDIR)
	@printf '%s\n' '$(CC) $(CFLAGS) $(LDFLAGS) $(PREFIX) $(LIBDIR) $(INCLUDEDIR)' > $@.tmp
	@cmp -s $@.tmp $@ && $(RM) $@.tmp || mv $@.tmp $@
$(OBJDIR)/%.o: src/%.c $(OUTDIR)/.flags | $(DEP_TARGET)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
$(OUTDIR)/resources.c: data/resources.xml data/prompts.yaml data/file-bundle.schema.json $(wildcard data/scaffold/*) LICENSE | $(OUTDIR)
	glib-compile-resources --sourcedir=data --sourcedir=. --generate-source --c-name=ls_resources --target=$@ $<
$(OBJDIR)/resources.o: $(OUTDIR)/resources.c $(OUTDIR)/.flags
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
-include $(LIB_OBJECTS:.o=.d) $(OBJDIR)/cli/main.d $(OUTDIR)/test-libresite.d $(OUTDIR)/test-contracts.d $(OUTDIR)/test-packages.d $(OUTDIR)/test-ls-ai-provider.d $(OUTDIR)/test-ls-prune-step.d
# Recursion is once per make invocation; changed upstream archives relink users.
$(DEP_ARCHIVES): bundled-deps
	@test -f $@
