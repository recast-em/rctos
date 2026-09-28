#!/bin/sh
# loc.sh - zählt Codezeilen für das Codebudget: ohne Leerzeilen, ohne reine
# Kommentarzeilen, ohne generierte Daten (Schrift) und ohne Fremdcode (limine.h).
# Aufruf: tools/loc.sh <verzeichnis>...
find "$@" \( -name '*.c' -o -name '*.h' -o -name '*.S' \) \
    ! -name 'font8x16.c' ! -name 'limine.h' -print | sort | xargs cat | awk '
    /^[ \t]*$/                  { next }
    in_comment                  { if ($0 ~ /\*\//) in_comment = 0; next }
    /^[ \t]*\/\*/               { if ($0 !~ /\*\//) in_comment = 1; next }
    /^[ \t]*\/\//               { next }
                                { n++ }
    END                         { print n + 0 }'
