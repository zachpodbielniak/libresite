# Gates: robustness review

Scope: Review the builder, repair input and extension boundaries, add deterministic artifact acceptance checks, document behavior and commit verified changes in the dedicated worktree.

- [x] G1: Release/debug builds, regression tests and shell lint pass without warnings.
  CHECK: DEPS=system bash tests/verify.sh
  EXPECT: LIBRESITE VERIFIED
  EVIDENCE: exit=0; shell=/bin/sh; cwd=/var/home/zach/source/projects/libresite/trees/robustness-review; path=0a95055f4dc5/14 entries; EXPECT=matched; output-sha256=fc4b5ff5db53a0fb3a396d0c9fd726b735fc1ac20a0e9dda79ae2f57d102d551; output-bytes=19

- [x] G2: Installed CLI and external library/plugin consumers pass.
  CHECK: DEPS=system bash tests/install.sh
  EXPECT: INSTALL VERIFIED
  EVIDENCE: exit=0; shell=/bin/sh; cwd=/var/home/zach/source/projects/libresite/trees/robustness-review; path=0a95055f4dc5/14 entries; EXPECT=matched; output-sha256=44d084d65a6e67700a95996647efa503ece21f55935e3bfb0a3b3e756c0fbe74; output-bytes=328

- [x] G3: Ownership and callback regressions pass with address and undefined behavior sanitizers.
  CHECK: make DEPS=system DEBUG=1 ASAN=1 UBSAN=1 tests
  EXPECT: CLI VERIFIED
  EVIDENCE: exit=0; shell=/bin/sh; cwd=/var/home/zach/source/projects/libresite/trees/robustness-review; path=0a95055f4dc5/14 entries; EXPECT=matched; output-sha256=4ae23ef12bdf57e180ffc5b9f65deaf4b90c0b2f97b2ad362ca716f858fd2b71; output-bytes=17441

- [x] G4: Review findings, contract documentation and Conventional Commits match the final implementation.
  EVIDENCE: Reviewed final diff in commit 0854a02 against docs/robustness-review.org and the original request. All source modules, public contracts, build/install scripts and agent workflows are covered by the review. Final release/debug logs each contain 105 passing GTests; G1-G3 were re-executed successfully after the last runtime change. Existing catalog/recipe bytes and dependency pins are unchanged. Conventional Commit exists on feat/robustness-review in trees/robustness-review; no generated application or live-quality claim was added.

Integration verification: merged onto master 993a45c in trees/merge-robustness. DEPS=system bash tests/verify.sh and DEPS=system bash tests/install.sh both exited zero with their success markers; release/debug each reported 105 passing GTests. Retry fixtures now assert Git HEAD and clean their private Git-containing output trees without following symlinks. The sanitizer evidence above is from the original review branch; the merge integration re-ran release/debug, ShellCheck and install checks.

# Gates: C application generation

Scope: reusable C stack prompts with pinned GitLab dependencies, YAML configuration, HTMX pages, SQLite by default and optional database bootstrap SQL.

- [x] C1: profile selection, API context and application artifact contracts pass regression tests
  CHECK: make tests
  EXPECT: CLI VERIFIED
  EVIDENCE: exit=0; shell=/bin/sh; cwd=/var/home/zach/source/projects/libresite/trees/c-application-stack; path=c1e10aa6edd2/14 entries; EXPECT=matched; output-sha256=32e322f7e72a5df41258edce0104eb32ca022692c61e340e04956cdd76b4f298; output-bytes=16132

- [x] C2: release/debug builds and shell checks pass without warnings
  CHECK: bash tests/verify.sh
  EXPECT: LIBRESITE VERIFIED
  EVIDENCE: exit=0; shell=/bin/sh; cwd=/var/home/zach/source/projects/libresite/trees/c-application-stack; path=c1e10aa6edd2/14 entries; EXPECT=matched; output-sha256=fc4b5ff5db53a0fb3a396d0c9fd726b735fc1ac20a0e9dda79ae2f57d102d551; output-bytes=19

- [x] C3: installed catalogs render C stack context outside the checkout and public consumers work
  CHECK: bash tests/install.sh
  EXPECT: INSTALL VERIFIED
  EVIDENCE: exit=0; shell=/bin/sh; cwd=/var/home/zach/source/projects/libresite/trees/c-application-stack; path=c1e10aa6edd2/14 entries; EXPECT=matched; output-sha256=80b42a6138c77df1699793c1e002926f96d5c81f8c517bd75ac4640f2192bcb0; output-bytes=315

- [x] C4: documentation and API contract agree with pinned dependency headers and build flags
  EVIDENCE: Reviewed pinned yaml-glib parser/node/mapping headers and parser implementation, htmx-glib server/router/response/builder headers, orm-glib engine/connection/result/transaction/value headers and all three build configurations; real-library probes confirmed ORM 0.2 naming, YAML scalar style handling, routing ownership and SQLite persistence. docs/c-application.org and the agent skill describe the same contract and distinguish generation guidance from executed checks.

- [x] C5: pinned YAML typing, HTMX routing/escaping and ORM SQLite persistence probes pass
  CHECK: make stack-tests
  EXPECT: STACK API VERIFIED
  EVIDENCE: exit=0; shell=/bin/sh; cwd=/var/home/zach/source/projects/libresite/trees/c-application-stack; path=c1e10aa6edd2/14 entries; EXPECT=matched; output-sha256=1ee3b1201047f1ae14f25b563002049cf4dc3ec5fcf3694380b720c1bf4db994; output-bytes=243

C application integration verification: merged feat/c-application-stack (47e7df4) onto master adc7b74 in trees/merge-c-application, preserving responsiveness, strict validation and Git publication changes. make DEPS=system tests, DEPS=system bash tests/verify.sh, DEPS=system bash tests/install.sh and make DEPS=system stack-tests all exited zero. Release/debug each passed 108 GTests with no warning/error diagnostics; all three pinned-library probes passed. Historical branch evidence above retains its original scope; no live generated application/browser or external database was tested during integration.
