#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
prefix="${HOME}/.local"
build_dir="${script_dir}/build"

usage() {
    printf 'Usage: bash .install.sh [--prefix DIR] [--build-dir DIR]\n'
}

while (($#)); do
    case "$1" in
        --prefix)
            if (($# < 2)); then
                usage >&2
                exit 2
            fi
            prefix="$2"
            shift 2
            ;;
        --build-dir)
            if (($# < 2)); then
                usage >&2
                exit 2
            fi
            build_dir="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            printf 'Unknown option: %s\n' "$1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if ! command -v cmake >/dev/null 2>&1; then
    printf 'Error: CMake is required to build Twig.\n' >&2
    exit 1
fi

cmake -S "$script_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir" --config Release
cmake --install "$build_dir" --config Release --prefix "$prefix"
printf 'Twig installed to %s/bin/twig\n' "${prefix%/}"

case ":${PATH}:" in
    *":${prefix%/}/bin:"*) ;;
    *) printf 'Add %s/bin to PATH to run twig from any directory.\n' "${prefix%/}" ;;
esac
