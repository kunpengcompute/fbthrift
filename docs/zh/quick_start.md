# 快速入门

> 适用版本：FbThrift v1.1.0

本文档指导用户从零构建带四项请求链路优化的FbThrift，并编译、启动配套Benchmark。四项优化包括动态收包缓冲区、Folly IOBuf TLS内存池、ThreadManager direct-func和请求热路径去锁。

## 1. 获取公共Benchmark仓库

统一从公共仓库获取Benchmark与自动构建脚本。

```bash
git clone https://gitcode.com/donghuanan/AccLibBenchmark.git
cd AccLibBenchmark
```

仓库中与FbThrift相关的两个目录职责如下。(若无权限请联系管理员申请并说明原因。)

- [fbthrift_folly_benchmark](https://gitcode.com/donghuanan/AccLibBenchmark/tree/master/fbthrift_folly_benchmark)：存放`press.thrift`、CMake配置、`press_client`和`press_server`源码。
- [fb_folly_autobuild](https://gitcode.com/donghuanan/AccLibBenchmark/tree/master/fb_folly_autobuild)：存放`install.py`自动构建脚本、`run.py`性能矩阵脚本及使用说明。

```text
AccLibBenchmark/
├── fbthrift_folly_benchmark/
│   ├── press.thrift
│   ├── CMakeLists.txt
│   ├── client/
│   └── server/
└── fb_folly_autobuild/
    ├── fbthrift.patch
    ├── install.py
    └── run.py
```

> **优化补丁：** 脚本默认含优化代码版本，若有需要，请将fbthrift.patch放在install.py同目录下，脚本支持自动补全补丁。

## 2. 编译环境

推荐准备至少30GB可用磁盘空间，并确保构建机可以通过HTTPS访问GitCode和GitHub。

### 2.1 安装系统依赖

- Debian或Ubuntu执行以下命令。

  ```bash
  sudo apt-get update
  sudo apt-get install -y \
  git cmake build-essential pkg-config xz-utils numactl \
  liburing-dev libboost-all-dev libdouble-conversion-dev \
  libgflags-dev libgoogle-glog-dev libevent-dev libsodium-dev \
  liblz4-dev libsnappy-dev libzstd-dev libfmt-dev liblzma-dev \
  libgtest-dev libgmock-dev libssl-dev libaio-dev \
  libunwind-dev libdwarf-dev binutils-dev libiberty-dev \
  zlib1g-dev libbz2-dev
  ```

- openEuler或其他RPM系统执行以下命令。

  ```bash
  sudo dnf install -y \
  git cmake make gcc gcc-c++ pkgconf-pkg-config numactl \
  fmt fmt-devel glog glog-devel gflags gflags-devel \
  libevent libevent-devel double-conversion double-conversion-devel \
  boost boost-devel libunwind libunwind-devel \
  lz4 lz4-devel zstd zstd-devel libsodium libsodium-devel \
  liburing liburing-devel libatomic zlib zlib-devel \
  openssl openssl-devel
  ```

### 2.2 准备Clang 16

优先使用系统已安装的Clang 16。

```bash
clang-16 --version
clang++-16 --version

export CC="$(command -v clang-16)"
export CXX="$(command -v clang++-16)"
```

>**说明**：如果系统没有Clang 16，可使用与目标架构匹配的LLVM二进制包。Folly、Fizz、Wangle、FbThrift和Benchmark必须使用同一组`CC`、`CXX`。

## 3. 手动编译

手动方式适合首次部署和定位单个组件的构建问题。

### 3.1 创建目录

```bash
export WORK=/home/your-user/fbthrift-work
export INS="$WORK/ins"
export ACCLIB="$WORK/AccLibBenchmark"

mkdir -p "$WORK" "$INS"
```

如果公共仓库尚未下载到`$ACCLIB`，执行以下命令获取。

```bash
git clone https://gitcode.com/donghuanan/AccLibBenchmark.git "$ACCLIB"
```

最终目录关系如下。

```text
$WORK/
├── folly/
├── fizz/
├── wangle/
├── fbthrift/
├── AccLibBenchmark/
└── ins/
    ├── folly/
    ├── fizz/
    ├── wangle/
    └── fbthrift/
```

### 3.2 下载依赖并应用补丁

1. 下载依赖源码。

   ```bash
   git clone --recurse-submodules --branch dev_iouring --single-branch \
   https://gitcode.com/boostkit/folly.git "$WORK/folly"

   git clone --recurse-submodules --branch v2022.11.14.00 --single-branch \
   https://github.com/facebookincubator/fizz.git "$WORK/fizz"

   git clone --recurse-submodules --branch v2022.11.14.00 --single-branch \
   https://github.com/facebook/wangle.git "$WORK/wangle"

   git clone --recurse-submodules --branch dev_20221114 --single-branch \
   https://gitcode.com/boostkit/fbthrift.git "$WORK/fbthrift"
   ```

2. 应用FbThrift v1.1.0优化补丁。

   ```bash
    cd "$WORK/fbthrift"
    git apply --check "$ACCLIB/fb_folly_autobuild/fbthrift.patch"
    git apply --3way "$ACCLIB/fb_folly_autobuild/fbthrift.patch"
    ```

   如果`git apply --reverse --check`能够成功，说明补丁已经应用，不应重复执行。

### 3.3 编译Folly

```bash
cmake -S "$WORK/folly" -B "$WORK/folly/_build" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_STANDARD=17 \
  -DBUILD_BENCHMARKS=OFF \
  -DBUILD_TESTS=OFF \
  -DBUILD_SHARED_LIBS=ON \
  -DCMAKE_INSTALL_PREFIX="$INS/folly"

cmake --build "$WORK/folly/_build" --parallel "$(nproc)"
cmake --install "$WORK/folly/_build"
```

### 3.4 编译Fizz

```bash
cmake -S "$WORK/fizz/fizz" -B "$WORK/fizz/build_" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTS=OFF \
  -DBUILD_SHARED_LIBS=ON \
  -Dfolly_DIR="$INS/folly/lib/cmake/folly" \
  -DCMAKE_INSTALL_PREFIX="$INS/fizz"

cmake --build "$WORK/fizz/build_" --parallel "$(nproc)"
cmake --install "$WORK/fizz/build_"
```

### 3.5 编译Wangle

```bash
cmake -S "$WORK/wangle/wangle" -B "$WORK/wangle/build_" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTS=OFF \
  -DBUILD_SHARED_LIBS=ON \
  -Dfolly_DIR="$INS/folly/lib/cmake/folly" \
  -Dfizz_DIR="$INS/fizz/lib/cmake/fizz" \
  -DCMAKE_INSTALL_PREFIX="$INS/wangle"

cmake --build "$WORK/wangle/build_" --parallel "$(nproc)"
cmake --install "$WORK/wangle/build_"
```

### 3.6 编译FbThrift

```bash
cmake -S "$WORK/fbthrift" -B "$WORK/fbthrift/build_" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=ON \
  -DTHRIFT_ENABLE_ARM_SVE2=ON \
  -Dfolly_DIR="$INS/folly/lib/cmake/folly" \
  -Dfizz_DIR="$INS/fizz/lib/cmake/fizz" \
  -Dwangle_DIR="$INS/wangle/lib/cmake/wangle" \
  -DCMAKE_INSTALL_PREFIX="$INS/fbthrift"

cmake --build "$WORK/fbthrift/build_" --parallel "$(nproc)"
cmake --install "$WORK/fbthrift/build_"
```

目标CPU不支持SVE2时，将`THRIFT_ENABLE_ARM_SVE2`设置为`OFF`。该设置只影响Compact Protocol SVE2路径，不关闭其他请求链路优化。

### 3.7 生成并编译Benchmark

```bash
export BENCH="$ACCLIB/fbthrift_folly_benchmark"
export CMAKE_PREFIX_PATH="$INS/fbthrift:$INS/wangle:$INS/fizz:$INS/folly"
export LD_LIBRARY_PATH="$INS/fbthrift/lib:$INS/fbthrift/lib64:$INS/wangle/lib:$INS/wangle/lib64:$INS/fizz/lib:$INS/fizz/lib64:$INS/folly/lib:$INS/folly/lib64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

cd "$BENCH"
"$INS/fbthrift/bin/thrift1" --gen mstch_cpp2 press.thrift

cmake -S "$BENCH" -B "$BENCH/build" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH"

cmake --build "$BENCH/build" --parallel "$(nproc)"
```

构建完成后应存在以下内容。

```text
$BENCH/build/press_server
$BENCH/build/press_client
```

只有修改`press.thrift`或缺少`gen-cpp2`时才需要重新生成代码；仅修改C++源码时不要重复生成，以免覆盖已有生成文件。

### 3.8 启动并验证Benchmark

在服务端和客户端终端中都先设置运行环境。

```bash
export WORK=/home/your-user/fbthrift-work
export INS="$WORK/ins"
export BENCH="$WORK/AccLibBenchmark/fbthrift_folly_benchmark"
export LD_LIBRARY_PATH="$INS/fbthrift/lib:$INS/fbthrift/lib64:$INS/wangle/lib:$INS/wangle/lib64:$INS/fizz/lib:$INS/fizz/lib64:$INS/folly/lib:$INS/folly/lib64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
```

1. 先启动服务端。

   ```bash
   "$BENCH/build/press_server" \
   --port=23456 \
   --num_io_threads=12 \
   --num_cpu_threads=12
   ```

2. 另开终端运行客户端。

   ```bash
   "$BENCH/build/press_client" \
   --host=127.0.0.1 \
   --port=23456 \
   --transport=header \
   --fields=none \
   --payload_size=1024 \
   --connections=100 \
   --queue_depth=10 \
   --io_threads=12 \
   --cpu_threads=12 \
   --test_seconds=30 \
   --compression=none
   ```

输出中应包含`Success`、`Fail`、`QPS`、`Throughput`和延迟分位数。先确认`Fail=0`，再扩大连接数、测试时长和Payload。Rocket测试只需将`--transport=header`改为`rocket`。

需要验证io_uring时，客户端和服务端都增加`--use_io_uring=true`，并确认Folly及系统`liburing`支持该路径。

## 4. 编译脚本

脚本方式适合重复构建、修改源码后重编以及批量运行性能矩阵。

### 4.1 准备脚本与补丁

1. 执行以下命令。

   ```bash
   git clone https://gitcode.com/donghuanan/AccLibBenchmark.git
   cd AccLibBenchmark/fb_folly_autobuild
   ```

2. 确认以下文件存在。

   ```bash
   test -f install.py
   test -f run.py
   test -f fbthrift.patch
   ```

其中`fbthrift.patch`需要由FbThrift v1.1.0发布包提供并放到当前目录。

### 4.2 适配公共仓库目录

公共仓库的Benchmark位于脚本同级目录，而当前`install.py`默认把另一个Benchmark仓库直接克隆到`WORK/fbthrift_folly_benchmark`。

1. 使用本公共仓库时，应让脚本直接使用已下载的兄弟目录。

   ```python
   BENCHMARK_DIR = SCRIPT_DIR.parent / "fbthrift_folly_benchmark"
   ```

2. 同时将`prepare_benchmark_source()`调整为只检查本地工程。

   ```python
   def prepare_benchmark_source():
    if not (BENCHMARK_DIR / "CMakeLists.txt").is_file():
        raise FileNotFoundError(
            "benchmark source not found: {}".format(BENCHMARK_DIR)
        )
   ```

这样脚本不会再次克隆其他Benchmark仓库，手动与脚本方式都会使用公共仓库中的同一份`fbthrift_folly_benchmark`源码。

### 4.3 配置install.py

至少确认以下配置。

```python
WORK = Path("/data/your-user/fbthrift-work")
INS = WORK / "ins"

AUTO_REENTRY = True
SKIP_SOURCE_CLONE = False
INSTALL_PACKAGES = False
REGENERATE_THRIFT_SOURCES = False
THRIFT_ENABLE_ARM_SVE2 = True
```

- 首次构建保持`SKIP_SOURCE_CLONE=False`，允许通过HTTPS获取Folly、Fizz、Wangle和FbThrift。
- 系统依赖已经安装时保持`INSTALL_PACKAGES=False`；需要脚本调用`apt-get`或`dnf`时设为`True`。
- 修改`press.thrift`或缺少`gen-cpp2`时设置`REGENERATE_THRIFT_SOURCES=True`。
- CPU不支持SVE2时设置`THRIFT_ENABLE_ARM_SVE2=False`。
- 若系统没有Clang 16，需要在脚本同目录放置匹配架构的Clang 16归档，或调整`CLANG16_TARBALL`。

### 4.4 执行自动构建

1. 执行以下命令。

   ```bash
   cd AccLibBenchmark/fb_folly_autobuild
   python3 install.py
   ```

   脚本依次完成系统依赖检查、Clang探测、源码下载、优化补丁应用、Folly/Fizz/Wangle/FbThrift安装、Thrift代码生成和Benchmark编译。

2. 成功后检查。

   ```text
   WORK/ins/fbthrift/bin/thrift1
   AccLibBenchmark/fbthrift_folly_benchmark/build/press_server
   AccLibBenchmark/fbthrift_folly_benchmark/build/press_client
   ```

   源码目录已经存在时，`AUTO_REENTRY=True`会复用Git checkout并保留本地修改。更换编译器或出现CMake Cache冲突时，只清理各组件的构建目录，再重新运行脚本，不要删除源码目录。

### 4.5 使用run.py执行性能矩阵

1. 运行前修改`run.py`中的以下内容。

   - `EXECUTABLE`：指向实际的`press_client`。
   - `PREFIX_CMD`：确认服务端IP、端口、transport、NUMA节点和io_uring开关。
   - `CONN_SETUPS`、`QD_SETUPS`、`DATA_SETUPS`：先使用小矩阵验证环境。
   - `OUTPUT_CSV`：设置结果文件名称，避免覆盖已有结果。

2. 先按3.8节启动`press_server`，再执行以下命令。

   ```bash
   cd AccLibBenchmark/fb_folly_autobuild
   python3 run.py
   ```

   脚本会逐项运行`press_client`，并将QPS、吞吐量、平均延迟、P99延迟、成功率及客户端/服务端CPU利用率写入CSV。

## 修订记录

|文档版本|发布日期|修改说明|
| :---| :---| :---|
|01|2026-9-30|第一次正式发布:<br>• 基于echo client的完整端到端rpc的自写benchmark，统计QPS，吞吐量，rpc的时延包括平均值，p50,p90,p99等，支持本地回环与双机直连。<br>• 自动化编译依赖库，目标库，benchmark的脚本。<br>• benchmark的运行脚本与自动采集数据并输出excel表格。|
