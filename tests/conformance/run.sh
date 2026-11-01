#!/bin/sh
# Differential conformance suite: every progs/NAME.i is compiled with the
# C-INTERCAL oracle ($ORACLE_ICK -b) and with ours ($OURS_ICK -b, default
# build/ick).  If the oracle rejects the program, both compilers' stdout,
# stderr and exit status must match.  Otherwise both executables run with
# stdin from progs/NAME.in (or /dev/null) and their stdout, stderr and exit
# status must match byte for byte.
#
# progs/slow/ holds pit programs that are correct tests but expensive to
# compile (several CPU-seconds each); they run only with SLOW=1 or by name.
#
# Usage: tests/conformance/run.sh [NAME...]   (NAME like hello or slow/quine)
# Env:   OURS_ICK  compiler under test (default: build/ick)
#        JOBS      parallel programs (default: 4)
#        SLOW=1    also run progs/slow/*.i

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)

if [ -z "${ORACLE_ICK:-}" ]; then
  cd "$root" || exit 2
  exec nix develop -c sh "$here/run.sh" "$@"
fi

progs=$here/progs

# Single-program worker: run.sh --one ID RESULTDIR   (ID = path under progs/)
if [ "${1:-}" = "--one" ]; then
  name=$(basename "$2")
  src=$progs/$(dirname "$2")
  out=$3/$(echo "$2" | tr / _)
  ours=$OURS_ICK
  w=$(mktemp -d "${TMPDIR:-/tmp}/conf.$name.XXXXXX") || exit 2
  trap 'rm -rf "$w"' EXIT
  input=/dev/null
  [ -f "$src/$name.in" ] && input=$src/$name.in

  # compile SIDE COMPILER: leaves cout/cerr/cstatus in $w/SIDE
  compile() {
    mkdir "$w/$1"
    cp "$src/$name.i" "$w/$1/$name.i"
    if [ -x "$2" ]; then
      (cd "$w/$1" && timeout 60 "$2" -b "$name.i" >cout 2>cerr </dev/null)
      echo $? >"$w/$1/cstatus"
    else
      : >"$w/$1/cout"
      echo "compiler not found: $2" >"$w/$1/cerr"
      echo 127 >"$w/$1/cstatus"
    fi
  }
  # run SIDE: leaves out/err/status in $w/SIDE
  run() {
    (cd "$w/$1" && timeout 10 "./$name" <"$input" >out 2>err)
    echo $? >"$w/$1/status"
  }
  # compare FILE...: diff each file between the two sides
  compare() {
    ok=0
    for f in "$@"; do
      if ! cmp -s "$w/oracle/$f" "$w/ours/$f"; then
        ok=1
        echo "  --- $f differs (oracle vs ours):"
        diff -u "$w/oracle/$f" "$w/ours/$f" >"$w/d"
        if head -n 1 "$w/d" | grep -q '^--- '; then
          sed -n '3,22p' "$w/d"
        else
          sed -n '1,20p' "$w/d"
        fi | sed 's/^/  /'
      fi
    done
    return $ok
  }

  compile oracle "$ORACLE_ICK"
  compile ours "$ours"
  if [ "$(cat "$w/oracle/cstatus")" -ne 0 ] || [ ! -x "$w/oracle/$name" ]; then
    # oracle rejected the program: compiler diagnostics must agree
    if compare cout cerr cstatus >"$w/diff"; then res=PASS; else res=FAIL; fi
    kind="compile error $(cat "$w/oracle/cstatus")"
  elif [ "$(cat "$w/ours/cstatus")" -ne 0 ] || [ ! -x "$w/ours/$name" ]; then
    res=FAIL
    kind="ours failed to compile (status $(cat "$w/ours/cstatus"))"
    sed -n '1,20p' "$w/ours/cout" "$w/ours/cerr" | sed 's/^/  /' >"$w/diff"
  else
    run oracle
    run ours
    if compare out err status >"$w/diff"; then res=PASS; else res=FAIL; fi
    kind="exit $(cat "$w/oracle/status")"
  fi
  {
    echo "$res $2 ($kind)"
    [ "$res" = FAIL ] && cat "$w/diff"
  } >"$out"
  exit 0
fi

OURS_ICK=${OURS_ICK:-build/ick}
case $OURS_ICK in
  /*) ;;
  */*) OURS_ICK=$root/$OURS_ICK ;;
  *) OURS_ICK=$(command -v "$OURS_ICK" || echo "$root/$OURS_ICK") ;;
esac
export OURS_ICK

results=$(mktemp -d "${TMPDIR:-/tmp}/conf-results.XXXXXX") || exit 2
trap 'rm -rf "$results"' EXIT INT TERM

if [ $# -gt 0 ]; then
  names=$*
else
  names=$(cd "$progs" && ls *.i | sed 's/\.i$//')
  if [ "${SLOW:-0}" = 1 ]; then
    names="$names $(cd "$progs" && ls slow/*.i | sed 's/\.i$//')"
  fi
fi

printf '%s\n' $names |
  xargs -P "${JOBS:-4}" -I{} sh "$here/run.sh" --one {} "$results"

pass=0
total=0
for name in $names; do
  total=$((total + 1))
  r=$results/$(echo "$name" | tr / _)
  if [ -f "$r" ]; then
    cat "$r"
    head -n 1 "$r" | grep -q '^PASS' && pass=$((pass + 1))
  else
    echo "FAIL $name (harness produced no result)"
  fi
done
echo "conformance: $pass/$total passed"
[ "$pass" -eq "$total" ]
