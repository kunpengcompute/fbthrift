#!/bin/bash
set -u -o pipefail
export PATH="/usr/local/bin:/usr/bin:/bin:/usr/local/sbin:/usr/sbin:/sbin:${PATH:-}"
if ! command -v git >/dev/null 2>&1; then apt-get update 2>&1 || true; apt-get install -y -qq git 2>/dev/null; fi
BASE_SHA="${1:-HEAD~1}"
MIN_COVERAGE="${2:-80}"
TEST_TARGETS_ARG="${3:-}"
SRC_DIR="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
HEAD_SHA="$(git rev-parse HEAD 2>/dev/null || echo 'unknown')"
BUILD_DIR="build_"
echo "============================================"
echo "  fbthrift 增量覆盖率检查（全自动）"
echo "============================================"
echo "  源码:   $SRC_DIR"
echo "  基线:   ${BASE_SHA:0:12}"
echo "  当前:   ${HEAD_SHA:0:12}"
echo "  阈值:   ${MIN_COVERAGE}%"
echo "============================================"
echo ""
echo "=== 换源 ==="
cat > /etc/apt/sources.list << 'EOF'
deb https://mirrors.tuna.tsinghua.edu.cn/debian bookworm main contrib non-free non-free-firmware
deb https://mirrors.tuna.tsinghua.edu.cn/debian bookworm-updates main contrib non-free non-free-firmware
deb http://mirrors.aliyun.com/debian-security bookworm-security main contrib non-free non-free-firmware
EOF
apt-get update 2>&1 || true
if ! grep -q 'gh-proxy.test.osinfra.cn' /root/.gitconfig 2>/dev/null; then
  cat >> /root/.gitconfig << 'EOF'
[url "https://gh-proxy.test.osinfra.cn/https://github.com/"]
insteadOf = https://github.com/
EOF
fi
echo "  源已切换"
echo ""
echo "=== Step 0: 安装依赖 ==="
apt-get update 2>&1 || true
for pkg in libboost-all-dev libdouble-conversion-dev libgflags-dev libgoogle-glog-dev libevent-dev libsodium-dev liblz4-dev libsnappy-dev liblzma-dev libgtest-dev libgmock-dev libssl-dev libaio-dev make clang-16 cmake clang libunwind-dev libdwarf-dev binutils-dev libiberty-dev zlib1g-dev libbz2-dev unzip libzstd-dev libfmt-dev python3 python3-six cython3 libxxhash-dev; do
  if ! apt-get install -y --fix-missing "$pkg" >/tmp/apt_install.log 2>&1; then
    apt-get update 2>&1 || true
    apt-get install -y --fix-missing "$pkg" >/tmp/apt_install.log 2>&1 || echo "  警告: $pkg 安装失败"
  fi
done
export CC=/usr/bin/clang-16
export CXX=/usr/bin/clang++-16
if [ ! -e /usr/bin/clang ]; then ln -sf /usr/bin/clang-16 /usr/bin/clang 2>/dev/null || true; fi
if [ ! -e /usr/bin/clang++ ]; then ln -sf /usr/bin/clang++-16 /usr/bin/clang++ 2>/dev/null || true; fi
PROFDATA=""
COV=""
for v in 16 18 17 14 ""; do
  if command -v "llvm-profdata-$v" >/dev/null 2>&1; then PROFDATA="llvm-profdata-$v"; COV="llvm-cov-$v"; break
  elif [ -z "$v" ] && command -v llvm-profdata >/dev/null 2>&1; then PROFDATA="llvm-profdata"; COV="llvm-cov"; break; fi
done
if [ -z "$PROFDATA" ]; then
  apt-get install -y -qq llvm 2>/dev/null
  for v in 16 18 17 14 ""; do
    if command -v "llvm-profdata-$v" >/dev/null 2>&1; then PROFDATA="llvm-profdata-$v"; COV="llvm-cov-$v"; break; fi
  done
fi
if ! find /usr -name 'libgtest.a' 2>/dev/null | grep -q .; then
  if [ -d /usr/src/googletest ]; then (cd /usr/src/googletest && cmake . 2>/dev/null && make 2>/dev/null && make install 2>/dev/null); fi
