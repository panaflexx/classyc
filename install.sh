#!/usr/bin/env bash
#
# install.sh - configure, build, and install classyc.
#
#   ./install.sh          system install to /usr/local (sudo needed for the
#                          final `cmake --install` step)
#   ./install.sh --user   per-user install to ~/.classyc, no sudo needed
#
# Uninstall with ./uninstall.sh (or ./uninstall.sh --user).
set -euo pipefail

repo_dir=$(cd "$(dirname "$0")" && pwd)
cd "$repo_dir"

usage () { sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; }

user=0
for arg in "$@"; do
  case "$arg" in
    --user) user=1 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "install.sh: unrecognised option '$arg'" >&2; usage >&2; exit 1 ;;
  esac
done

if [ ! -f ext/mir/CMakeLists.txt ]; then
  echo "install.sh: ext/mir submodule not initialized; run:" >&2
  echo "    git submodule update --init ext/mir" >&2
  exit 1
fi

jobs=$( (command -v nproc >/dev/null 2>&1 && nproc) \
     || (command -v sysctl >/dev/null 2>&1 && sysctl -n hw.ncpu) \
     || echo 4)

if [ "$user" -eq 1 ]; then
  prefix="$HOME/.classyc"
  build_dir="$repo_dir/build-user"
  echo "install.sh: per-user install to $prefix"
  cmake -B "$build_dir" -S . -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix"
  cmake --build "$build_dir" -j"$jobs"
  cmake --install "$build_dir"
  echo
  echo "install.sh: installed to $prefix"
  case ":$PATH:" in
    *":$prefix/bin:"*) ;;
    *) echo "install.sh: $prefix/bin is not on your PATH; add this to your shell rc:" ;
       echo "    export PATH=\"$prefix/bin:\$PATH\"" ;;
  esac
else
  build_dir="$repo_dir/build"
  echo "install.sh: system install (prefix /usr/local; sudo needed for the install step)"
  cmake -B "$build_dir" -S . -DCMAKE_BUILD_TYPE=Release
  cmake --build "$build_dir" -j"$jobs"
  sudo cmake --install "$build_dir"
  echo
  echo "install.sh: installed to /usr/local"
fi
