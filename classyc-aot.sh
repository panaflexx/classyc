#!/usr/bin/env bash
#
# classyc-aot - Ahead-of-time compile C source files to a native executable
#           using the MIR toolchain (classyc -> b2obj -> gcc).
#
# Pipeline, per the project workflow:
#     classyc  -c  foo.c            ->  foo.bmir   (C  -> binary MIR)
#     b2obj    foo.bmir foo.o   ->  foo.o      (MIR -> ELF object)
#     gcc -o prog *.o           ->  prog       (link with the system linker)
#
# Usage:
#     classyc-aot [options] file1.c [file2.c ...] -o output
#
# Recognised inputs:
#     *.c            C source, compiled with classyc and b2obj
#     *.bmir         pre-built binary MIR, converted with b2obj
#     *.o  *.a       passed straight to the linker
#
# Options:
#     -o FILE        name of the output executable (default: a.out)
#     -I DIR / -IDIR, -D..., -U..., -include F, -std=..., -O..., -w, -pedantic
#                    passed through to classyc (the C front end)
#     -F DIR / -FDIR, -framework NAME
#                    clang-style frameworks: classyc (headers + JIT) and the linker
#     -L..., -l...   passed through to gcc (the linker)
#     --with-mir     also link the MIR core library (mir.o + mir-gen.o) - needed
#                    when the compiled program calls the MIR API, e.g. when
#                    bootstrapping the classyc compiler itself
#     -g             build a debug binary: emit debug info from classyc (which
#                    b2obj turns into DWARF automatically) and link gcc with -g
#     -k, --keep     keep intermediate .bmir / .o files (in a temp dir)
#     -v, --verbose  echo each command before running it
#     -h, --help     show this help and exit
#
# Examples:
#     classyc-aot hello.c -o hello
#     classyc-aot --with-mir -I c2mir -I. \
#             src/classyc.c src/classyc-driver.c -o classyc.selfhosted
#
set -euo pipefail

prog=$(basename "$0")

usage () { sed -n '2,35p' "$0" | sed 's/^# \{0,1\}//'; }

# Locate the classyc and b2obj tools: prefer ones next to this script, then PATH.
# The C2M and B2OBJ environment variables override the tools, which makes it
# possible to bootstrap successive generations of the compiler, e.g.
#     C2M=./classyc-aot ./classyc-aot --with-mir ... -o classyc-gen2.aot
script_dir=$(cd "$(dirname "$0")" && pwd)
csrc_dir="./src"
mir_dir="ext/mir"
mir_lib_dir="./lib"

find_tool () {
  local name=$1
  if [ -x "$script_dir/$name" ]; then echo "$script_dir/$name"
  elif command -v "$name" >/dev/null 2>&1; then command -v "$name"
  else echo "$prog: cannot find '$name' (looked in $script_dir and \$PATH)" >&2; exit 1
  fi
}

# OS-specific tool names and link flags.  b2obj emits PIC via .mir.addrpool
# (no ABS64 in .text), so Linux can use the default PIE toolchain.
pie_flags=()
if [ "$(uname -s)" = "Darwin" ]; then
    B2OBJ_DEFAULT="b2objmac"
else
    B2OBJ_DEFAULT="b2obj"
fi

C2M=${C2M:-$(find_tool "bin/classyc")}
B2OBJ=${B2OBJ:-$(find_tool "bin/$B2OBJ_DEFAULT")}
CC=${CC:-gcc}

output="a.out"
keep=0
verbose=0
debug=0            # -g: build a debug (DWARF) binary
with_mir=0         # link the MIR core (mir.o + mir-gen.o)
chanfibers=0       # -ffibers: compile mir-aot-runtime.c with -DCHANFIBERS

# Initialize all arrays properly (critical with `set -u`)
c2m_flags=()
objects=()
link_objects=()
ld_flags_v=()
default_libs=()

# These standard libraries are commonly needed
default_libs=(-lm -lpthread -ldl)

run () { [ "$verbose" -eq 1 ] && echo "+ $*" >&2; "$@"; }

