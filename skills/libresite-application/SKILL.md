---
name: libresite-application
description: Create a libresite recipe for a complete web-application slice with frontend, backend and persistence, then generate and verify the output project. Use when the user wants application generation through libresite.
---

# Generate an application with libresite

Locate the checkout or installed CLI and read `docs/recipe-format.org`, `docs/operations.org` and `recipes/application.yaml`. Define a small complete user workflow before selecting modules. Preserve the requested stack. If no stack is specified, the repository's example is a local single-user GNU89/GLib application with SQLite and ordinary HTML forms.

Write a new recipe describing routes, input constraints, state transitions, persistence and the actual audience. Use `application` style for repeated-use product interfaces. Choose a pipeline of brief, design, database, backend, frontend, review and native validate, adjusting order when a concrete integration requires it. Every step may specify its own `ai.provider` and `ai.model`. A changed provider should get its own model; inspect `--plan` before generation.

Describe the frontend/backend/database contract explicitly in the brief. Do not replace persistence with an in-memory mock, invent an authentication flow, or choose a dependency that conflicts with the user's FOSS requirement. When requesting htmx-glib or orm-glib, inspect their actual headers/docs and supply the relevant API contract in step instructions; a model cannot infer a local library's API from its name. Keep projects small enough for the complete-context prompt limit. Use a native plugin or split genuinely independent components when a larger application cannot fit.

Require build files, README.org, product/design/review docs and concrete implementation files in the final native validation step. Run `--validate`, inspect `--plan`, and preview the backend contract. Generate to a new directory with an installed/authenticated provider. Resume only matching work. Do not execute arbitrary generated setup commands without first reading them and confirming they fit the user's requested work.

After generation, inspect the output's README and build configuration. List the Fedora build dependencies explicitly. Run appropriate build/tests in the user's development environment; on an immutable host, do not mutate `/usr` or install host packages automatically. Check the main workflow and failure states if the runtime is available. A passing libresite `validate` step proves required file presence, not a passing application build or browser test. Report actual checks, remaining runtime limitations and the output directory. Deployment remains a separate requested action.
