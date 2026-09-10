# Gates: premium prompts and flagship recipe

OWNS: data/prompts.yaml, recipes/flagship.yaml, docs/**, README.org, tests/cli.sh, tests/install.sh, tests/fixtures/provider.sh, GATES.md

Scope: Actionable design and delivery contracts, a complete multi-page recipe, and documented, tested CLI preflight. Fixture tests do not establish live visual quality.

- [x] G1: Release/debug builds and all offline tests pass with zero compiler warnings, including every flagship generation stage preview.
  CHECK: DEPS=system bash tests/verify.sh
  EXPECT: LIBRESITE VERIFIED
  EVIDENCE: exit=0; shell=/bin/sh; cwd=/var/home/zach/source/projects/libresite/trees/premium-prompts; path=35c4b50b4f40/14 entries; EXPECT=matched; output-sha256=fc4b5ff5db53a0fb3a396d0c9fd726b735fc1ac20a0e9dda79ae2f57d102d551; output-bytes=19

- [x] G2: Installed CLI resolves every flagship stage outside the checkout and public plugin/consumer checks pass.
  CHECK: DEPS=system bash tests/install.sh
  EXPECT: INSTALL VERIFIED
  EVIDENCE: exit=0; shell=/bin/sh; cwd=/var/home/zach/source/projects/libresite/trees/premium-prompts; path=35c4b50b4f40/14 entries; EXPECT=matched; output-sha256=8be8e32735ecaa54b1c1490e9c8dca8bd5efddaeec8ac242280c2909545b5d0d; output-bytes=207

- [x] G3: Manual review confirms coherent routes, exact phase acceptance paths, usable local workflows, bounded project scope and honest documentation of runtime verification limits.
  EVIDENCE: Reviewed 2026-09-09 against the CLI plan: eight AI stages, six native checks, eight routes and twenty final required artifacts. Phase deliverables match following required paths. All routes have defined content and onward journeys; local export, reset and no-JavaScript behavior are specified. The review preflight measures 14425 bytes with empty context; the 55000-byte artifact target is explicitly advisory. README and docs distinguish source/native checks from unexecuted live generation and browser evaluation. System-dependency release/debug, all 68 GTests, five provider fixtures, ShellCheck and installed extension checks passed.
