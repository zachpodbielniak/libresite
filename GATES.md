# Gates: recipe SKUs

Scope: Ship six fixed-scope vertical packages, retain the C application SKU, raise default quality, and preserve sold inputs across catalog upgrades.

- [ ] G1: Six vertical recipes enforce their route and handoff contracts; the application stays c-glib with htmx/orm.
  CHECK: bash tests/packages.sh build/release/libresite
  EXPECT: PACKAGES VERIFIED
  EVIDENCE: pending

- [ ] G2: Sealed recipes and catalogs round-trip without fingerprint drift and reject tampering; old packages resume after catalog changes.
  CHECK: build/release/test-packages
  EXPECT: End of package tests
  EVIDENCE: pending

- [ ] G3: Release and debug builds and regression tests pass without warnings; shell checks pass.
  CHECK: bash tests/verify.sh
  EXPECT: LIBRESITE VERIFIED
  EVIDENCE: pending

- [ ] G4: Installed CLI, public API and plugins remain usable outside the checkout.
  CHECK: bash tests/install.sh
  EXPECT: INSTALL VERIFIED
  EVIDENCE: pending

- [x] G5: The shared catalog sets flagship expectations for every recipe; SKU scope and pinning limits are documented.
  EVIDENCE: Reviewed data/prompts.yaml shared defaults and docs/packages.org scope, exclusions, native-check limits and archived-generator requirements.

- [ ] G6: Conventional Commits are merged to master with a clean tracked worktree.
  EVIDENCE: pending

- [ ] G7: Package ownership and recovery paths pass ASan and UBSan regression tests.
  CHECK: make DEBUG=1 ASAN=1 UBSAN=1 tests
  EXPECT: REVISION VERIFIED
  EVIDENCE: pending
