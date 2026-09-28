#!/bin/sh
# buildinfo.sh - erzeugt buildinfo.h: Codezeilen, Revision und Datum des Commits.
# Reproduzierbar: kein Bauzeitpunkt, nur Daten aus dem Quellbaum.
lines=$(tools/loc.sh kernel)
rev=$(git rev-parse --short=7 HEAD 2>/dev/null || echo unknown)
date=$(git log -1 --format=%cs 2>/dev/null || echo unknown)
if [ -n "$(git status --porcelain -- kernel 2>/dev/null)" ]; then
    rev="$rev+"
fi
cat <<EOT
/* Generiert von tools/buildinfo.sh. */
#define RC_BUILD_LINES $lines
#define RC_BUILD_REV "$rev"
#define RC_BUILD_DATE "$date"
EOT