fi
GMOCK_LIB=$(find /usr -name 'libgmock.a' 2>/dev/null | head -1)
GMOCK_MAIN_LIB=$(find /usr -name 'libgmock_main.a' 2>/dev/null | head -1)
GTEST_LIB=$(find /usr -name 'libgtest.a' 2>/dev/null | head -1)
GTEST_MAIN_LIB=$(find /usr -name 'libgtest_main.a' 2>/dev/null | head -1)
GTEST_INCLUDE_DIR=$(find /usr/include -name 'gtest.h' 2>/dev/null | head -1 | xargs dirname 2>/dev/null | xargs dirname 2>/dev/null)
GMOCK_INCLUDE_DIR="${GTEST_INCLUDE_DIR}/gmock"
if [ -n "$GMOCK_LIB" ]; then
  export LIBGMOCK_LIBRARY="$GMOCK_LIB"; export LIBGMOCK_MAIN_LIBRARY="$GMOCK_MAIN_LIB"; export LIBGTEST_LIBRARY="$GTEST_LIB"; export LIBGTEST_MAIN_LIBRARY="$GTEST_MAIN_LIB"; export GMOCK_INCLUDE_DIR="$GMOCK_INCLUDE_DIR"; export GTEST_INCLUDE_DIR="$GTEST_INCLUDE_DIR"
fi
echo "  CC=$CC CXX=$CXX"
echo "  $PROFDATA / $COV"
if [ -z "$PROFDATA" ] || [ -z "$COV" ]; then
  echo "ERROR: llvm-profdata/llvm-cov 未找到"
  echo "RESULT: FAIL"
  exit 1
fi
echo ""
echo "=== Step 1: 增量文件 ==="
resolve_base() {
  if [ -n "${CI_MERGE_REQUEST_DIFF_BASE_SHA:-}" ] && git rev-parse --verify "$CI_MERGE_REQUEST_DIFF_BASE_SHA^{commit}" >/dev/null 2>&1; then BASE_SHA="$CI_MERGE_REQUEST_DIFF_BASE_SHA"; return 0; fi
  if [ -n "${CI_MERGE_REQUEST_TARGET_BRANCH_SHA:-}" ] && git rev-parse --verify "$CI_MERGE_REQUEST_TARGET_BRANCH_SHA^{commit}" >/dev/null 2>&1; then BASE_SHA="$CI_MERGE_REQUEST_TARGET_BRANCH_SHA"; return 0; fi
  git rev-parse --verify "$BASE_SHA^{commit}" >/dev/null 2>&1 && return 0
  git rev-parse --verify "origin/$BASE_SHA^{commit}" >/dev/null 2>&1 && { BASE_SHA="origin/$BASE_SHA"; return 0; }
  git rev-parse --verify "refs/remotes/origin/$BASE_SHA^{commit}" >/dev/null 2>&1 && { BASE_SHA="refs/remotes/origin/$BASE_SHA"; return 0; }
  git rev-parse --verify "refs/remotes/fork/$BASE_SHA^{commit}" >/dev/null 2>&1 && { BASE_SHA="refs/remotes/fork/$BASE_SHA"; return 0; }
  for ref in $(git for-each-ref --format='%(refname:short)' refs/remotes 2>/dev/null); do
    if echo "$ref" | grep -q "/$BASE_SHA$"; then BASE_SHA="$ref"; return 0; fi
  done
  for remote in $(git remote 2>/dev/null); do
    if git fetch "$remote" "$BASE_SHA" 2>/dev/null; then
      git rev-parse --verify "FETCH_HEAD^{commit}" >/dev/null 2>&1 && { BASE_SHA="FETCH_HEAD"; return 0; }
    fi
    git fetch "$remote" 2>/dev/null || true
    git rev-parse --verify "$BASE_SHA^{commit}" >/dev/null 2>&1 && return 0
    git rev-parse --verify "$remote/$BASE_SHA^{commit}" >/dev/null 2>&1 && { BASE_SHA="$remote/$BASE_SHA"; return 0; }
  done
  return 1
}
if ! resolve_base; then
  FALLBACK_INC=$(git -C "$SRC_DIR" diff --name-only "HEAD~1" "$HEAD_SHA" -- '*.cpp' '*.cc' 2>/dev/null | grep -v '/test/' | grep -v '/tests/' | grep -v '/benchmarks/' | grep -v '/tool/' | grep -v '/examples/' || true)
  if [ -z "$FALLBACK_INC" ]; then
    echo "  无法解析基线，但本分支未涉及源码文件修改，跳过覆盖率检查。"
    echo "RESULT: SKIP"
    exit 0
  fi
  echo "ERROR: 基线 $BASE_SHA 不存在"; echo "RESULT: FAIL"; exit 1
