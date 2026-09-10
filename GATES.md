# Gates: engine limits for client work

Scope: Lift text-only/SVG-only artifacts, add rename, focus stage context, stop between design and implementation, and add a published-tree QA runner without changing in-process validate.

- [x] G1: Binary artifacts round-trip as base64; PNG import/export, rename, focused prompts, gates and QA GTests pass.
  CHECK: make DEPS=system DEBUG=1 tests
  EXPECT: 115 GTests plus packages/cli/revision/usage/scaffold/qa scripts
  EVIDENCE: exit=0; LIBRESITE debug tests including /artifact/binary, /artifact/rename, /prompt/focus, /pipeline/gate, /qa/structural; QA VERIFIED

- [x] G2: Native gate stops until `--approve`; `--qa` inspects a published tree.
  CHECK: bash tests/qa.sh build/debug/libresite
  EXPECT: QA VERIFIED
  EVIDENCE: exit=0; gate rejected without --approve, resume with --approve published, --qa OK structural

- [x] G3: Revision accepts binary sources; bundled recipes validate with design-gate.
  CHECK: bash tests/revision.sh build/debug/libresite && bash tests/packages.sh build/debug/libresite
  EXPECT: REVISION VERIFIED and PACKAGES VERIFIED
  EVIDENCE: with-png publication; PACKAGES VERIFIED for all SKUs

- [x] G4: Release and debug regression tests pass without warnings.
  CHECK: bash tests/verify.sh
  EXPECT: LIBRESITE VERIFIED
  EVIDENCE: exit=0; LIBRESITE VERIFIED

- [x] G5: Installed CLI, public API and plugins remain usable outside the checkout.
  CHECK: bash tests/install.sh
  EXPECT: INSTALL VERIFIED
  EVIDENCE: exit=0; INSTALL VERIFIED including tests/qa.sh on the staged binary

- [x] G6: Address and undefined-behavior sanitizers pass on ownership changes.
  CHECK: make DEPS=system DEBUG=1 ASAN=1 UBSAN=1 tests
  EXPECT: tests pass
  EVIDENCE: exit=0; 115 GTests plus packages/cli/revision/usage/scaffold/qa under ASan/UBSan

- [x] G7: Conventional Commits are merged to master with a clean tracked worktree.
  EVIDENCE: master fast-forwarded to 0cc906d; feat/engine-limits worktree remains at that commit. verify.sh, install.sh and ASan/UBSan tests passed before the merge.
