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
  EVIDENCE: exit=0; shell=/bin/sh; cwd=/var/home/zach/source/projects/libresite/trees/feat-revision-mode; path=604a669fb015/14 entries; EXPECT=matched; output-sha256=248c490ad91d9b9ad242da08fa186cc25a39ccf59e2dea2fb9c1da30ad30e4e4; output-bytes=1789

- [x] G3: Ownership changes pass address and undefined behavior sanitizers.
  CHECK: make DEBUG=1 ASAN=1 UBSAN=1 tests
  EXPECT: CLI VERIFIED
  EVIDENCE: exit=0; shell=/bin/sh; cwd=/var/home/zach/source/projects/libresite/trees/feat-revision-mode; path=604a669fb015/14 entries; EXPECT=matched; output-sha256=4836d659f7e74c19722c75f0ceb4391e5217e4ce313a6a352a008de086693c9c; output-bytes=85960

- [ ] G4: Documentation matches the implementation and Conventional Commits are merged into master.
  EVIDENCE: pending
