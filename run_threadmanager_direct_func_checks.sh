#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  run_threadmanager_direct_func_checks.sh [ut|coverage|all]

Modes:
  ut        Build with Clang 16 and run the ThreadManager direct-func UTs.
  coverage  Build with coverage instrumentation, run the UTs, and calculate
            incremental line coverage for the optimization commit.
  all       Run both steps. This is the default.

Optional environment variables:
  DIRECT_FUNC_COMMIT             Optimization commit, default: 0c5106f
  MIN_INCREMENTAL_COVERAGE       Required percentage, default: 80
  FBTHRIFT_UT_BUILD_DIR          Normal UT build directory
  FBTHRIFT_COVERAGE_BUILD_DIR    Coverage build directory
  FBTHRIFT_CLANG16_CXX           Normal compiler
  FBTHRIFT_COVERAGE_CXX          Coverage compiler; must include profile runtime
  FBTHRIFT_FOLLY_PREFIX          Installed Folly prefix
  FBTHRIFT_FIZZ_PREFIX           Installed Fizz prefix
  FBTHRIFT_WANGLE_PREFIX         Installed Wangle prefix
  FBTHRIFT_INSTALL_PREFIX        Installed fbthrift prefix

Examples:
  ./run_threadmanager_direct_func_checks.sh ut
  ./run_threadmanager_direct_func_checks.sh coverage
  MIN_INCREMENTAL_COVERAGE=85 ./run_threadmanager_direct_func_checks.sh all
EOF
}

mode="${1:-all}"
case "${mode}" in
  ut|coverage|all) ;;
  -h|--help)
    usage
    exit 0
    ;;
  *)
    usage >&2
    exit 2
    ;;
esac

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo="$(git -C "${script_dir}" rev-parse --show-toplevel)"

direct_func_commit="${DIRECT_FUNC_COMMIT:-0c5106f}"
minimum_coverage="${MIN_INCREMENTAL_COVERAGE:-80}"
relative_source=thrift/lib/cpp/concurrency/ThreadManager.cpp
source_file="${repo}/${relative_source}"
test_source="${repo}/thrift/lib/cpp/concurrency/test/ThreadManagerDirectFuncTest.cpp"
test_target=thread_manager_direct_func-t
test_filter='ThreadManagerDirectFuncTest'

ut_build="${FBTHRIFT_UT_BUILD_DIR:-/tmp/fbthrift-threadmanager-direct-func-ut}"
coverage_build="${FBTHRIFT_COVERAGE_BUILD_DIR:-/tmp/fbthrift-threadmanager-direct-func-coverage}"

clang16_cxx="${FBTHRIFT_CLANG16_CXX:-/home/donghuan/clang-16/bin/clang++}"
coverage_cxx="${FBTHRIFT_COVERAGE_CXX:-/usr/bin/clang++}"
llvm_profdata="${LLVM_PROFDATA:-/usr/bin/llvm-profdata}"
llvm_cov="${LLVM_COV:-/usr/bin/llvm-cov}"

folly_prefix="${FBTHRIFT_FOLLY_PREFIX:-/home/donghuan/ins/folly}"
fizz_prefix="${FBTHRIFT_FIZZ_PREFIX:-/home/donghuan/ins/fizz}"
wangle_prefix="${FBTHRIFT_WANGLE_PREFIX:-/home/donghuan/ins/wangle}"
fbthrift_prefix="${FBTHRIFT_INSTALL_PREFIX:-/home/donghuan/ins/fbthrift}"

runtime_library_path="${folly_prefix}/lib:${fizz_prefix}/lib:${wangle_prefix}/lib:${fbthrift_prefix}/lib"
if [[ -n "${LD_LIBRARY_PATH:-}" ]]; then
  runtime_library_path="${runtime_library_path}:${LD_LIBRARY_PATH}"
fi

require_executable() {
  if [[ ! -x "$1" ]]; then
    echo "Required executable not found: $1" >&2
    exit 1
  fi
}

require_executable "${clang16_cxx}"
require_executable "${coverage_cxx}"
require_executable "${llvm_profdata}"
require_executable "${llvm_cov}"

git -C "${repo}" rev-parse --verify "${direct_func_commit}^{commit}" >/dev/null

common_cmake_args=(
  -DCMAKE_BUILD_TYPE=Debug
  "-DCMAKE_PREFIX_PATH=${folly_prefix};${fizz_prefix};${wangle_prefix}"
  "-Dfizz_DIR=${fizz_prefix}/lib/cmake/fizz"
  "-Dwangle_DIR=${wangle_prefix}/lib/cmake/wangle"
  -Denable_tests=ON
  -Dlib_only=ON
  -Dthriftpy=OFF
  -Dthriftpy3=OFF
  "-DTHRIFT_COMPILER_INCLUDE=${fbthrift_prefix}/include"
)

run_ut() {
  echo "== Configure normal UT build =="
  cmake -S "${repo}" -B "${ut_build}" \
    "${common_cmake_args[@]}" \
    "-DCMAKE_CXX_COMPILER=${clang16_cxx}"

  echo "== Build ${test_target} =="
  cmake --build "${ut_build}" --target "${test_target}" -j"$(nproc)"

  echo "== Run ThreadManager direct-func UTs =="
  LD_LIBRARY_PATH="${runtime_library_path}" \
    ctest --test-dir "${ut_build}" \
      --output-on-failure \
      -R "${test_filter}"
}

