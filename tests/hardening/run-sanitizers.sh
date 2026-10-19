#!/bin/sh
# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# SPDX-License-Identifier: Apache-2.0
#
# run-sanitizers.sh <corei7-64 recipe workdir> [work dir]
#
# Builds tests/ with the build host's gcc and clang against the recipe's
# x86-64 sysroot and runs them under:
#   ASan + UBSan (gcc, clang)   use after free, double free, overflows, leaks
#                               (LSan), undefined behaviour; clang adds its
#                               integer and nullability checks
#   TSan (gcc, clang)           data races, between the main loop and the
#                               cover art worker
#   valgrind memcheck           on the image toolchain's own build, which is
#                               not instrumented: invalid accesses and
#                               definite leaks
#
# Each configuration is built twice, with cover art (and the test method)
# on and off: the worker thread only exists with it on.
#
# The fake bus frees each message when its last
# reference goes, as luna-service2 does: a message held past that is a
# use after free for ASan and valgrind. Every LSError the fake hands out is
# heap allocated, so one not freed is a leak.
#
# The image toolchain ships no sanitizer runtimes; the host's do, and the
# host's glibc is the sysroot's (check with <sysroot>/usr/lib/ld-linux-*.so.2
# --version). The binaries get the sysroot's loader as their interpreter and
# an rpath that puts the host's sanitizer runtimes and libstdc++ (newer, so
# backward compatible) ahead of the sysroot's libraries.
#
# The com.webos.service.mediacontroller recipe's corei7-64 workdir has all
# it needs (glib, luna-service2, pbnjson, PmLogLib, curl, googletest).
set -u

W=${1:?usage: run-sanitizers.sh <corei7-64 recipe workdir> [work dir]}
WORK=${2:-$HOME/claude-scratch/mediacontroller/sanitizers}
SRC=$(cd "$(dirname "$0")/../.." && pwd)
S=$W/recipe-sysroot
N=$W/recipe-sysroot-native
RT=$WORK/hostrt
fail=0

mkdir -p "$RT"
for l in libasan.so.8 libubsan.so.1 libtsan.so.2 libstdc++.so.6 libgcc_s.so.1; do
	f=$(gcc -print-file-name="$l")
	[ -e "$f" ] && ln -sf "$(readlink -f "$f")" "$RT/$l"
done
# clang's runtimes are linked statically; nothing to add for them.

export PKG_CONFIG_SYSROOT_DIR="$S"
export PKG_CONFIG_LIBDIR="$S/usr/lib/pkgconfig:$S/usr/share/pkgconfig"
export PKG_CONFIG_PATH="$S/usr/lib/pkgconfig"
export ASAN_OPTIONS=detect_leaks=1:abort_on_error=1:detect_stack_use_after_return=1:fast_unwind_on_malloc=0:strict_string_checks=1
export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
export TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1
export G_SLICE=always-malloc G_DEBUG=gc-friendly
# The symbolizer otherwise reaches for debuginfod and can crash on its cache
export DEBUGINFOD_URLS=