# --- argument parsing -------------------------------------------------------
while [ $# -gt 0 ]; do
  arg=$1
  case "$arg" in
    -h|--help) usage; exit 0 ;;
    -k|--keep) keep=1 ;;
    -v|--verbose) verbose=1 ;;
    --with-mir)
	  c2m_flags+=("-I" "$mir_dir")
	  c2m_flags+=("-I" "include")
	  with_mir=1 ;;
    -o) shift; [ $# -gt 0 ] || { echo "$prog: -o needs an argument" >&2; exit 1; }; output=$1 ;;
    -o*) output=${arg#-o} ;;
    # -g: emit debug info through the whole pipeline (classyc -> b2obj -> gcc)
    -g) debug=1; c2m_flags+=("$arg") ;;
    # c2m front-end flags that take a separate argument
    -I|-D|-U|-include|-F)
      shift; [ $# -gt 0 ] || { echo "$prog: $arg needs an argument" >&2; exit 1; }
      c2m_flags+=("$arg" "$1")
      # -F is also a linker framework-search path (clang).
      [ "$arg" = "-F" ] && ld_flags_v+=("$arg" "$1") ;;
    -framework)
      shift; [ $# -gt 0 ] || { echo "$prog: $arg needs an argument" >&2; exit 1; }
      c2m_flags+=("-framework" "$1")
      ld_flags_v+=("-framework" "$1") ;;
    -F*)
      c2m_flags+=("$arg")
      ld_flags_v+=("$arg") ;;
    -I*|-D*|-U*|-std=*|-O*|-w|-pedantic|-fsigned-char|-fno-*)
      c2m_flags+=("$arg") ;;
    # -ffibers: opt-in go/await syntax for classyc, and pull the fiber/channel
    # runtime (cyfiber) into the AOT runtime object via -DCHANFIBERS.  Also
    # pass -DCHANFIBERS to classyc so TUs like http-serve-fibers.c can skip
    # MINICORO_IMPL (mir-aot-runtime already provides mco_*).
    -ffibers)
      c2m_flags+=("$arg" "-DCHANFIBERS")
      chanfibers=1 ;;
    # linker flags that take a separate argument
    -L|-l)
      shift; [ $# -gt 0 ] || { echo "$prog: $arg needs an argument" >&2; exit 1; }
      ld_flags_v+=("$arg" "$1") ;;
    -L*|-l*|-Wl,*)
      ld_flags_v+=("$arg") ;;
    *.c)    sources+=("$arg") ;;
    *.cy)   sources+=("$arg") ;;
    *.bmir) sources+=("$arg") ;;
    *.o|*.a) link_objects+=("$arg") ;;
    -*)     echo "$prog: warning: unrecognised option '$arg' forwarded to linker" >&2
            ld_flags_v+=("$arg") ;;
    *)      echo "$prog: warning: unrecognised input '$arg' forwarded to linker" >&2
            link_objects+=("$arg") ;;
  esac
  shift
done

