# 快速入门

> 适用版本：FbThrift v1.1.0

本文档指导用户从零构建带四项请求链路优化的FbThrift，并编译、启动配套Benchmark。四项优化包括动态收包缓冲区、Folly IOBuf TLS内存池、ThreadManager direct-func和请求热路径去锁。

## 1. 获取优化源码

本项目提供两种获取优化源码的方式：按1.1节直接获取优化源码，或依次按1.2～1.4节获取基线源码、校验并应用补丁。完成后，进入第2章准备编译环境。

先创建统一工作目录。后续Folly、Fizz、Wangle、FbThrift的源码、安装目录和Benchmark均位于`$WORK`下。

```bash
export WORK=/home/your-user/fbthrift-work
export INS="$WORK/ins"
export ACCLIB="$WORK/AccLibBenchmark"

mkdir -p "$WORK" "$INS"
```

### 1.1 直接获取优化源码

dev_20221114分支已包含FbThrift序列化优化、动态收包缓冲区、ThreadManager direct-func、请求热路径去锁等优化内容。

```bash
git clone --recurse-submodules --branch dev_20221114 --single-branch \
  https://gitcode.com/boostkit/fbthrift.git "$WORK/fbthrift"
cd "$WORK/fbthrift"
```

1.2~1.4均为补丁仓的获取与应用，若已获取优化源码，即可跳转至[第二章](#2-编译环境)进行编译准备。

### 1.2 获取基线源码、补丁和校验文件

基线源码保存在`$WORK/fbthrift`目录，补丁和校验文件保存在`$WORK/fbthrift-patches`目录。

```bash
git clone --recurse-submodules --branch v2022.11.14.00 --single-branch \
  https://github.com/facebook/fbthrift.git "$WORK/fbthrift"
git clone --branch master --single-branch \
  https://gitcode.com/boostkit/fbthrift.git "$WORK/fbthrift-patches"
cd "$WORK/fbthrift-patches"
```

### 1.3 软件包完整性校验

本项目以补丁文件形式提供 FbThrift 性能优化功能，采用 **SHA-256 校验**确认补丁在下载、传输和存储过程中是否发生变化。

SHA-256 校验用于验证文件完整性，不单独证明来源真实性。请从 [FbThrift 官方仓库](https://gitcode.com/boostkit/fbthrift)获取补丁及同一版本的校验文件。

**1. 校验文件**

| 文件名称 | 说明 |
| --- | --- |
| `fbthrift_folly.patch` | FbThrift 性能优化补丁 |
| `fbthrift_folly.patch.sha256` | 记录上述补丁文件名及 SHA-256 摘要值的校验文件 |

补丁与校验文件应来自同一发布版本。补丁更新时，应同步更新校验文件。

**2. 校验步骤**

将补丁和校验文件放在同一目录，在该目录下执行以下命令。按前面的步骤获取后，当前目录即为`$WORK/fbthrift-patches`。

```bash
sha256sum --check --strict fbthrift_folly.patch.sha256
```

该命令读取校验文件中的摘要值，与实际补丁的 SHA-256 摘要进行比较，并检查校验文件格式。[命令说明](https://www.gnu.org/software/coreutils/manual/html_node/sha2-utilities.html)

校验通过时，输出如下：

```text
fbthrift_folly.patch: OK
```

中文环境可能显示“成功”。应确认输出对应的文件名为 `fbthrift_folly.patch`，且命令没有报告失败或格式错误。

**3. 结果判定**

| 校验结果 | 判定及处理 |
| --- | --- |
| 显示 `OK` 或“成功”，且无错误提示 | 补丁与校验文件中的摘要一致，完整性校验通过，可继续应用补丁 |
| 显示 `FAILED` 或“失败” | 补丁内容与预期不一致，停止使用并重新获取 |
| 提示文件不存在或无法读取 | 检查当前目录、文件名及文件是否下载完整 |
| 提示校验文件格式错误 | 重新获取发布方提供的校验文件 |

**4. 异常处理**

校验失败时，请从官方仓库重新获取同一版本的补丁和校验文件，再次执行校验。

不要通过修改校验文件中的摘要值使校验通过。如重新获取后仍然失败，请向发布方反馈补丁版本、文件名和完整的校验输出。

### 1.4 应用优化补丁

校验通过后，切换到基线源码目录，检查并应用补丁：

```bash
cd "$WORK/fbthrift"
git apply --check "$WORK/fbthrift-patches/fbthrift_folly.patch"
git apply "$WORK/fbthrift-patches/fbthrift_folly.patch"
```

补丁只需应用一次。完成后，继续准备第2章的编译环境，并按第3章获取依赖、编译与安装。

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

- Folly需要使用Snappy 1.1.8及以上版本的动态库。系统依赖安装完成后，执行以下命令确认Snappy版本及`libsnappy.so`动态库路径。

  ```bash
  pkg-config --modversion snappy
  ldconfig -p | grep 'libsnappy\.so'
  ```
  
  如果Snappy版本低于1.1.8、仅安装了静态库，或者Folly与Benchmark实际加载了不同版本的`libsnappy.so`，后续编译Benchmark时可能出现以下错误。
  
  ```output
  undefined symbol: _ZTIN6snappy6sourceE
  ```
  
  出现该错误时，应先升级Snappy动态库并确保Folly与Benchmark使用同一版本，然后重新编译Folly和Benchmark。

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

### 3.1 下载依赖源码和Benchmark

FbThrift源码已在第1章准备完成。以下命令获取Folly、Fizz、Wangle依赖及公共Benchmark仓库。

   ```bash
   git clone --recurse-submodules --branch dev_iouring --single-branch \
   https://gitcode.com/boostkit/folly.git "$WORK/folly"

   git clone --recurse-submodules --branch v2022.11.14.00 --single-branch \
   https://github.com/facebookincubator/fizz.git "$WORK/fizz"

   git clone --recurse-submodules --branch v2022.11.14.00 --single-branch \
   https://github.com/facebook/wangle.git "$WORK/wangle"

   git clone https://gitcode.com/boostkit/AccLibBenchmark.git "$ACCLIB"
   ```

仓库中与FbThrift相关的两个目录职责如下。(若无权限请联系管理员申请并说明原因。)

- [fbthrift_folly_benchmark](https://gitcode.com/boostkit/AccLibBenchmark/tree/master/fbthrift_folly_benchmark)：存放`press.thrift`、CMake配置、`press_client`和`press_server`源码。
- [fb_folly_autobuild](https://gitcode.com/boostkit/AccLibBenchmark/tree/master/fb_folly_autobuild)：存放`install.py`自动构建脚本、`run.py`性能矩阵脚本及使用说明。

```text
AccLibBenchmark/
├── fbthrift_folly_benchmark/
│   ├── press.thrift
│   ├── CMakeLists.txt
│   ├── client/
│   └── server/
└── fb_folly_autobuild/
    ├── fbthrift_folly.patch(if needed)
    ├── install.py
    └── run.py
```

由于AccLibBenchmark作为压测工具的代码汇总，众多工具我们不会使用。因此拉取压测工具代码仓后后，只选用其中的`fbthrift_folly_benchmark`压测工具，以及`fb_folly_autobuild`的自动化脚本，如上方目录结构所示。

> **说明:** 使用脚本安装时，默认拉取优化代码版本。若需要由脚本应用补丁，请先按第1.3节完成补丁校验，再将`fbthrift_folly.patch`放在`fb_folly_autobuild/`目录下。脚本安装方式见第4章。

### 3.2 编译Folly

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

### 3.3 编译Fizz

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

### 3.4 编译Wangle

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

### 3.5 编译FbThrift

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

### 3.6 生成并编译Benchmark

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

```output
$BENCH/build/press_server
$BENCH/build/press_client
```

只有修改`press.thrift`或缺少`gen-cpp2`时才需要重新生成代码；仅修改C++源码时不要重复生成，以免覆盖已有生成文件。

### 3.7 查看最终目录结构

全部组件和Benchmark编译完成后，相关目录结构如下。

```output
$WORK/
├── AccLibBenchmark/
│   ├── fbthrift_folly_benchmark/
│   │   ├── build/
│   │   │   ├── press_client
│   │   │   └── press_server
│   │   └── gen-cpp2/
│   └── fb_folly_autobuild/
├── folly/
│   └── _build/
├── fizz/
│   └── build_/
├── wangle/
│   └── build_/
├── fbthrift/
│   └── build_/
└── ins/
    ├── folly/
    ├── fizz/
    ├── wangle/
    └── fbthrift/
```

其中，`folly/`、`fizz/`、`wangle/`和`fbthrift/`保存源码及构建产物，`ins/`保存各组件的安装结果，Benchmark可执行文件位于`$ACCLIB/fbthrift_folly_benchmark/build/`。

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

### 4.1 准备脚本工具

1. 执行以下命令。

   ```bash
   git clone https://gitcode.com/boostkit/AccLibBenchmark.git
   cd AccLibBenchmark/fb_folly_autobuild
   ```

2. 确认以下文件存在。

   ```bash
   test -f install.py
   test -f run.py
   ```

如需由脚本应用优化补丁，请先按[第1.3节](#13-软件包完整性校验)完成校验，再将`fbthrift_folly.patch`放到当前目录。

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
WORK = Path("/home/your-user/fbthrift-work")
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

   ```output
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
|:---|:---|:---|
|01|2026-9-30|第一次正式发布。|
