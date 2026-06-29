#!/bin/bash -eu

cd "$SRC"

# Compile all source files into object files
SRCS=$(find src -name '*.c')
for f in $SRCS; do
  $CC $CFLAGS -Isrc -c -o "${f%.c}.o" "$f"
done

# Build static library
ar rcs libstreamux.a $(find src -name '*.o')

# Build each harness
for harness in channel_fuzzer session_fuzzer stream_fuzzer migrate_fuzzer; do
  $CXX $CXXFLAGS -Isrc -c -o "fuzz/${harness}.o" "fuzz/${harness}.cc"
  $CXX $CXXFLAGS -Isrc -o "$OUT/${harness}" "fuzz/${harness}.o" \
    libstreamux.a $LIB_FUZZING_ENGINE -lm
done

# Copy seed corpus
for harness in channel_fuzzer session_fuzzer stream_fuzzer migrate_fuzzer; do
  if [ -d "fuzz/corpus/${harness}" ]; then
    mkdir -p "$OUT/${harness}_corpus"
    cp fuzz/corpus/${harness}/* "$OUT/${harness}_corpus/" 2>/dev/null || true
  fi
done