if [ ${#sources[@]} -eq 0 ] && [ ${#link_objects[@]} -eq 0 ]; then
  echo "$prog: no input files" >&2
  usage >&2
  exit 1
fi

# --- working directory for intermediates -----------------------------------
workdir=$(mktemp -d "${TMPDIR:-/tmp}/c2m-aot.XXXXXX")
cleanup () { [ "$keep" -eq 1 ] || rm -rf "$workdir"; }
trap cleanup EXIT
[ "$keep" -eq 1 ] && echo "$prog: keeping intermediates in $workdir" >&2

objects=()

# Build the small MIR ahead-of-time runtime (conversion-builtin helpers) and
# link it in automatically.  Harmless for programs that do not need it.
if [ -f "$csrc_dir/mir-aot-runtime.c" ]; then
  echo Compile runtime support $csrc_dir
  rt_obj="$workdir/mir-aot-runtime.o"
  rt_cmd=("$CC" -O2)
  [ "$debug" -eq 1 ] && rt_cmd+=(-g)
  [ "$chanfibers" -eq 1 ] && rt_cmd+=(-DCHANFIBERS -I ext/ccchan)
  rt_cmd+=(-c -I include -I "${mir_dir}" "$csrc_dir/mir-aot-runtime.c" -o "$rt_obj")
  echo "${rt_cmd[@]}"
  run "${rt_cmd[@]}"
  link_objects+=("$rt_obj")
fi
# Emulated TLS runtime (mir_tls_addr / mir_tls_base) for N1 AOT
if [ -f "${mir_dir}/mir-tls.c" ]; then
  tls_obj="$workdir/mir-tls.o"
  tls_cmd=("$CC" -O2 -c -I "${mir_dir}" "${mir_dir}/mir-tls.c" -o "$tls_obj")
  [ "$debug" -eq 1 ] && tls_cmd=("$CC" -O2 -g -c -I "${mir_dir}" "${mir_dir}/mir-tls.c" -o "$tls_obj")
  echo "${tls_cmd[@]}"
  run "${tls_cmd[@]}"
  link_objects+=("$tls_obj")
fi

# --with-mir: link the MIR core (mir.o + mir-gen.o), but NOT c2mir.o (which the
# bootstrap compiles from source itself).  Prefer standalone objects next to the
# script; otherwise extract them from libmir.a so a plain checkout still works.
if [ "$with_mir" -eq 1 ]; then
  if [ -f "${mir_lib_dir}/libmir_static.a" ]; then
    link_objects+=("${mir_lib_dir}/libmir_static.a")
  else
    echo "$prog: --with-mir: need $script_dir/{mir.o,mir-gen.o} or $script_dir/libmir.a" >&2
    exit 1
  fi
fi

# Avoid name collisions when two sources share a basename (e.g. a/x.c b/x.c).
unique_base () {
  local b=$1 n var

  # Use a regular variable as a counter (dynamic variable name)
  var="seen_base_${b//[^a-zA-Z0-9_]/_}"   # sanitize name

  if eval "[ -z \"\${$var:-}\" ]"; then
    eval "$var=1"
    echo "$b"
    return
  fi

  eval "n=\$$var"
  eval "$var=\$((n + 1))"
  echo "${b}_$n"
}

for src in "${sources[@]}"; do
  base=$(basename "$src"); base=${base%.*}
  base=$(unique_base "$base")
  obj="$workdir/$base.o"

  # b2obj takes exactly <input> <output>; it emits DWARF automatically when
  # the .bmir carries debug info, i.e. when classyc was given -g above.
  case "$src" in
    *.c|*.cy)
      bmir="$workdir/$base.bmir"

      compile_cmd=("$C2M")
      [ ${#c2m_flags[@]} -gt 0 ] && compile_cmd+=("${c2m_flags[@]}")
	  compile_cmd+=(-c -o "$bmir" "$src")
	  run "${compile_cmd[@]}"

      run "$B2OBJ" "-O3" "$bmir" "$obj"
      ;;
    *.bmir)
      run "$B2OBJ" "-O3" "$src" "$obj"
      ;;
  esac
  objects+=("$obj")
done

# --- link -------------------------------------------------------------------

# Build the full command safely for old bash + set -u
link_cmd=("$CC" -o "$output")

# Keep DWARF in the final binary when building a debug executable.
[ "$debug" -eq 1 ] && link_cmd+=(-g)

# Append arrays safely (this avoids unbound variable errors on Bash 3.2)
[ ${#objects[@]} -gt 0 ] && link_cmd+=("${objects[@]}")
[ ${#link_objects[@]} -gt 0 ] && link_cmd+=("${link_objects[@]}")
[ ${#ld_flags_v[@]} -gt 0 ] && link_cmd+=("${ld_flags_v[@]}")
[ ${#pie_flags[@]} -gt 0 ] && link_cmd+=("${pie_flags[@]}")
[ ${#default_libs[@]} -gt 0 ] && link_cmd+=("${default_libs[@]}")

# Show the command (safe echo)
echo "${link_cmd[@]}"

# Actually run it
run "${link_cmd[@]}"

echo "$prog: created $output"
