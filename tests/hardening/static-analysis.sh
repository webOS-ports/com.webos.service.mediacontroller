#!/bin/sh
# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# SPDX-License-Identifier: Apache-2.0
#
# static-analysis.sh <corei7-64 recipe workdir> [work dir]
#
# Static-analysis gate for the service's sources (src/), built with cover
# art and the test method on, so that all of them are analysed. Runs:
#
#   gcc -fanalyzer -O0, -O2  use after free, double free, leaks, NULL
#                            dereference, uninitialised reads; twice, as
#                            inlining at -O2 hides some paths and shows others
#   clang analyzer           the same from a second engine, with the alpha
#                            unix/security/core checkers
#   clang-tidy               clang-analyzer-*, bugprone-*, cert-*, ...
#   cppcheck                 all checks, exhaustive, inconclusive
#   flawfinder               race-prone calls (TOCTOU), unsafe string calls
#   Coccinelle               tests/hardening/cocci/*.cocci: a bare LSError
#                            instead of the self-freeing CLSError
#
# sparse is not run: it parses C only, and the service is C++. A Coccinelle
# rule for a bus method returning false (luna-service2's "not handled"
# second reply) would be worth having, but Coccinelle 1.3 does not match
# class-qualified function definitions, which every method here is;
# Replies.EveryMethodRepliesOnceToAnyPayload checks it at run time instead.
#
# Findings are compared with tests/hardening/baseline.txt, the triaged ones
# that are false positives or not this service's to fix (each with its
# reason). Anything else fails the gate. A tool that is not installed is
# reported and skipped, not passed.
set -u
export LC_ALL=C

W=${1:?usage: static-analysis.sh <corei7-64 recipe workdir> [work dir]}
WORK=${2:-$HOME/claude-scratch/mediacontroller/analysis}
SRC=$(cd "$(dirname "$0")/../.." && pwd)
HERE=$SRC/tests/hardening
S=$W/recipe-sysroot
N=$W/recipe-sysroot-native
B=$WORK/build
mkdir -p "$WORK"
skipped=""
fail=0

export PKG_CONFIG_SYSROOT_DIR="$S"
export PKG_CONFIG_LIBDIR="$S/usr/lib/pkgconfig:$S/usr/share/pkgconfig"
export PKG_CONFIG_PATH="$S/usr/lib/pkgconfig"

