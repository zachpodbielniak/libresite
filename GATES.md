# Gates: native static scaffold

OWNS: src/** data/** recipes/** tests/** docs/** skills/** README.org Makefile rules.mk AGENTS.md GATES.md

Scope: Add a deterministic scaffold module, integrate static recipes and previews, verify preservation and offline execution, and merge Conventional Commits into master.

- [x] G1: Release/debug regression checks and scaffold behavior pass without compiler warnings.
  CHECK: bash tests/verify.sh
  EXPECT: LIBRESITE VERIFIED
  EVIDENCE: exit=0; shell=/bin/sh; cwd=/var/home/zach/source/projects/libresite/trees/feat-native-scaffold; path=ae84c92d8d99/14 entries; EXPECT=matched; output-sha256=fc4b5ff5db53a0fb3a396d0c9fd726b735fc1ac20a0e9dda79ae2f57d102d551; output-bytes=19

- [x] G2: Installed native scaffold works outside the checkout alongside public API and plugin tests.
  CHECK: bash tests/install.sh
  EXPECT: INSTALL VERIFIED
  EVIDENCE: exit=0; shell=/bin/sh; cwd=/var/home/zach/source/projects/libresite/trees/feat-native-scaffold; path=ae84c92d8d99/14 entries; EXPECT=matched; output-sha256=962a7b8536fddef5fd27f1283102c6f00cfbac170d4798956db5155368fad8c5; output-bytes=2817

- [x] G3: Scaffold ownership and cancellation pass sanitizer checks.
  CHECK: make DEBUG=1 ASAN=1 UBSAN=1 tests
  EXPECT: CLI VERIFIED
  EVIDENCE: exit=0; shell=/bin/sh; cwd=/var/home/zach/source/projects/libresite/trees/feat-native-scaffold; path=ae84c92d8d99/14 entries; EXPECT=matched; output-sha256=673eddcdbed5bc9560271ecb3527b1f5a6bba81964c5870111ab15f7e3b7cc4d; output-bytes=87933

- [x] G4: Documentation, templates and final Conventional Commit merge match the requested scope.
  EVIDENCE: Reviewed native template emission, unchanged seed paths, closed options, cancellation and atomic conflicts, static recipe acceptance, installed previews and template-bound checkpoint identity against docs/scaffold.org. Release/debug verification, installed execution and ASan/UBSan passed; generated Makefile checks included a missing-file negative control. Feature commit 2148793 uses a Conventional Commit subject and master was fast-forwarded to it from 8017a93. No live model, browser, contact delivery or legal review was performed.