calculate_incremental_coverage() {
  local binary="$1"
  local profile="$2"
  local artifacts="$3"

  git -C "${repo}" diff --unified=0 \
    "${direct_func_commit}^" "${direct_func_commit}" -- \
    "${relative_source}" |
    awk '
      /^@@/ {
        if (match($0, /\+([0-9]+)(,([0-9]+))?/, range)) {
          start = range[1]
          count = range[3] == "" ? 1 : range[3]
          for (offset = 0; offset < count; ++offset) {
            print start + offset
          }
        }
      }
    ' > "${artifacts}/added-lines.txt"

  "${llvm_cov}" show "${binary}" \
    -instr-profile="${profile}" \
    -show-line-counts-or-regions \
    -show-expansions=false \
    -show-instantiations=false \
    "${source_file}" |
    awk -F '|' '
      NF >= 3 {
        line = $1
        count = $2
        gsub(/[[:space:]]/, "", line)
        gsub(/[[:space:],]/, "", count)
        if (line ~ /^[0-9]+$/ && count ~ /^[0-9]+$/) {
          print line, count
        }
      }
    ' > "${artifacts}/line-counts.txt"

  awk '
    FNR == NR {
      added[$1] = 1
      changed++
      next
    }
    $1 in added {
      executable++
      if ($2 > 0) {
        covered++
      } else {
        missed++
        missed_lines = missed_lines (missed_lines == "" ? "" : ",") $1
      }
    }
    END {
      printf "DIRECT_FUNC_COMMIT=%s\n", commit
      printf "CHANGED_LINES=%d\n", changed
      printf "EXECUTABLE_CHANGED_LINES=%d\n", executable
      printf "COVERED_CHANGED_LINES=%d\n", covered
      printf "MISSED_CHANGED_LINES=%d\n", missed
      printf "INCREMENTAL_LINE_COVERAGE=%.2f%%\n", \
        executable == 0 ? 100 : covered * 100 / executable
      printf "MISSED_EXECUTABLE_LINES=%s\n", missed_lines
    }
  ' commit="${direct_func_commit}" \
    "${artifacts}/added-lines.txt" \
    "${artifacts}/line-counts.txt" |
    tee "${artifacts}/incremental-coverage.txt"

  awk '
    FILENAME == ARGV[1] {
      added[$1] = 1
      next
    }
    FILENAME == ARGV[2] {
      if ($1 in added) {
        status[$1] = $2 > 0 ? "COVERED" : "MISSED"
        count[$1] = $2
      }
      next
    }
    FILENAME == ARGV[3] && FNR in status {
      printf "%-7s line=%-5d count=%-5d %s\n", \
        status[FNR], FNR, count[FNR], $0
    }
  ' "${artifacts}/added-lines.txt" \
    "${artifacts}/line-counts.txt" \
    "${source_file}" > "${artifacts}/incremental-lines.txt"

  "${llvm_cov}" report "${binary}" \
    -instr-profile="${profile}" \
    "${source_file}" "${test_source}" \
    > "${artifacts}/full-report.txt"

  "${llvm_cov}" show "${binary}" \
    -instr-profile="${profile}" \
    -format=html \
    -output-dir="${artifacts}/html" \
    "${source_file}" "${test_source}" >/dev/null

  local actual_coverage
  actual_coverage="$(
    awk -F= '
      $1 == "INCREMENTAL_LINE_COVERAGE" {
        gsub(/%/, "", $2)
        print $2
      }
    ' "${artifacts}/incremental-coverage.txt"
  )"

  if ! awk -v actual="${actual_coverage}" -v required="${minimum_coverage}" \
    'BEGIN { exit !(actual + 0 >= required + 0) }'; then
    echo "Incremental coverage ${actual_coverage}% is below ${minimum_coverage}%" >&2
    return 1
  fi

  echo "Incremental coverage gate passed: ${actual_coverage}% >= ${minimum_coverage}%"
}

run_coverage() {
  echo "== Configure coverage build =="
  cmake -S "${repo}" -B "${coverage_build}" \
    "${common_cmake_args[@]}" \
    "-DCMAKE_CXX_COMPILER=${coverage_cxx}" \
    '-DCMAKE_CXX_FLAGS=-fprofile-instr-generate -fcoverage-mapping' \
    '-DCMAKE_EXE_LINKER_FLAGS=-fprofile-instr-generate'

  echo "== Build instrumented ${test_target} =="
  cmake --build "${coverage_build}" --target "${test_target}" -j"$(nproc)"

  local binary="${coverage_build}/bin/${test_target}"
  local artifacts
  artifacts="$(mktemp -d "${coverage_build}/coverage-artifacts.XXXXXX")"

  echo "== Run instrumented UTs =="
  LLVM_PROFILE_FILE="${artifacts}/direct-func-%p.profraw" \
    LD_LIBRARY_PATH="${runtime_library_path}" \
    "${binary}" --gtest_color=no

  "${llvm_profdata}" merge -sparse \
    "${artifacts}"/direct-func-*.profraw \
    -o "${artifacts}/direct-func.profdata"

  echo "== Calculate incremental line coverage =="
  calculate_incremental_coverage \
    "${binary}" \
    "${artifacts}/direct-func.profdata" \
    "${artifacts}"

  echo "Coverage artifacts: ${artifacts}"
  echo "Summary:            ${artifacts}/incremental-coverage.txt"
  echo "Changed-line detail: ${artifacts}/incremental-lines.txt"
  echo "Full report:         ${artifacts}/full-report.txt"
  echo "HTML report:         ${artifacts}/html/index.html"
}

case "${mode}" in
  ut)
    run_ut
    ;;
  coverage)
    run_coverage
    ;;
  all)
    run_ut
    run_coverage
    ;;
esac