# A configured tree for the include paths and defines.
gccdir=$(ls -d "$S"/usr/lib/x86_64-*-linux/* | head -n 1)
triple=$(basename "$(dirname "$gccdir")")
cxxinc=$S/usr/include/c++/$(basename "$gccdir")
PATH=$N/usr/bin:$PATH cmake -S "$SRC" -B "$B" -G Ninja \
	-DCMAKE_TOOLCHAIN_FILE="$HERE/host-sysroot.cmake" -DMCS_SYSROOT="$S" -DMCS_HOST_CC=gcc \
	-DWEBOS_INSTALL_ROOT=/ -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
	-DMCS_COVER_ART=ON -DMCS_TEST_METHODS=ON \
	>"$WORK/configure.log" 2>&1 || { echo "configure failed, see $WORK/configure.log"; exit 2; }

# The flags the build uses for a source file, without the output options.
flags() {
	python3 - "$B/compile_commands.json" "$1" <<'EOF'
import json, shlex, sys
db, src = sys.argv[1], sys.argv[2]
for e in json.load(open(db)):
    if e["file"].endswith(src):
        args = shlex.split(e["command"])[1:]
        out, skip = [], False
        for a in args:
            if skip: skip = False; continue
            if a in ("-o", "-c", "-MF", "-MT", "-MQ"): skip = True; continue
            if a.endswith(src) or a.startswith("-M"): continue
            out.append(a)
        print(" ".join(shlex.quote(a) for a in out))
        break
EOF
}

SOURCES=$(cd "$SRC" && ls src/*.cpp src/coverArt/*.cpp)
: >"$WORK/findings.txt"

record() {
	# tool, then findings on stdin, one per line, as "file:line: text"
	sed -e "s|$SRC/||g" -e "s|^|$1: |" >>"$WORK/findings.txt"
}

have() {
	command -v "$1" >/dev/null 2>&1 && return 0
	skipped="$skipped $1"
	echo "== $1: not installed, skipped"
	return 1
}

if have gcc; then
	for opt in -O0 -O2; do
		echo "== gcc -fanalyzer $opt"
		for f in $SOURCES; do
			eval "gcc -x c++ -c -o /dev/null $opt -fanalyzer -Wanalyzer-too-complex \
				--param=analyzer-bb-explosion-factor=50 --param=analyzer-max-enodes-per-program-point=64 \
				$(flags "$f") $SRC/$f" 2>&1
		done | grep -E "^$SRC/src/[^:]+:[0-9]+:[0-9]+: warning: .*\[-Wanalyzer" \
			| sed -E 's/terminating analysis for this program point:.*\[-Wanalyzer-too-complex\]/terminating analysis (too complex) [-Wanalyzer-too-complex]/' \
			| sort -u | record "gcc-fanalyzer$opt"
	done
fi

if have clang; then
	echo "== clang analyzer"
	for f in $SOURCES; do
		eval "clang++ --analyze -Xanalyzer -analyzer-output=text \
			-Xanalyzer -analyzer-checker=core,cplusplus,unix,deadcode,security,nullability,optin.portability,optin.core \
			-Xanalyzer -analyzer-checker=alpha.unix,alpha.security,alpha.core.PointerArithm,alpha.core.CastSize,alpha.core.Conversion,alpha.cplusplus \
			--gcc-install-dir=$gccdir -isystem $cxxinc -isystem $cxxinc/$triple \
			$(flags "$f" | sed 's/-fpermissive//; s/-fcanon-prefix-map//') -o /dev/null $SRC/$f" 2>&1
	done | grep -E "^$SRC/src/[^:]+:[0-9]+:[0-9]+: warning:" \
		| sed -E 's/:[0-9]+: warning:/: warning:/' | sort -u | record clang-analyzer
fi

if have clang-tidy; then
	echo "== clang-tidy"
	for f in $SOURCES; do
		clang-tidy --quiet -p "$B" "$SRC/$f" \
			--checks='clang-analyzer-*,bugprone-*,cert-*,concurrency-*,misc-*,performance-*,portability-*,readability-misleading-indentation,readability-suspicious-call-argument,-bugprone-easily-swappable-parameters,-misc-include-cleaner,-misc-unused-parameters,-misc-non-private-member-variables-in-classes,-misc-use-anonymous-namespace,-misc-use-internal-linkage,-cert-err33-c,-bugprone-assignment-in-if-condition,-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling,-bugprone-reserved-identifier,-cert-dcl37-c,-cert-dcl51-cpp,-misc-const-correctness,-cert-err58-cpp' \
			--extra-arg="--gcc-install-dir=$gccdir" --extra-arg="-isystem$cxxinc" --extra-arg="-isystem$cxxinc/$triple" \
			--extra-arg=-Wno-unknown-warning-option 2>/dev/null
	done | grep -E "^$SRC/src/[^:]+:[0-9]+:[0-9]+: (warning|error):" \
		| sed -E 's/:[0-9]+: (warning|error):/: \1:/' | sort -u | record clang-tidy
fi

if have cppcheck; then
	echo "== cppcheck"
	# shellcheck disable=SC2046,SC2086  # one argument per source file
	cppcheck --enable=all --check-level=exhaustive --inconclusive --std=c++23 --language=c++ \
		--library=gnu --library=posix --force --inline-suppr --quiet \
		--suppress=missingIncludeSystem --suppress=missingInclude --suppress=unmatchedSuppression \
		--suppress=checkersReport --suppress=unusedFunction \
		--template='{file}:{line}: {severity}: {message} [{id}]' \
		-I "$SRC/include" -I "$SRC/src/coverArt" -DMCS_COVER_ART=1 -DUSE_TEST_METHOD=1 -D__linux__ -D__GNUC__ \
		$(cd "$SRC" && printf '%s ' $SOURCES | sed "s|src/|$SRC/src/|g") \
		>"$WORK/cppcheck.log" 2>&1
	# Style findings (naming, const-ness, C casts) are listed for reading but
	# do not gate; the bug classes do.
	grep -E "^$SRC/src/[^:]+:[0-9]+: style:" "$WORK/cppcheck.log" | sed "s|$SRC/||" | sort -u >"$WORK/cppcheck-style.txt"
	grep -E "^$SRC/src/[^:]+:[0-9]+: (error|warning|performance|portability):" "$WORK/cppcheck.log" \
		| sort -u | record cppcheck
	# ...except the style checks that point at logic errors.
	grep -E "\[(knownConditionTrueFalse|redundantAssignment|duplicateCondition|identicalConditionAfterEarlyExit|oppositeInnerCondition|duplicateBranch|uninitvar|nullPointer.*)\]$" \
		"$WORK/cppcheck-style.txt" | sed "s|^|$SRC/|" | record cppcheck
fi

if have flawfinder; then
	echo "== flawfinder"
	flawfinder --minlevel=1 --falsepositive --dataonly --quiet "$SRC/src" 2>&1 \
		| grep -E "^$SRC/src/[^:]+:[0-9]+: +\[[1-5]\]" | sort -u | record flawfinder
fi

# The diff spatch prints, as "file:line: rule: text" for each line a rule
# marked. The file is the "---" side: the "+++" side is a temporary file
# named after spatch's pid.
cocci() {
	spatch --very-quiet --c++ --sp-file "$1" "$2" 2>/dev/null \
		| awk -v rule="$(basename "$1" .cocci)" '
			/^--- / { file = $2; next }
			/^@@ / { split($2, a, ","); line = -a[1]; n = 0; next }
			/^-/ && !/^---/ { print file ":" line + n ": " rule ": " substr($0, 2); n++; next }
			/^ / { n++ }'
}

if have spatch; then
	echo "== Coccinelle"
	# A rule that finds nothing in its own self-test is broken, not clean.
	for c in "$HERE"/cocci/*.cocci; do
		if [ -z "$(cocci "$c" "$HERE/cocci/selftest.cpp")" ]; then
			echo "   $(basename "$c") matches nothing in cocci/selftest.cpp"
			fail=1
		fi
	done
	for c in "$HERE"/cocci/*.cocci; do
		for f in $SOURCES; do
			cocci "$c" "$SRC/$f"
		done
	done | sed -e "s|$SRC/||" | sort -u | record coccinelle
fi

# Line numbers move; a baseline entry is tool, file and message.
key() { sed -E 's/^([^:]+): ([^:]+):[0-9]+(:[0-9]+)?:/\1: \2:/'; }
grep -v '^#' "$HERE/baseline.txt" 2>/dev/null | sed 's/ *#.*//' | grep . | key | sort -u >"$WORK/baseline.keys"
key <"$WORK/findings.txt" | sort -u >"$WORK/findings.keys"
comm -23 "$WORK/findings.keys" "$WORK/baseline.keys" >"$WORK/new.keys"
comm -13 "$WORK/findings.keys" "$WORK/baseline.keys" >"$WORK/gone.keys"

echo
[ -s "$WORK/cppcheck-style.txt" ] && echo "cppcheck style notes (not gated): $(wc -l <"$WORK/cppcheck-style.txt"), in $WORK/cppcheck-style.txt"
echo "findings: $(wc -l <"$WORK/findings.keys"), triaged in baseline.txt: $(wc -l <"$WORK/baseline.keys")"
if [ -s "$WORK/gone.keys" ]; then
	echo "no longer reported (drop from baseline.txt):"
	sed 's/^/  /' "$WORK/gone.keys"
fi
[ -n "$skipped" ] && echo "skipped:$skipped"
if [ -s "$WORK/new.keys" ]; then
	echo "NEW findings:"
	sed 's/^/  /' "$WORK/new.keys"
	exit 1
fi
[ $fail -ne 0 ] && { echo "a Coccinelle rule is broken"; exit 1; }
echo "clean"
exit 0