build() {
	name=$1; cc=$2; san=$3; nosan=$4; opts=$5
	[ "$nosan" = - ] && nosan=""
	echo "== build $name"
	# clang does not find the sysroot's gcc (crtbeginS.o, libstdc++'s
	# headers) in its Yocto triple directory on its own. Passed here as the
	# flags below replace the toolchain file's defaults.
	extra=""; gccdir=""
	if [ "$cc" = clang ]; then
		gccdir=$(ls -d "$S"/usr/lib/x86_64-*-linux/* | head -n 1)
		triple=$(basename "$(dirname "$gccdir")")
		cxxinc=$S/usr/include/c++/$(basename "$gccdir")
		extra="--gcc-install-dir=$gccdir -isystem $cxxinc -isystem $cxxinc/$triple"
	fi
	PATH=$N/usr/bin:$PATH cmake -S "$SRC" -B "$WORK/$name" -G Ninja \
		-DCMAKE_TOOLCHAIN_FILE="$SRC/tests/hardening/host-sysroot.cmake" \
		-DMCS_SYSROOT="$S" -DMCS_HOST_CC="$cc" \
		-DWEBOS_INSTALL_ROOT=/ \
		-DBUILD_TESTING=ON $opts -DMCS_SANITIZE="$san" -DMCS_NO_SANITIZE="$nosan" -DCMAKE_CXX_FLAGS="-g -O1 $extra" \
		-DCMAKE_EXE_LINKER_FLAGS="${gccdir:+--gcc-install-dir=$gccdir} -Wl,--dynamic-linker=$S/usr/lib/ld-linux-x86-64.so.2 -Wl,--disable-new-dtags -Wl,-rpath,$RT:$S/usr/lib" \
		>"$WORK/$name-configure.log" 2>&1 || { echo "FAIL configure $name"; tail -20 "$WORK/$name-configure.log"; fail=1; return 1; }
	ninja -C "$WORK/$name" mcs-tests >"$WORK/$name-build.log" 2>&1 \
		|| { echo "FAIL build $name"; grep -E "error|FAILED" "$WORK/$name-build.log" | head -20; fail=1; return 1; }
}

run() {
	name=$1; shift
	if "$@" >"$WORK/$name-run.log" 2>&1; then
		echo "PASS $name ($(grep -E '^\[  PASSED' "$WORK/$name-run.log"))"
	else
		echo "FAIL $name"
		grep -E "ERROR: |runtime error|WARNING: ThreadSanitizer|SUMMARY|Failure|FAILED" "$WORK/$name-run.log" | head -20
		fail=1
	fi
}

# Unsigned wrap-around is defined behaviour, and libstdc++ relies on it
# (basic_string's size arithmetic); the signed and conversion checks stay.
for variant in "plain:-DMCS_COVER_ART=OFF" "coverart:-DMCS_COVER_ART=ON -DMCS_TEST_METHODS=ON"; do
	vname=${variant%%:*}; vopts=${variant#*:}
	for cfg in "asan-gcc gcc address,undefined -" \
	           "asan-clang clang address,undefined,integer,nullability unsigned-integer-overflow,unsigned-shift-base" \
	           "tsan-gcc gcc thread -" "tsan-clang clang thread -"; do
		# shellcheck disable=SC2086
		set -- $cfg
		build "$1-$vname" "$2" "$3" "$4" "$vopts" && run "$1-$vname" "$WORK/$1-$vname/tests/mcs-tests"
	done
done

# valgrind on the image toolchain's build, run through the sysroot's loader.
if command -v valgrind >/dev/null 2>&1; then
	T=$(ls -d "$N"/usr/bin/*-webos-linux* | head -n 1)
	echo "== build valgrind (image toolchain)"
	if PATH=$T:$N/usr/bin:$PATH cmake -S "$SRC" -B "$WORK/yocto" -G Ninja \
			-DCMAKE_TOOLCHAIN_FILE="$W/toolchain.cmake" \
			-DWEBOS_INSTALL_ROOT=/ \
			-DBUILD_TESTING=ON -DMCS_COVER_ART=ON -DMCS_TEST_METHODS=ON >"$WORK/yocto-configure.log" 2>&1 \
		&& PATH=$T:$N/usr/bin:$PATH ninja -C "$WORK/yocto" mcs-tests >"$WORK/yocto-build.log" 2>&1; then
		run valgrind valgrind --error-exitcode=99 --leak-check=full --errors-for-leak-kinds=definite \
			--show-leak-kinds=definite --track-origins=yes \
			"$S/usr/lib/ld-linux-x86-64.so.2" --library-path "$S/usr/lib" "$WORK/yocto/tests/mcs-tests"
	else
		echo "FAIL build valgrind"; fail=1
	fi
else
	echo "== valgrind: not installed, skipped"
fi

exit $fail
