# Gates: step usage and job pricing

OWNS: src/** tests/** docs/** README.org Makefile GATES.md

Scope: Persist structured usage for each executed step, price records using explicit rates, and merge verified Conventional Commits into master.

- [ ] G1: Offline usage, pricing, resume and regression checks pass in release and debug with zero warnings.
  CHECK: bash tests/verify.sh
  EXPECT: LIBRESITE VERIFIED
  EVIDENCE: pending

- [ ] G2: Installed public API and CLI work outside the checkout.
  CHECK: bash tests/install.sh
  EXPECT: INSTALL VERIFIED
  EVIDENCE: pending

- [ ] G3: Usage ownership and failure paths pass sanitizers.
  CHECK: make DEBUG=1 ASAN=1 UBSAN=1 tests
  EXPECT: CLI VERIFIED
  EVIDENCE: pending

- [ ] G4: Documented accounting semantics match code and verified Conventional Commits are merged into master.
  EVIDENCE: pending
