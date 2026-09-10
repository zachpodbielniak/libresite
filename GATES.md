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
  EVIDENCE: exit=0; shell=/bin/sh; cwd=/var/home/zach/source/projects/libresite/trees/feat-step-usage; path=8c5cae66fb0a/14 entries; EXPECT=matched; output-sha256=d7354d577a1e65cc8ba82f072740490df0bc4381215d3620e6b1789c9f7ecace; output-bytes=2832

- [x] G3: Usage ownership and failure paths pass sanitizers.
  CHECK: make DEBUG=1 ASAN=1 UBSAN=1 tests
  EXPECT: CLI VERIFIED
  EVIDENCE: exit=0; shell=/bin/sh; cwd=/var/home/zach/source/projects/libresite/trees/feat-step-usage; path=8c5cae66fb0a/14 entries; EXPECT=matched; output-sha256=76422ec96d530537a3f06934a79e031b3d4f51880eb6adc79e7c6e63d2e6cbf4; output-bytes=88698

- [x] G4: Documented accounting semantics match code and verified Conventional Commits are merged into master.
  EVIDENCE: Reviewed transport-byte accounting, failed executions, retries, legacy and interrupted history, exact rate lookup, pricing arithmetic, and native scaffold integration against docs/usage.org. Gates G1-G3 passed on integration commit a719250 (feature e32d555 plus master f1ba7c0). Master was fast-forwarded to a719250; its make -j4 all completed with no warnings, its CLI exposes --price-usage and --rates, and git status and git diff --check were clean. Conventional Commits preserve both features. All four gates are met; no abandoned outcomes. Tests use offline fixtures, not live providers.
