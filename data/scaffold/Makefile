# SPDX-License-Identifier: AGPL-3.0-or-later
# Static sources are the deliverable; no package manager or compilation needed.
.DEFAULT_GOAL := all
.PHONY: all tests test clean help license
all: tests
tests test:
	bash tests/scaffold.sh
clean:
	@printf '%s\n' 'Static site: no generated build files to remove.'
help:
	@printf '%s\n' 'make all | tests | clean | help | license' 'Example: make tests' 'Open index.html locally; configure your static host to serve 404.html with HTTP 404.'
license:
	@printf '%s\n' 'Scaffold: AGPL-3.0-or-later. See LICENSE for the completed project license.'
