# Generated static-site instructions

- Native scaffolding already supplies Makefile, 404.html, privacy.html, terms.html and docs/contact.org. Preserve working boilerplate; spend generation effort on distinctive composition, typography, content, artwork and useful interactions. Return only new or changed files.
- Read the recipe brief and docs/design.org when present. Apply the shared visual system to utility pages; do not replace a distinctive design with the scaffold's unstyled HTML.
- Preserve supplied copy and approved legal text. Privacy and terms start as clearly marked noindex drafts, not legal policies; never invent operators, services, addresses or legal guarantees. Keep drafts out of public sitemaps until reviewed.
- Follow docs/contact.org only if the brief requests contact submission. No endpoint is implemented by this scaffold. Never enable a pretend form or report a message sent without backend acceptance.
- Static sources require Fedora make, bash and coreutils; run make tests (or bash tests/scaffold.sh). These checks establish file presence only. Add project-specific tests and wire them into make tests; retain the scaffold check. Document actual commands and results in README.org.
- Extend this file with exact project build, preview, SEO maintenance and test commands. Supply LICENSE for the completed project (AGPLv3 by default); preserve third-party notices.
- Verify mobile and desktop layouts, keyboard/focus behavior, reduced motion and accessibility with native HTML semantics and correct ARIA. Record unexecuted browser checks honestly.
- Configure the static host to serve 404.html with HTTP 404. Its home link assumes root hosting; for a subpath deploy, use the actual base path, never a relative link that breaks on nested missing URLs. Do not claim the filesystem check verifies HTTP routing.