fi
INCREMENTAL_CPP=$(git -C "$SRC_DIR" diff --name-only "$BASE_SHA" "$HEAD_SHA" -- '*.cpp' '*.cc' 2>/dev/null | grep -v '/test/' | grep -v '/tests/' | grep -v '/benchmarks/' | grep -v '/tool/' | grep -v '/examples/' || true)
if [ -z "$INCREMENTAL_CPP" ]; then echo "没有增量源文件，跳过。"; echo "RESULT: SKIP"; exit 0; fi
mapfile -t INCREMENTAL_FILES <<< "$INCREMENTAL_CPP"
for f in "${INCREMENTAL_FILES[@]}"; do echo "  $f"; done
ADDED_LINES=$(git -C "$SRC_DIR" diff "$BASE_SHA" "$HEAD_SHA" -- "${INCREMENTAL_FILES[@]}" 2>/dev/null | grep -E '^\+' | grep -vE '^\+\+\+' | wc -l)
if [ "$ADDED_LINES" -eq 0 ]; then echo "  无新增代码行（纯删除），跳过增量覆盖率检查。"; echo "RESULT: SKIP"; exit 0; fi
echo ""
echo "=== Step 2: 构建依赖 ==="
cd "$SRC_DIR"
if [ ! -f /usr/local/lib/libzstd.a ]; then
  echo "  编译 zstd..."
  if ! wget --no-check-certificate -q -O /tmp/zstd.tar.gz https://buildtools.obs.cn-north-4.myhuaweicloud.com/zstd-1.4.5.tar.gz; then echo "ERROR: 下载 zstd 失败"; exit 1; fi
  tar xzf /tmp/zstd.tar.gz && cd zstd-1.4.5
  if ! make -j"$(nproc)" >/tmp/build_zstd.log 2>&1; then tail -3 /tmp/build_zstd.log; echo "ERROR: zstd 编译失败"; exit 1; fi
  if ! make PREFIX=/usr/local/ install 2>&1 | tail -1; then echo "ERROR: install 失败"; exit 1; fi
  cd "$SRC_DIR"
fi
if [ ! -d /usr/local/fmt ]; then
  echo "  编译 fmt..."
  if ! wget --no-check-certificate -q -O /tmp/fmt.tar.gz https://buildtools.obs.cn-north-4.myhuaweicloud.com/fmt-8.0.1.tar.gz; then echo "ERROR: 下载 fmt 失败"; exit 1; fi
  tar xzf /tmp/fmt.tar.gz && cd fmt-8.0.1; mkdir -p build && cd build
  cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local/fmt 2>&1 | tail -3
  if ! make -j"$(nproc)" >/tmp/build_fmt.log 2>&1; then tail -3 /tmp/build_fmt.log; echo "ERROR: fmt 编译失败"; exit 1; fi
  if ! make install 2>&1 | tail -1; then echo "ERROR: install 失败"; exit 1; fi
  cd "$SRC_DIR"
fi
if [ ! -f /usr/local/lib/libz.a ]; then
  echo "  编译 zlib..."
  if ! wget --no-check-certificate -q -O /tmp/zlib.tar.gz https://buildtools.obs.cn-north-4.myhuaweicloud.com/zlib-1.2.13.tar.gz; then echo "ERROR: 下载 zlib 失败"; exit 1; fi
  tar xzf /tmp/zlib.tar.gz && cd zlib-1.2.13
  CFLAGS="-Wno-error" ./configure --prefix=/usr/local/ 2>&1 | tail -3
  if ! make -j"$(nproc)" >/tmp/build_zlib.log 2>&1; then tail -3 /tmp/build_zlib.log; echo "ERROR: zlib 编译失败"; exit 1; fi
  if ! make install 2>&1 | tail -1; then echo "ERROR: install 失败"; exit 1; fi
  cd "$SRC_DIR"
