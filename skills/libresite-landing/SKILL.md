---
name: libresite-landing
description: Turn a concrete landing-page, studio-site or portfolio brief into a libresite YAML recipe and generate its static website project. Use for libresite-driven site creation, not unrelated frontend edits.
---

# Generate a static site with libresite

Locate the libresite checkout or installed CLI. Read `docs/recipe-format.org` and `recipes/landing.yaml` from that checkout (installed copies are under `share/libresite`). Keep the user's audience, brand, supplied facts, assets, provider/model choices and output location authoritative.

Create a dedicated recipe rather than modifying the shipped example. Put the product brief, audience, stack and accessibility requirements in `site`. Select `editorial`, `minimalist`, `industrial` or `cinematic` based on the brief. Use the smallest complete pipeline: brief, design, frontend, review, validate. The final native check should require the real output files, including the homepage, stylesheet, README, AGENTS.md and design/review documents. AGENTS.md tells a later agent how to preview, test and troubleshoot the generated site.

Treat mobile and desktop as first-class defaults without asking the user to opt in. Require deliberate phone, tablet and desktop compositions and complete navigation/content/actions on every route. Keep the shared responsive acceptance matrix from `docs/prompts.org` in generated test documentation and AGENTS.md: 360x800, 390x844, 768x1024, 1366x768 and 1440x900, intermediate widths, phone landscape and 200 percent zoom. After generation, check viewport configuration, reflow, media, touch/keyboard/pointer access and unintended overflow; label browser checks unexecuted when no browser evidence exists. Native validation proves file presence only. Preserve these defaults when replacing the prompt catalog.

Specify a real destination for each conversion action. Do not invent contact information, customers, testimonials or statistics. A static site should not imply that a signup or payment backend exists. Use local SVG artwork and licensed supplied assets; the builder's response protocol emits text files, not raster images.

Run the recipe through `--validate`, inspect `--plan`, and inspect `--preview design`. If the builder needs changes, read repository `AGENTS.md`; keep recipe work separate from tool implementation. Build with GNU make if necessary. Run generation with `--recipe FILE --output DIR` once the brief is concrete and the requested generation is authorized. The output directory must be new and its parent must exist. A provider must be installed and authenticated; report an actual setup failure without inventing success.

Use `--resume` only with the same output, recipe and prompt catalog after an interruption. After publication, read the generated README and review document. Check the actual HTML, stylesheet, links and keyboard behavior, and use browser tooling if available. Distinguish what was checked from model claims. Report the generated directory and how to open it; do not deploy or publish it to a service unless requested.

For prompt changes, edit a copied full `data/prompts.yaml` catalog and use `--prompts FILE`. The snapshots in `data/design-references` are reference material; the centralized runtime catalog already reconciles their conflicting directions.

For explicit delivery contracts, add `validate.options.checks` with exact `path`, `contains`/`excludes` literal lists and optional inclusive `min_bytes`/`max_bytes` integers. Read `recipes/checked-landing.yaml` as an example. Keep `required` for files that must contain non-whitespace text. These predicates run before candidate acceptance and can trigger corrective retries; they do not replace browser or accessibility review.
