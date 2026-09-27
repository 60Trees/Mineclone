#!/usr/bin/env bash
set -e

cd "$(dirname "$0")"

if ! command -v doxygen &>/dev/null; then
    echo "Error: Doxygen is not installed."
    exit 1
fi

mkdir -p docs

cat > Doxyfile <<EOF
PROJECT_NAME           = "$(basename "$PWD")"
OUTPUT_DIRECTORY       = "$PWD/docs"
INPUT                  = "$PWD/src"
RECURSIVE              = YES
GENERATE_HTML          = YES
GENERATE_LATEX         = NO
HTML_EXTRA_STYLESHEET  = "$PWD/doxygen-aero.css"
HTML_COLORSTYLE        = LIGHT
HTML_COLORSTYLE_HUE    = 220
HTML_COLORSTYLE_SAT    = 100
HTML_COLORSTYLE_GAMMA  = 80
DISABLE_INDEX          = NO
GENERATE_TREEVIEW      = YES
FULL_SIDEBAR           = NO
HTML_DYNAMIC_SECTIONS  = YES
EXTRACT_ALL            = NO
HIDE_UNDOC_CLASSES     = YES
EOF

doxygen Doxyfile
echo "Documentation generated in docs/html/"
