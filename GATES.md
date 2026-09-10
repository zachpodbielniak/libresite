# Gates: step usage and job pricing

OWNS: src/** tests/** docs/** README.org Makefile GATES.md

Scope: Persist structured usage for each executed step, price records using explicit rates, and merge verified Conventional Commits into master.

- [x] G1: Offline usage, pricing, resume and regression checks pass in release and debug with zero warnings.
  CHECK: bash tests/verify.sh
  EXPECT: LIBRESITE VERIFIED
  EVIDENCE: exit=0; shell=/bin/sh; cwd=/var/home/zach/source/projects/libresite/trees/feat-step-usage; path=8c5cae66fb0a/14 entries; EXPECT=matched; output-sha256=fc4b5ff5db53a0fb3a396d0c9fd726b735fc1ac20a0e9dda79ae2f57d102d551; output-bytes=19

- [x] G2: Installed public API and CLI work outside the checkout.
  CHECK: bash tests/install.sh
  EXPECT: INSTALL VERIFIED
  EVIDENCE: exit=0; shell=/bin/sh; cwd=/var/home/zach/source/projects/libresite/trees/feat-step-usage; path=8c5cae66fb0a/14 entries; EXPECT=matched; output-sha256=6b6fd0ef6574f04c3a4e81c87b5ff65f75d1e163acd5c4a1bcafb9eaca0c9347; output-bytes=1817

- [x] G3: Usage ownership and failure paths pass sanitizers.
  CHECK: make DEBUG=1 ASAN=1 UBSAN=1 tests
  EXPECT: CLI VERIFIED
  EVIDENCE: exit=0; shell=/bin/sh; cwd=/var/home/zach/source/projects/libresite/trees/feat-step-usage; path=8c5cae66fb0a/14 entries; EXPECT=matched; output-sha256=0a4f3cb0c0cc8782724ddcfd409c0831db6d5052fc52657bd59d85c979f85eaa; output-bytes=87020

- [ ] G4: Documented accounting semantics match code and verified Conventional Commits are merged into master.
  EVIDENCE: pending
