#!/usr/bin/env bash
# One-click fbthrift build via getdeps.
#
# Dependency cache (ALL third-party tarballs, including double-conversion /
# gflags / glog / folly / etc.) lives under one tree:
#   ${GETDEPS_SCRATCH:-<repo>/third_party/getdeps}/
#     downloads/   # source archives; getdeps reuses if present and sha256 matches
#     extracted/   # unpacked sources
#     build/       # object files
#     installed/   # per-dep install prefixes
#
# Multiple clones can share one cache: GETDEPS_SCRATCH=/data/fbdeps ./build.sh
#
# Prefetch below only runs curl when the target file is missing (same filenames
# getdeps uses: manifest_name + "-" + basename(url path)).
#
# Options:
#   --allow-system-packages: prefer OS packages when getdeps can satisfy a dep.
#   --no-tests: skip building fbthrift tests.
#
# Env overrides:
#   GETDEPS_SCRATCH=/path   cache root (default: <repo>/third_party/getdeps)
#   JOBS=8                  parallel jobs (default: min(nproc, 16); folly needs RAM)
#   GETDEPS_CLEAN=1         pass --clean to getdeps (wipe scratch, full rebuild)
#   USE_CLANG16=1           use Clang 16 (default: on if CLANG16_ROOT exists)
#   USE_CLANG16=0           force system GCC
#   CLANG16_ROOT=/path      default: /home/gxt/clang-16

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

PATCHES_DIR="$ROOT_DIR/patches"
FBCODE_BUILDER="$ROOT_DIR/build/fbcode_builder"
GETDEPS_PY="$FBCODE_BUILDER/getdeps.py"
SCRATCH="${GETDEPS_SCRATCH:-$ROOT_DIR/third_party/getdeps}"
DOWNLOADS="$SCRATCH/downloads"
REPOS="$SCRATCH/repos"

# Restore getdeps toolchain if missing, then apply patches/ overlays (openEuler, xxhash, …).
ensure_fbcode_builder() {
  if [[ ! -f "$GETDEPS_PY" ]]; then
    if [[ -d "$ROOT_DIR/.git" ]]; then
      echo "[getdeps] missing $GETDEPS_PY; restoring build/fbcode_builder from git..."
      git -C "$ROOT_DIR" checkout HEAD -- build/fbcode_builder
    fi
  fi
  if [[ ! -f "$GETDEPS_PY" ]]; then
    echo "ERROR: $GETDEPS_PY not found."
    echo "  Use a full fbthrift clone that includes build/fbcode_builder/ (getdeps.py)."
    exit 1
  fi
  apply_build_patches
}

