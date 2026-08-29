#!/usr/bin/env bash
set -euo pipefail

prefix="${1:-${HOME}/.local}"
twig_binary="${prefix%/}/bin/twig"

if [[ ! -e "$twig_binary" && ! -L "$twig_binary" ]]; then
    printf 'Twig is not installed at %s\n' "$twig_binary"
    exit 0
fi

if [[ -d "$twig_binary" ]]; then
    printf 'Refusing to remove a directory: %s\n' "$twig_binary" >&2
    exit 1
fi

rm -- "$twig_binary"
printf 'Removed %s\n' "$twig_binary"
