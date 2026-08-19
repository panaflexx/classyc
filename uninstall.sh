#!/usr/bin/env bash
#
# uninstall.sh - remove a classyc install made by install.sh.
#
#   ./uninstall.sh          remove the system install (sudo needed)
#   ./uninstall.sh --user   remove the per-user (~/.classyc) install
set -euo pipefail

repo_dir=$(cd "$(dirname "$0")" && pwd)
cd "$repo_dir"

usage () { sed -n '2,6p' "$0" | sed 's/^# \{0,1\}//'; }

user=0
for arg in "$@"; do
  case "$arg" in
    --user) user=1 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "uninstall.sh: unrecognised option '$arg'" >&2; usage >&2; exit 1 ;;
  esac
done

if [ "$user" -eq 1 ]; then
  build_dir="$repo_dir/build-user"
  cmd=(cmake --build "$build_dir" --target uninstall)
else
  build_dir="$repo_dir/build"
  cmd=(sudo cmake --build "$build_dir" --target uninstall)
fi

if [ ! -d "$build_dir" ]; then
  echo "uninstall.sh: $build_dir not found -- nothing to uninstall" >&2
  echo "(run ./install.sh$( [ "$user" -eq 1 ] && echo ' --user') first)" >&2
  exit 1
fi

"${cmd[@]}"
