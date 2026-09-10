# Gates: recipe SKUs

Scope: Ship six fixed-scope vertical packages, retain the C application SKU, raise default quality, and preserve sold inputs across catalog upgrades.

- [x] G1: Six vertical recipes enforce their route and handoff contracts; the application stays c-glib with htmx/orm.
  CHECK: bash tests/packages.sh build/release/libresite
  EXPECT: PACKAGES VERIFIED
  EVIDENCE: exit=0; shell=/bin/sh; cwd=/var/home/zach/source/projects/libresite/trees/recipe-skus; path=16a50ccf0eab/14 entries; EXPECT=matched; output-sha256=ffde0ed7bb92c6a569ade7d09ad8b3746196164edfa75344a5c9595c7192e67d; output-bytes=1154

- [x] G2: Sealed recipes and catalogs round-trip without fingerprint drift and reject tampering; old packages resume after catalog changes.
  CHECK: build/release/test-packages
  EXPECT: End of package tests
  EVIDENCE: exit=0; shell=/bin/sh; cwd=/var/home/zach/source/projects/libresite/trees/recipe-skus; path=16a50ccf0eab/14 entries; EXPECT=matched; output-sha256=2a927b4fabfe2e4ca042999691bbe24cd514f474e0d9a047fb9ac86e99470f22; output-bytes=650

- [x] G3: Release and debug builds and regression tests pass without warnings; shell checks pass.
  CHECK: bash tests/verify.sh
  EXPECT: LIBRESITE VERIFIED
  EVIDENCE: exit=0; shell=/bin/sh; cwd=/var/home/zach/source/projects/libresite/trees/recipe-skus; path=16a50ccf0eab/14 entries; EXPECT=matched; output-sha256=fc4b5ff5db53a0fb3a396d0c9fd726b735fc1ac20a0e9dda79ae2f57d102d551; output-bytes=19

- [x] G4: Installed CLI, public API and plugins remain usable outside the checkout.
  CHECK: bash tests/install.sh
  EXPECT: INSTALL VERIFIED
  EVIDENCE: exit=0; shell=/bin/sh; cwd=/var/home/zach/source/projects/libresite/trees/recipe-skus; path=16a50ccf0eab/14 entries; EXPECT=matched; output-sha256=d57d7a61350216a64a6dd511303fdb8ada62cdce6c124f795d7dd16eb9a37ffe; output-bytes=3034

- [x] G5: The shared catalog sets flagship expectations for every recipe; SKU scope and pinning limits are documented.
  EVIDENCE: Reviewed data/prompts.yaml shared defaults and docs/packages.org scope, exclusions, native-check limits and archived-generator requirements.

- [ ] G6: Conventional Commits are merged to master with a clean tracked worktree.
  EVIDENCE: pending

- [ ] G7: Package ownership and recovery paths pass ASan and UBSan regression tests.
  CHECK: make DEBUG=1 ASAN=1 UBSAN=1 tests
  EXPECT: REVISION VERIFIED
  EVIDENCE: pending
