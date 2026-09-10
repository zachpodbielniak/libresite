# Gates: libresite bootstrap

OWNS: src/**, tests/**, data/**, recipes/**, docs/**, skills/**, Makefile, config.mk, rules.mk, README.org, AGENTS.md

Scope: A GNU89 GLib site builder with extensible recipe steps, five CLI providers, centralized design prompts, resumable generation, tests and documentation.

- [x] G1: Release and debug builds and edge-case/end-to-end tests pass without compiler warnings.
  CHECK: bash tests/verify.sh
  EXPECT: LIBRESITE VERIFIED
  EVIDENCE: exit=0; shell=/bin/sh; cwd=/var/home/zach/source/projects/libresite/trees/bootstrap; path=9914968ff619/14 entries; EXPECT=matched; output-sha256=fc4b5ff5db53a0fb3a396d0c9fd726b735fc1ac20a0e9dda79ae2f57d102d551; output-bytes=19

- [x] G2: Shipped recipes validate and installed binaries find embedded prompts outside the source tree.
  CHECK: bash tests/install.sh
  EXPECT: INSTALL VERIFIED
  EVIDENCE: exit=0; shell=/bin/sh; cwd=/var/home/zach/source/projects/libresite/trees/bootstrap; path=9914968ff619/14 entries; EXPECT=matched; output-sha256=961da68a70b8e2f0ec5607c02434607b17072f8622395ccdf6aaa3c33fdaa025; output-bytes=194

- [x] G3: Public extension contracts, recipe fields, prompts, design provenance, CLI usage and two agent skills are documented against implementation.
  EVIDENCE: Manual review 2026-09-09: compared CLI options, recipe/catalog parsers, provider adapter, module registry, signals and limits with README.org, docs/*.org, AGENTS.md and both validated SKILL.md files; seven design snapshots retained and curated in one runtime catalog. Existing LICENSE SHA-256 f92fb007c28d977b974ecd4c81f2b6581f1b480abb8d7b27d96f41db405ff9ec preserved.
