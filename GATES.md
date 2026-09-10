# Gates: C application generation

Scope: reusable C stack prompts with pinned GitLab dependencies, YAML configuration, HTMX pages, SQLite by default and optional database bootstrap SQL.

- [x] G1: profile selection, API context and application artifact contracts pass regression tests
  CHECK: make tests
  EXPECT: CLI VERIFIED
  EVIDENCE: exit=0; shell=/bin/sh; cwd=/var/home/zach/source/projects/libresite/trees/c-application-stack; path=c1e10aa6edd2/14 entries; EXPECT=matched; output-sha256=32e322f7e72a5df41258edce0104eb32ca022692c61e340e04956cdd76b4f298; output-bytes=16132

- [x] G2: release/debug builds and shell checks pass without warnings
  CHECK: bash tests/verify.sh
  EXPECT: LIBRESITE VERIFIED
  EVIDENCE: exit=0; shell=/bin/sh; cwd=/var/home/zach/source/projects/libresite/trees/c-application-stack; path=c1e10aa6edd2/14 entries; EXPECT=matched; output-sha256=fc4b5ff5db53a0fb3a396d0c9fd726b735fc1ac20a0e9dda79ae2f57d102d551; output-bytes=19

- [x] G3: installed catalogs render C stack context outside the checkout and public consumers work
  CHECK: bash tests/install.sh
  EXPECT: INSTALL VERIFIED
  EVIDENCE: exit=0; shell=/bin/sh; cwd=/var/home/zach/source/projects/libresite/trees/c-application-stack; path=c1e10aa6edd2/14 entries; EXPECT=matched; output-sha256=80b42a6138c77df1699793c1e002926f96d5c81f8c517bd75ac4640f2192bcb0; output-bytes=315

- [x] G4: documentation and API contract agree with pinned dependency headers and build flags
  EVIDENCE: Reviewed pinned yaml-glib parser/node/mapping headers and parser implementation, htmx-glib server/router/response/builder headers, orm-glib engine/connection/result/transaction/value headers and all three build configurations; real-library probes confirmed ORM 0.2 naming, YAML scalar style handling, routing ownership and SQLite persistence. docs/c-application.org and the agent skill describe the same contract and distinguish generation guidance from executed checks.

- [x] G5: pinned YAML typing, HTMX routing/escaping and ORM SQLite persistence probes pass
  CHECK: make stack-tests
  EXPECT: STACK API VERIFIED
  EVIDENCE: exit=0; shell=/bin/sh; cwd=/var/home/zach/source/projects/libresite/trees/c-application-stack; path=c1e10aa6edd2/14 entries; EXPECT=matched; output-sha256=1ee3b1201047f1ae14f25b563002049cf4dc3ec5fcf3694380b720c1bf4db994; output-bytes=243