fi
if [ ! -d /usr/local/boost_1_78_0 ]; then
  echo "  编译 boost（需要几分钟）..."
  if ! wget --no-check-certificate -q -O /tmp/boost.tar.gz https://buildtools.obs.cn-north-4.myhuaweicloud.com/boost_1_78_0.tar.gz; then echo "ERROR: 下载 boost 失败"; exit 1; fi
  tar xzf /tmp/boost.tar.gz && cd boost_1_78_0
  ./bootstrap.sh --with-toolset=clang --prefix=/usr/local/boost_1_78_0 2>&1 | tail -3
  if ! ./b2 install >/tmp/build_boost.log 2>&1; then tail -3 /tmp/build_boost.log; echo "ERROR: boost 编译失败"; exit 1; fi
  cd "$SRC_DIR"
fi
if [ ! -d /usr/local/folly ]; then
  echo "  编译 folly..."
  if ! wget --no-check-certificate -q -O /tmp/folly.zip https://buildtools.obs.cn-north-4.myhuaweicloud.com/folly-2022.11.14.00.zip; then echo "ERROR: 下载 folly 失败"; exit 1; fi
  unzip -q /tmp/folly.zip; cd folly-2022.11.14.00; mkdir -p _build && cd _build
  cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=17 -DBUILD_BENCHMARKS=OFF -DBUILD_TESTS=OFF -DCMAKE_INSTALL_PREFIX=/usr/local/folly -DBUILD_SHARED_LIBS=ON -DCMAKE_PREFIX_PATH="/usr/local/fmt/lib/cmake/fmt/" 2>&1 | tail -5
  if ! make -j"$(nproc)" >/tmp/build_folly.log 2>&1; then tail -5 /tmp/build_folly.log; echo "ERROR: folly 编译失败"; exit 1; fi
  if ! make install 2>&1 | tail -1; then echo "ERROR: install 失败"; exit 1; fi
  cd "$SRC_DIR"
fi
if [ ! -d /usr/local/fizz ]; then
  echo "  编译 fizz..."
  if ! wget --no-check-certificate -q -O /tmp/fizz.zip https://buildtools.obs.cn-north-4.myhuaweicloud.com/fizz-2022.11.14.00.zip; then echo "ERROR: 下载 fizz 失败"; exit 1; fi
  unzip -q /tmp/fizz.zip; cd fizz-2022.11.14.00; mkdir -p build_ && cd build_
  cmake ../fizz/ -Dfmt_DIR=/usr/local/fmt/lib/cmake/fmt -Dfolly_DIR=/usr/local/folly/lib/cmake/folly -DCMAKE_INSTALL_PREFIX=/usr/local/fizz 2>&1 | tail -5
  if ! make -j"$(nproc)" >/tmp/build_fizz.log 2>&1; then tail -5 /tmp/build_fizz.log; echo "ERROR: fizz 编译失败"; exit 1; fi
  if ! make install 2>&1 | tail -1; then echo "ERROR: install 失败"; exit 1; fi
  cd "$SRC_DIR"
fi
if [ ! -d /usr/local/wangle ]; then
  echo "  编译 wangle..."
  if ! wget --no-check-certificate -q -O /tmp/wangle.zip https://buildtools.obs.cn-north-4.myhuaweicloud.com/wangle-2022.11.14.00.zip; then echo "ERROR: 下载 wangle 失败"; exit 1; fi
  unzip -q /tmp/wangle.zip; cd wangle-2022.11.14.00; mkdir -p build_ && cd build_
  cmake ../wangle -Dfmt_DIR=/usr/local/fmt/lib/cmake/fmt -Dfolly_DIR=/usr/local/folly/lib/cmake/folly -Dfizz_DIR=/usr/local/fizz/lib/cmake/fizz -DCMAKE_INSTALL_PREFIX=/usr/local/wangle -DBUILD_SHARED_LIBS=ON 2>&1 | tail -5
  if ! make -j"$(nproc)" >/tmp/build_wangle.log 2>&1; then tail -5 /tmp/build_wangle.log; echo "ERROR: wangle 编译失败"; exit 1; fi
  if ! make install 2>&1 | tail -1; then echo "ERROR: install 失败"; exit 1; fi
  cd "$SRC_DIR"
