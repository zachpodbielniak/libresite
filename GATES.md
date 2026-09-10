# Gates: revision mode

OWNS: src/** data/prompts.yaml recipes/revision.yaml tests/** docs/** README.org skills/** Makefile GATES.md

Scope: Import an existing text project, apply a change brief incrementally, publish a new output, and merge Conventional Commits into master.

- [ ] G1: Offline revision, failure, resume and regression tests pass without compiler warnings in release and debug builds.
  CHECK: bash tests/verify.sh
  EXPECT: LIBRESITE VERIFIED
  EVIDENCE: pending

- [x] G2: Installed CLI and public API work outside the checkout.
  CHECK: bash tests/install.sh
  EXPECT: INSTALL VERIFIED
  EVIDENCE: exit=0; shell=/bin/sh; cwd=/var/home/zach/source/projects/libresite/trees/feat-revision-mode; path=604a669fb015/14 entries; EXPECT=matched; output-sha256=feb83fa876439e5684023c7b2b1509500bfa54615453252b78b0b9e17753a4bc; output-bytes=1526

- [ ] G3: Ownership changes pass address and undefined behavior sanitizers.
  CHECK: make DEBUG=1 ASAN=1 UBSAN=1 tests
  EXPECT: CLI VERIFIED
  EVIDENCE: pending

- [ ] G4: Documentation matches the implementation and Conventional Commits are merged into master.
  EVIDENCE: pending