apply_build_patches() {
  local p overlay
  for p in "$PATCHES_DIR"/fbcode_builder/*.patch; do
    [[ -f "$p" ]] || continue
    if patch -p1 -d "$ROOT_DIR" --forward --dry-run --silent -i "$p" 2>/dev/null; then
      patch -p1 -d "$ROOT_DIR" --forward --silent -i "$p"
      echo "[patch] $(basename "$p")"
    else
      echo "[patch] skip (already applied): $(basename "$p")"
    fi
  done
  overlay="$PATCHES_DIR/fbcode_builder/overlay"
  if [[ -d "$overlay" ]]; then
    echo "[patch] overlay -> build/fbcode_builder"
    cp -a "$overlay"/. "$FBCODE_BUILDER/"
  fi
  overlay="$PATCHES_DIR/deps/overlay"
  if [[ -d "$overlay" ]]; then
    echo "[patch] overlay -> build/deps"
    mkdir -p "$ROOT_DIR/build/deps"
    cp -a "$overlay"/. "$ROOT_DIR/build/deps/"
  fi
  # Cloned fbthrift source may lack FindXxhash.cmake on older pins.
  local fbthrift_repo="$REPOS/github.com-facebook-fbthrift.git"
  if [[ -f "$PATCHES_DIR/fbcode_builder/overlay/CMake/FindXxhash.cmake" ]]; then
    if [[ -d "$fbthrift_repo/build/fbcode_builder/CMake" ]]; then
      cp -f "$PATCHES_DIR/fbcode_builder/overlay/CMake/FindXxhash.cmake" \
        "$fbthrift_repo/build/fbcode_builder/CMake/"
    fi
  fi
}

ensure_fbcode_builder

# Git 2.35+ rejects repos owned by another user ("dubious ownership"), e.g. cache
# populated by root. Trust getdeps checkouts for this process only (not ~/.gitconfig).
run_getdeps() {
  GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=safe.directory GIT_CONFIG_VALUE_0='*' \
    python3 "$GETDEPS_PY" "$@"
}

prepare_getdeps_git_repos() {
  [[ -d "$REPOS" ]] || return 0
  local uid repo ouid
  uid="$(id -u)"
  for repo in "$REPOS"/*; do
    [[ -d "$repo/.git" ]] || continue
    ouid="$(stat -c '%u' "$repo" 2>/dev/null)" || continue
    if [[ "$ouid" == "$uid" ]]; then
      continue
    fi
    if [[ "$EUID" -eq 0 && -n "${SUDO_UID:-}" ]]; then
      echo "[git] chown $repo -> uid $SUDO_UID"
      chown -R "$SUDO_UID:$SUDO_GID" "$repo"
    else
      echo "[git] warning: $repo owned by uid $ouid (you are $uid)"
      echo "[git] fix: sudo chown -R $(id -un):$(id -gn) $REPOS"
    fi
  done
}

if J="$(nproc 2>/dev/null)"; then
  :
elif J="$(sysctl -n hw.ncpu 2>/dev/null)"; then
  :
else
  J=4
fi
JOBS="${JOBS:-$J}"
if [[ "$JOBS" -gt 16 ]]; then
  JOBS=16
fi

# Folly assumes x86_64 when CMAKE_LIBRARY_ARCHITECTURE is empty (enables -msse4.2).
MACHINE_ARCH="$(uname -m)"
CLANG16_ROOT="${CLANG16_ROOT:-/home/gxt/clang-16}"
USE_CLANG16="${USE_CLANG16:-}"
if [[ -z "$USE_CLANG16" && -x "$CLANG16_ROOT/bin/clang" ]]; then
  USE_CLANG16=1
fi

# openEuler aarch64: -lunwind often resolves to libunwind.so.1 while folly's
# StackTrace needs unw_backtrace from libunwind.so.8 ("DSO missing from command line").
detect_unwind_ldflags() {
  local arch="$1"
  local libdir="/usr/lib64"
  [[ -d /usr/lib/aarch64-linux-gnu ]] && libdir="/usr/lib/aarch64-linux-gnu"
  local libs=()
  if [[ -f "$libdir/libunwind.so.8" ]]; then
    libs+=("$libdir/libunwind.so.8")
  fi
  if [[ "$arch" == aarch64* && -f "$libdir/libunwind-aarch64.so.8" ]]; then
    libs+=("$libdir/libunwind-aarch64.so.8")
  fi
  if [[ ${#libs[@]} -gt 0 ]]; then
    printf '%s ' '-Wl,--no-as-needed' "${libs[@]}" '-latomic'
  else
    printf '%s' '-Wl,--no-as-needed -lunwind -latomic'
  fi
  # Non-PIC static boost .a + default PIE link fails on aarch64; allow non-PIC .a.
  if [[ "$arch" == aarch64* ]]; then
    printf '%s' ' -no-pie'
  fi
}

if [[ "$USE_CLANG16" == "1" && -x "$CLANG16_ROOT/bin/clang" ]]; then
  export CC="$CLANG16_ROOT/bin/clang"
  export CXX="$CLANG16_ROOT/bin/clang++"
  echo "[compiler] Clang 16: $CXX"
  UNWIND_LDFLAGS="$(detect_unwind_ldflags "$MACHINE_ARCH")"
  echo "[link] unwind: $UNWIND_LDFLAGS"
  # Only pass unwind via CMake flags. Do NOT export LDFLAGS globally: it breaks
  # ninja/bootstrap when the host lacks the libunwind soname ninja was linked against.
  EXTRA_CMAKE_DEFINES=$(
    printf '{"CMAKE_LIBRARY_ARCHITECTURE":"%s","CMAKE_POSITION_INDEPENDENT_CODE":"ON","CMAKE_EXE_LINKER_FLAGS":"%s","CMAKE_SHARED_LINKER_FLAGS":"%s","CMAKE_MODULE_LINKER_FLAGS":"%s","THRIFT_RPC":"OFF","THRIFT_BENCHMARKS":"OFF"}' \
      "$MACHINE_ARCH" "$UNWIND_LDFLAGS" "$UNWIND_LDFLAGS" "$UNWIND_LDFLAGS"
  )
else
  unset CC CXX
  echo "[compiler] system $(command -v g++ || command -v c++)"
  EXTRA_CMAKE_DEFINES=$(
    printf '{"CMAKE_CXX_FLAGS":"-Wno-class-memaccess","CMAKE_LIBRARY_ARCHITECTURE":"%s","CMAKE_POSITION_INDEPENDENT_CODE":"ON","THRIFT_RPC":"OFF","THRIFT_BENCHMARKS":"OFF"}' \
      "$MACHINE_ARCH"
  )
fi

# Boost: b2 must use -fPIC; wipe cached objects if manifest flags change.
BOOST_FPIC_STAMP="$SCRATCH/installed/.boost_fpic_v2"
if [[ ! -f "$BOOST_FPIC_STAMP" ]]; then
  echo "[boost] clean rebuild with cflags/cxxflags/linkflags=-fPIC"
  rm -rf "$SCRATCH"/extracted/boost-* "$SCRATCH"/build/boost-* "$SCRATCH"/installed/boost-*
  rm -f "$SCRATCH/installed/.boost_built_with_fpic"
  rm -rf "$SCRATCH/build/fbthrift"
  mkdir -p "$SCRATCH/installed"
  touch "$BOOST_FPIC_STAMP"
fi
echo "[cmake] THRIFT_RPC=OFF (skip mvfst/fizz/wangle at configure time)"

# Reconfigure folly if compiler or unwind link flags changed.
FOLLY_CACHE="$SCRATCH/build/folly/CMakeCache.txt"
if [[ -f "$FOLLY_CACHE" ]]; then
  WIPE_FOLLY=
  if [[ -n "${CXX:-}" ]]; then
    OLD_CXX="$(sed -n 's|^CMAKE_CXX_COMPILER:FILEPATH=||p' "$FOLLY_CACHE" | head -1)"
    if [[ -n "$OLD_CXX" && "$OLD_CXX" != "$CXX" ]]; then
      echo "[compiler] folly was built with $OLD_CXX, wiping for $CXX"
      WIPE_FOLLY=1
    fi
  fi
  if [[ -n "${UNWIND_LDFLAGS:-}" ]]; then
    OLD_LDFLAGS="$(sed -n 's|^CMAKE_EXE_LINKER_FLAGS:STRING=||p' "$FOLLY_CACHE" | head -1)"
    if [[ -n "$OLD_LDFLAGS" && "$OLD_LDFLAGS" != "$UNWIND_LDFLAGS" ]]; then
      echo "[link] folly linker flags changed, wiping folly build"
      WIPE_FOLLY=1
    fi
  fi
  if [[ -n "$WIPE_FOLLY" ]]; then
    rm -rf "$SCRATCH/build/folly" "$SCRATCH/installed/folly"
  fi
fi

mkdir -p "$DOWNLOADS"

# Heal ninja if a prior run linked it against missing libunwind.so.1 (bad LDFLAGS).
for _nb in "$SCRATCH"/build/ninja-*/ninja "$SCRATCH"/extracted/ninja-*/ninja-*/ninja; do
  if [[ -x "$_nb" ]] && ! "$_nb" --version &>/dev/null; then
    echo "[ninja] removing broken ninja: $_nb"
    rm -rf "$SCRATCH"/build/ninja-* "$SCRATCH"/installed/ninja-* "$SCRATCH"/extracted/ninja-*
    break
  fi
done
unset _nb

# fizz needs cmake >= 3.20.4. getdeps used to build 3.20.2 on openEuler because
# platform.py did not treat openeuler as rpm (fixed there). Prefer /usr/bin/cmake.
CMAKE_INSTALL_ARGS=()
if SYS_CMAKE_VER="$(cmake --version 2>/dev/null | sed -n 's/cmake version //p' | head -1)"; then
  if printf '3.20.4\n%s\n' "$SYS_CMAKE_VER" | sort -V -C 2>/dev/null; then
    echo "[cmake] use system cmake $SYS_CMAKE_VER"
    rm -rf "$SCRATCH"/installed/cmake-* "$SCRATCH"/build/cmake-* 2>/dev/null || true
    CMAKE_INSTALL_ARGS=(--install-dir "cmake:/usr")
  fi
fi

prefetch_if_missing() {
  local url="$1" dest="$2"
  if [[ -f "$dest" ]] && [[ -s "$dest" ]]; then
    echo "[cache] skip: $(basename "$dest")"
    return 0
  fi
  echo "[fetch] $(basename "$dest")"
  curl -fL --retry 5 --retry-delay 3 --connect-timeout 30 "$url" -o "$dest.tmp" && mv -f "$dest.tmp" "$dest"
}

echo "[1/3] Warm download cache (only if missing): $DOWNLOADS"
# Names must match getdeps ArchiveFetcher: downloads/<manifest>-<basename(url)>
prefetch_if_missing "https://www.zlib.net/fossils/zlib-1.2.13.tar.gz" \
  "$DOWNLOADS/zlib-zlib-1.2.13.tar.gz" || true
prefetch_if_missing "https://github.com/facebook/zstd/releases/download/v1.4.5/zstd-1.4.5.tar.gz" \
  "$DOWNLOADS/zstd-zstd-1.4.5.tar.gz" || true
prefetch_if_missing "https://github.com/fmtlib/fmt/archive/refs/tags/8.0.1.tar.gz" \
  "$DOWNLOADS/fmt-8.0.1.tar.gz" || true
prefetch_if_missing "https://archives.boost.io/release/1.78.0/source/boost_1_78_0.tar.gz" \
  "$DOWNLOADS/boost-boost_1_78_0.tar.gz" || true
prefetch_if_missing "https://github.com/google/double-conversion/archive/v3.1.4.tar.gz" \
  "$DOWNLOADS/double-conversion-v3.1.4.tar.gz" || true
prefetch_if_missing "https://github.com/gflags/gflags/archive/v2.2.2.tar.gz" \
  "$DOWNLOADS/gflags-v2.2.2.tar.gz" || true
prefetch_if_missing "https://github.com/google/glog/archive/v0.5.0.tar.gz" \
  "$DOWNLOADS/glog-v0.5.0.tar.gz" || true
prefetch_if_missing "https://github.com/jedisct1/libsodium/releases/download/1.0.18-RELEASE/libsodium-1.0.18.tar.gz" \
  "$DOWNLOADS/libsodium-libsodium-1.0.18.tar.gz" || true
prefetch_if_missing "https://github.com/Cyan4973/xxHash/archive/refs/tags/v0.8.3.tar.gz" \
  "$DOWNLOADS/xxhash-v0.8.3.tar.gz" || true

# CI image build: FBTHRIFT_PREFETCH_ONLY=1 ./build.sh  (or ci/prefetch-deps.sh)
if [[ "${FBTHRIFT_PREFETCH_ONLY:-0}" == "1" ]]; then
  echo "[prefetch] getdeps fetch --recursive fbthrift -> $SCRATCH"
  run_getdeps fetch --recursive fbthrift \
    --scratch-path "$SCRATCH" \
    --allow-system-packages
  echo "[prefetch] done. downloads: $DOWNLOADS"
  exit 0
fi

CLEAN_ARGS=()
if [[ "${GETDEPS_CLEAN:-0}" == "1" ]]; then
  CLEAN_ARGS=(--clean)
fi

prepare_getdeps_git_repos

# Free space: old github clone + cargo copy of full tree (including .git).
if [[ -d "$REPOS/github.com-facebook-fbthrift.git" ]]; then
  echo "[cleanup] removing unused github fbthrift clone: $REPOS/github.com-facebook-fbthrift.git"
  rm -rf "$REPOS/github.com-facebook-fbthrift.git"
fi
if [[ -d "$SCRATCH/build/fbthrift/source" ]]; then
  echo "[cleanup] removing cargo source mirror (was duplicate of repo + .git)"
  rm -rf "$SCRATCH/build/fbthrift/source"
fi

# Build fbthrift from THIS repo (--src-dir). Manifest [cargo] is disabled so getdeps
# does not copytree $ROOT_DIR -> build/fbthrift/source (that duplicated .git and OOMed disk).
echo "[src] fbthrift sources: $ROOT_DIR"
echo "[2/3] Build fbthrift (incremental unless GETDEPS_CLEAN=1)..."
run_getdeps build fbthrift \
  "${CLEAN_ARGS[@]}" \
  --scratch-path "$SCRATCH" \
  --src-dir "$ROOT_DIR" \
  --num-jobs "$JOBS" \
  --no-tests \
  --allow-system-packages \
  "${CMAKE_INSTALL_ARGS[@]}" \
  --extra-cmake-defines "$EXTRA_CMAKE_DEFINES"

echo "[3/3] Done."
echo "Scratch (deps + build cache): $SCRATCH"
echo "Tarball cache: $DOWNLOADS"
echo "Installed prefixes: $SCRATCH/installed/"