fi
echo "  依赖构建完成"
echo ""
echo "=== Step 3: cmake 配置 fbthrift ==="
rm -rf "$BUILD_DIR"; mkdir -p "$BUILD_DIR"; cd "$BUILD_DIR"
cmake "$SRC_DIR" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER="$CXX" -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_FLAGS="-fprofile-instr-generate -fcoverage-mapping" -DCMAKE_EXE_LINKER_FLAGS="-fprofile-instr-generate -fcoverage-mapping" -DTHRIFT_ENABLE_ARM_SVE2=ON -Denable_tests=ON -Dfmt_DIR=/usr/local/fmt/lib/cmake/fmt -Dfolly_DIR=/usr/local/folly/lib/cmake/folly -Dfizz_DIR=/usr/local/fizz/lib/cmake/fizz -Dwangle_DIR=/usr/local/wangle/lib/cmake/wangle ${GMOCK_LIB:+-DLIBGMOCK_LIBRARY="$GMOCK_LIB"} ${GMOCK_MAIN_LIB:+-DLIBGMOCK_MAIN_LIBRARY="$GMOCK_MAIN_LIB"} ${GTEST_LIB:+-DLIBGTEST_LIBRARY="$GTEST_LIB"} ${GTEST_MAIN_LIB:+-DLIBGTEST_MAIN_LIBRARY="$GTEST_MAIN_LIB"} ${GMOCK_INCLUDE_DIR:+-DGMOCK_INCLUDE_DIR="$GMOCK_INCLUDE_DIR"} ${GTEST_INCLUDE_DIR:+-DGTEST_INCLUDE_DIR="$GTEST_INCLUDE_DIR"} 2>&1 | tee /tmp/cmake_output.log | tail -10
if [ ! -f CMakeCache.txt ] || [ ! -f Makefile ]; then echo "ERROR: cmake 配置失败"; grep -i "error\|fail\|not found\|missing" /tmp/cmake_output.log | head -20; echo "RESULT: FAIL"; exit 1; fi
echo ""
echo "=== Step 4: 测试目标 ==="
if [ -n "$TEST_TARGETS_ARG" ]; then IFS=',' read -ra TARGETS <<< "$TEST_TARGETS_ARG"; else
  TARGETS=()
  ALL_TARGETS=$(grep -oP '^[a-zA-Z0-9_.-]+:' Makefile 2>/dev/null | tr -d ':' | sort -u || true)
  if [ -z "$ALL_TARGETS" ]; then ALL_TARGETS=$(ls CMakeFiles/ 2>/dev/null | grep '\.dir$' | sed 's/\.dir$//' || true); fi
  if [ -z "$ALL_TARGETS" ]; then ALL_TARGETS=$(make help 2>/dev/null | grep -oP '^\.\.\.\K\S+' || true); fi
  echo "  可用目标: $(echo "$ALL_TARGETS" | wc -w) 个"
  TEST_FILES=$(git -C "$SRC_DIR" diff --name-only "$BASE_SHA" "$HEAD_SHA" 2>/dev/null | grep -iP '/test[s]?/.*\.(cpp|cc)$' || true)
  if [ -n "$TEST_FILES" ]; then echo "  PR 新增测试文件:"; while IFS= read -r tf; do echo "    $tf"; done <<< "$TEST_FILES"; fi
  MATCHED_FILES=""
  if [ -n "$TEST_FILES" ]; then
    while IFS= read -r tf; do
      base=$(basename "$tf" .cpp); matched=false
      for variant in "$base" "$(echo "$base" | sed 's/Test$//' | sed 's/\(.\)\([A-Z]\)/\1_\2/g' | tr 'A-Z' 'a-z')_test" "$(echo "$base" | sed 's/Test$//' | tr 'A-Z' 'a-z')_test" "$(echo "$base" | sed 's/Test$//')Test-t" "$(echo "$base" | sed 's/Test$//' | sed 's/\(.\)\([A-Z]\)/\1_\2/g' | tr 'A-Z' 'a-z')"; do
        for t in $ALL_TARGETS; do
          if [ "$t" = "$variant" ] || [ "$t" = "${variant}-t" ] || [ "$t" = "${variant}_t" ]; then TARGETS+=("$t"); echo "    $tf -> $t (精确匹配)"; matched=true; MATCHED_FILES="$MATCHED_FILES $tf"; break 2; fi
        done
      done
      if [ "$matched" = false ]; then
        no_test=$(echo "$base" | sed 's/Test$//' | sed 's/test$//'); keywords=$(echo "$no_test" | sed 's/\([A-Z]\)/ \1/g' | tr 'A-Z' 'a-z' | tr ' ' '\n' | grep -v '^$' | grep -v '^.$' | tr '\n' ' ')
        for t in $ALL_TARGETS; do
          [ "$t" = "test" ] && continue
          t_lower=$(echo "$t" | tr 'A-Z' 'a-z')
          if ! echo "$t_lower" | grep -q 'test'; then continue; fi
          match=true
          for kw in $keywords; do if ! echo "$t_lower" | grep -q "$kw"; then match=false; break; fi; done
          if [ "$match" = "true" ]; then TARGETS+=("$t"); echo "    $tf -> $t (模糊匹配)"; matched=true; MATCHED_FILES="$MATCHED_FILES $tf"; break; fi
        done
      fi
      if [ "$matched" = false ]; then echo "    $tf -> 未找到匹配的测试目标"; fi
    done <<< "$TEST_FILES"
  fi
  UNMATCHED=""
  while IFS= read -r tf; do if ! echo "$MATCHED_FILES" | grep -q "$tf"; then UNMATCHED="$UNMATCHED$tf "; fi; done <<< "$TEST_FILES"
  if [ -n "$UNMATCHED" ] || [ ${#TARGETS[@]} -eq 0 ]; then
    echo "  有测试文件未匹配到，编译所有测试目标..."
    for t in $ALL_TARGETS; do
      [ "$t" = "test" ] && continue
      t_lower=$(echo "$t" | tr 'A-Z' 'a-z')
      if echo "$t_lower" | grep -q 'test'; then
        found_existing=false
        for existing in "${TARGETS[@]}"; do [ "$existing" = "$t" ] && found_existing=true && break; done
        [ "$found_existing" = false ] && TARGETS+=("$t")
      fi
    done
  fi
fi
if [ ${#TARGETS[@]} -eq 0 ]; then echo "ERROR: 未找到测试目标"; echo "  增量文件: $INCREMENTAL_CPP"; echo "  请通过参数指定: bash run_coverage.sh <base> <threshold> <test_targets>"; echo "RESULT: FAIL"; exit 1; fi
echo "  目标: ${TARGETS[*]}"
echo ""
echo "=== Step 5: 编译测试 ==="
declare -a TEST_BINS
for target in "${TARGETS[@]}"; do
  echo "  编译 $target ..."
  if ! make "$target" -j"$(nproc)" >/tmp/build_test.log 2>&1; then tail -5 /tmp/build_test.log; echo "ERROR: 编译 $target 失败"; echo "RESULT: FAIL"; exit 1; fi
  TESTBIN=$(find . -name "$target" -type f -executable 2>/dev/null | head -1)
  if [ -z "$TESTBIN" ]; then for path in "./bin/$target" "./$target" "$target" "./test/$target"; do if [ -x "$path" ]; then TESTBIN="$path"; break; fi; done; fi
  if [ -z "$TESTBIN" ]; then echo "ERROR: 找不到 $target 二进制"; echo "RESULT: FAIL"; exit 1; fi
  TEST_BINS+=("$TESTBIN"); echo "  $target -> $TESTBIN"
done
echo ""
echo "=== Step 6: 运行测试 ==="
rm -f coverage_*.profraw coverage.profdata
for i in "${!TARGETS[@]}"; do
  target="${TARGETS[$i]}"; testbin="${TEST_BINS[$i]}"
  echo "  运行 $testbin ..."
  if LLVM_PROFILE_FILE="coverage_${target}_%p.profraw" "$testbin" 2>&1; then echo "  $target: PASSED"; else echo "ERROR: $target 测试失败"; echo "RESULT: FAIL"; exit 1; fi
done
echo ""
echo "=== Step 7: 合并覆盖率 ==="
mapfile -t PROFRAW_ARRAY < <(ls coverage_*.profraw 2>/dev/null || true)
if [ ${#PROFRAW_ARRAY[@]} -eq 0 ]; then echo "ERROR: 无 profraw 文件"; echo "RESULT: FAIL"; exit 1; fi
echo "  profraw: ${#PROFRAW_ARRAY[@]} 个"
$PROFDATA merge "${PROFRAW_ARRAY[@]}" -o coverage.profdata 2>&1 || { echo "ERROR: profdata 合并失败"; echo "RESULT: FAIL"; exit 1; }
echo ""
echo "=== Step 8: 增量覆盖率 ==="
INCREMENTAL_NEWLINE=$(printf '%s\n' "${INCREMENTAL_FILES[@]}")
ALL_BINS_STR=$(printf '%s\n' "${TEST_BINS[@]}")
INCREMENTAL_RESULT=$(SRC_DIR="$SRC_DIR" BASE_SHA="$BASE_SHA" HEAD_SHA="$HEAD_SHA" COV="$COV" ALL_BINS="$ALL_BINS_STR" INCREMENTAL_CPP="$INCREMENTAL_NEWLINE" python3 << 'PYEOF'
import json, subprocess, os, re, sys
SRC_DIR = os.environ['SRC_DIR']; BASE_SHA = os.environ['BASE_SHA']; HEAD_SHA = os.environ['HEAD_SHA']; COV = os.environ['COV']
ALL_BINS = [b for b in os.environ['ALL_BINS'].split('\n') if b]
INCREMENTAL_CPP = [f for f in os.environ['INCREMENTAL_CPP'].split('\n') if f]
def get_added_lines(src_dir, base, head):
    result = subprocess.run(['git', '-C', src_dir, 'diff', base, head], capture_output=True, text=True)
    added = {}; current_file = None; new_line = 0
    for line in result.stdout.split('\n'):
        if line.startswith('+++ b/'): current_file = line[6:]; added[current_file] = set()
        elif line.startswith('@@'):
            m = re.search(r'\+(\d+)', line)
            if m: new_line = int(m.group(1))
        elif line.startswith('+') and not line.startswith('+++') and current_file: added[current_file].add(new_line); new_line += 1
        elif not line.startswith('-') and not line.startswith('\\'): new_line += 1
    return added
def get_coverage(cov_tool, testbins, profdata, srcfile):
    line_cov = {}
    for testbin in testbins:
        result = subprocess.run([cov_tool, 'export', testbin, '-instr-profile=' + profdata, '-format=text', srcfile], capture_output=True, text=True)
        if not result.stdout: continue
        try: data = json.loads(result.stdout)
        except: continue
        files = data.get("data", [{}])[0].get("files", [])
        file_segs = None
        if isinstance(files, list):
            for item in files:
                if isinstance(item, dict): file_segs = item.get("segments", []); break
        elif isinstance(files, dict):
            for fname, fdata in files.items(): file_segs = fdata.get("segments", []); break
        if file_segs is None: continue
        for seg in file_segs:
            if seg[3] == 1:
                ln = seg[0]; cnt = seg[2]
                if ln not in line_cov or cnt > line_cov[ln]: line_cov[ln] = cnt
    return line_cov
added_lines = get_added_lines(SRC_DIR, BASE_SHA, HEAD_SHA)
total_covered = 0; total_exec = 0
for f in INCREMENTAL_CPP:
    if f not in added_lines or not added_lines[f]: continue
    srcfile = os.path.join(SRC_DIR, f)
    if not os.path.exists(srcfile): print("  %s: 文件不存在" % f); continue
    line_cov = get_coverage(COV, ALL_BINS, 'coverage.profdata', srcfile)
    if not line_cov: print("  %s: 无覆盖率数据" % f); continue
    pr_exec = {ln: line_cov[ln] for ln in added_lines[f] if ln in line_cov}
    covered = sum(1 for c in pr_exec.values() if c > 0); total = len(pr_exec)
    if total > 0:
        pct = covered * 100 / total; print("  %s: %d/%d (%.1f%%)" % (f, covered, total, pct))
        total_covered += covered; total_exec += total
        uncovered = {ln: c for ln, c in pr_exec.items() if c == 0}
        if uncovered:
            print("    未覆盖行 (%d):" % len(uncovered))
            src_lines = []
            try:
                with open(srcfile) as sf: src_lines = sf.readlines()
            except: pass
            for ln in sorted(uncovered.keys()):
                if src_lines and ln <= len(src_lines): print("      %s:%d: %s" % (f, ln, src_lines[ln-1].rstrip()))
                else: print("      %s:%d" % (f, ln))
        no_cov_data = sorted(added_lines[f] - set(line_cov.keys()))
        if no_cov_data:
            print("    新增但无覆盖率数据 (%d):" % len(no_cov_data))
            src_lines = []
            try:
                with open(srcfile) as sf: src_lines = sf.readlines()
            except: pass
            for ln in no_cov_data:
                if src_lines and ln <= len(src_lines): print("      %s:%d: %s" % (f, ln, src_lines[ln-1].rstrip()))
                else: print("      %s:%d" % (f, ln))
    else: print("  %s: 无可执行新增行" % f)
if total_exec > 0:
    final_pct = total_covered * 100 / total_exec; print("TOTAL:%d/%d/%.1f" % (total_covered, total_exec, final_pct))
else: print("TOTAL:0/0/SKIP")
PYEOF
)
echo ""
TOTAL_LINE=$(echo "$INCREMENTAL_RESULT" | grep "^TOTAL:")
if [ -z "$TOTAL_LINE" ]; then echo "ERROR: 无法计算覆盖率"; echo "RESULT: FAIL"; exit 1; fi
TOTAL_COVERED=$(echo "$TOTAL_LINE" | cut -d: -f2 | cut -d/ -f1)
TOTAL_EXEC=$(echo "$TOTAL_LINE" | cut -d: -f2 | cut -d/ -f2 | cut -d/ -f1)
FINAL_PCT=$(echo "$TOTAL_LINE" | cut -d/ -f3)
echo "============================================"
echo "  覆盖率汇总（仅 PR 新增可执行行）"
echo "============================================"
echo "$INCREMENTAL_RESULT" | grep -v "^TOTAL:"
echo ""
echo "  总计: $TOTAL_COVERED/$TOTAL_EXEC = ${FINAL_PCT}%"
echo "  阈值: ${MIN_COVERAGE}%"
HTML_DIR="$BUILD_DIR/coverage_html"; rm -rf "$HTML_DIR"
SRC_LIST=()
for f in "${INCREMENTAL_FILES[@]}"; do [ -f "$SRC_DIR/$f" ] && SRC_LIST+=("$SRC_DIR/$f"); done
for covbin in "${TEST_BINS[@]}"; do "$COV" show "$covbin" -instr-profile=coverage.profdata -format=html -output-dir="$HTML_DIR" "${SRC_LIST[@]}" 2>/dev/null && break; done
echo "  HTML: $HTML_DIR/index.html"
PASS=$(python3 -c "
import sys
final = float(sys.argv[1])
threshold = float(sys.argv[2])
print('PASS' if final >= threshold else 'FAIL')
" "$FINAL_PCT" "$MIN_COVERAGE")
echo ""
echo "============================================"
echo "  RESULT: $PASS"
echo "  增量覆盖率 ${FINAL_PCT}% $PASS 阈值 ${MIN_COVERAGE}%"
echo "============================================"
[ "$PASS" = "PASS" ] && exit 0 || exit 1
