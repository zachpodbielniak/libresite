# Gates: revision mode

OWNS: src/** data/prompts.yaml recipes/revision.yaml tests/** docs/** README.org skills/** Makefile GATES.md

Scope: Import an existing text project, apply a change brief incrementally, publish a new output, and merge Conventional Commits into master.

- [x] G1: Offline revision, failure, resume and regression tests pass without compiler warnings in release and debug builds.
  CHECK: bash tests/verify.sh
  EXPECT: LIBRESITE VERIFIED
  EVIDENCE: exit=0; shell=/bin/sh; cwd=/var/home/zach/source/projects/libresite/trees/feat-revision-mode; path=604a669fb015/14 entries; EXPECT=matched; output-sha256=fc4b5ff5db53a0fb3a396d0c9fd726b735fc1ac20a0e9dda79ae2f57d102d551; output-bytes=19

- [x] G2: Installed CLI and public API work outside the checkout.
  CHECK: bash tests/install.sh
  EXPECT: INSTALL VERIFIED
  EVIDENCE: exit=0; shell=/bin/sh; cwd=/var/home/zach/source/projects/libresite/trees/feat-revision-mode; path=604a669fb015/14 entries; EXPECT=matched; output-sha256=d115bf745910eeb033780fd360d3a46275cfda5b0cbd76acf663d36ec38f5988; output-bytes=1802

- [x] G3: Ownership changes pass address and undefined behavior sanitizers.
  CHECK: make DEBUG=1 ASAN=1 UBSAN=1 tests
  EXPECT: CLI VERIFIED
  EVIDENCE: exit=0; shell=/bin/sh; cwd=/var/home/zach/source/projects/libresite/trees/feat-revision-mode; path=604a669fb015/14 entries; EXPECT=matched; output-sha256=8f8709b389d397c657265531cd67c360a917264c5371d2cae99668dc92ef9ae3; output-bytes=86080

- [ ] G4: Documentation matches the implementation and Conventional Commits are merged into master.
  EVIDENCE: pending
