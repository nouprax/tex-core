# Convenience wrapper around the single CMake/CTest graph. This Makefile
# never implements a second test or benchmark runner: `make test` runs the
# CTest `correctness` preset, `make bench` runs the CTest `benchmark` preset,
# and the sanitizer targets reuse the same graph through their presets.

BUILDDIR=build/cmake
ASAN_BUILDDIR=build/asan
UBSAN_BUILDDIR=build/ubsan
TSAN_BUILDDIR=build/tsan
FUZZ_BUILDDIR=build/fuzz
# The fuzz campaign needs a clang whose runtime ships libFuzzer; Apple's
# Xcode clang does not, so point FUZZ_CC at an LLVM install when needed
# (e.g. FUZZ_CC=/opt/homebrew/opt/llvm/bin/clang).
FUZZ_CC?=cc
TEX_CORE_FUZZ=$(FUZZ_BUILDDIR)/packages/tex-core/fuzz/tex-core-fuzz

.PHONY: all build test conformance bench asan-test ubsan-test tsan-test install clean distclean libFuzzer

all: build

build:
	cmake --preset default
	cmake --build --preset default --parallel

test: build
	ctest --preset correctness

conformance: build
	ctest --preset conformance

bench: build
	ctest --preset benchmark

asan-test:
	cmake --preset asan
	cmake --build --preset asan --parallel
	ctest --preset correctness-asan

ubsan-test:
	cmake --preset ubsan
	cmake --build --preset ubsan --parallel
	ctest --preset correctness-ubsan

tsan-test:
	cmake --preset tsan
	cmake --build --preset tsan --parallel
	ctest --preset correctness-tsan

install: build
	cmake --install $(BUILDDIR)

# Explicit, non-default fuzz campaign: configures the same CMake graph into
# a dedicated ASan build tree and runs a bounded smoke campaign. Findings
# land in the build tree only; longer campaigns rerun the binary with their
# own options.
libFuzzer:
	cmake -S . -B $(FUZZ_BUILDDIR) -DCMAKE_BUILD_TYPE=Asan -DCMAKE_C_COMPILER=$(FUZZ_CC) \
	    -DTEX_CORE_SHARED=OFF -DTEX_CORE_TESTS=OFF -DTEX_CORE_LIB_FUZZER=ON
	cmake --build $(FUZZ_BUILDDIR) --parallel --target tex-core-fuzz
	$(TEX_CORE_FUZZ) -runs=20000 -max_len=512

clean:
	rm -rf $(BUILDDIR) $(ASAN_BUILDDIR) $(UBSAN_BUILDDIR) $(TSAN_BUILDDIR) $(FUZZ_BUILDDIR)

distclean: clean
	rm -rf build
