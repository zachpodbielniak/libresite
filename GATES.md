# Gates: recipe SKUs

Scope: Ship six fixed-scope vertical packages, retain the C application SKU, raise default quality, and preserve sold inputs across catalog upgrades.

- [x] G1: Six vertical recipes enforce their route and handoff contracts; the application stays c-glib with htmx/orm.
  CHECK: bash tests/packages.sh build/release/libresite
  EXPECT: PACKAGES VERIFIED
  EVIDENCE: exit=0; shell=/bin/sh; cwd=/var/home/zach/source/projects/libresite/trees/recipe-skus; path=16a50ccf0eab/14 entries; EXPECT=matched; output-sha256=f74f0f1a395e5b75d82befe77f43f171213e2150ba8509020bbe0cbaec850f4b; output-bytes=1154

- [x] G2: Sealed recipes, catalogs and native templates round-trip without fingerprint drift and reject tampering; old packages resume after default changes.
  CHECK: build/release/test-packages
  EXPECT: End of package tests
  EVIDENCE: exit=0; shell=/bin/sh; cwd=/var/home/zach/source/projects/libresite/trees/recipe-skus; path=16a50ccf0eab/14 entries; EXPECT=matched; output-sha256=a75f34d8cce5d9632c6a54b7584690584d45a3429703f7fd6a8909b17614038a; output-bytes=681

- [x] G3: Release and debug builds and regression tests pass without warnings; shell checks pass.
  CHECK: bash tests/verify.sh
  EXPECT: LIBRESITE VERIFIED
  EVIDENCE: exit=0; shell=/bin/sh; cwd=/var/home/zach/source/projects/libresite/trees/recipe-skus; path=16a50ccf0eab/14 entries; EXPECT=matched; output-sha256=fc4b5ff5db53a0fb3a396d0c9fd726b735fc1ac20a0e9dda79ae2f57d102d551; output-bytes=19

- [x] G4: Installed CLI, public API and plugins remain usable outside the checkout.
  CHECK: bash tests/install.sh
  EXPECT: INSTALL VERIFIED
  EVIDENCE: exit=0; shell=/bin/sh; cwd=/var/home/zach/source/projects/libresite/trees/recipe-skus; path=16a50ccf0eab/14 entries; EXPECT=matched; output-sha256=634b26bb966be43eb50074aaee5fc9e330d12f2b5745102ba59f5fe84c5e4ba7; output-bytes=4064

- [x] G5: The shared catalog sets flagship expectations for every recipe; SKU scope and pinning limits are documented.
  EVIDENCE: Reviewed data/prompts.yaml shared defaults and docs/packages.org scope, exclusions, native-check limits and archived-generator requirements.

- [ ] G6: Conventional Commits are merged to master with a clean tracked worktree.
  EVIDENCE: pending

- [x] G7: Package ownership and recovery paths pass ASan and UBSan regression tests.
  CHECK: make DEBUG=1 ASAN=1 UBSAN=1 tests
  EXPECT: REVISION VERIFIED
  EVIDENCE: exit=0; shell=/bin/sh; cwd=/var/home/zach/source/projects/libresite/trees/recipe-skus; path=16a50ccf0eab/14 entries; EXPECT=matched; output-sha256=9bc5db01dcf1df242254c426c65e8803eac3c676f074df67f187012898613327; output-bytes=92560
