# Quick Start

This document guides you through building FbThrift with four request-path optimizations from scratch, and compiling and starting the accompanying benchmark. The four optimizations include the dynamic receive buffer, Folly IOBuf TLS memory pool, ThreadManager direct-func, and lock removal on the request hot path.

> ![Image indicating a note](public_sys-resources/icon-note.gif)**NOTE**
>
> This document applies to FbThrift v1.1.0.

## Creating a Working Directory and Obtaining the Public Benchmark Repository

First create a working directory, and then clone the public benchmark repository to the specified location. The source code, installation directories, and benchmarks of Folly, Fizz, Wangle, and FbThrift are all located under `$WORK`.

```bash
export WORK=/home/your-user/fbthrift-work
export INS="$WORK/ins"
export ACCLIB="$WORK/AccLibBenchmark"

mkdir -p "$WORK" "$INS"
git clone https://gitcode.com/boostkit/AccLibBenchmark.git "$ACCLIB"
cd "$ACCLIB"
```

The responsibilities of the two FbThrift-related directories in the repository are as follows. (If you do not have the required permissions, contact the maintainer to apply for them and explain the reason.)

- [fbthrift_folly_benchmark](https://gitcode.com/boostkit/AccLibBenchmark/tree/master/fbthrift_folly_benchmark): Stores the `press.thrift` file, CMake configuration, and the source code of `press_client` and `press_server`.
- [fb_folly_autobuild](https://gitcode.com/boostkit/AccLibBenchmark/tree/master/fb_folly_autobuild): Stores the `install.py` auto build script, the `run.py` performance matrix script, and usage instructions.

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

AccLibBenchmark serves as a code collection of stress testing tools. After pulling it, only the `fbthrift_folly_benchmark` stress testing tool and the automation scripts in `fb_folly_autobuild` are used, as shown in the directory structure above.

>![note](public_sys-resources/icon-note.gif) **NOTE**
>
>When installing with the script, the optimized code version is pulled by default. If code patches are needed based on the open-source repository, place `fbthrift_folly.patch` from the master branch of the patch repository into the `fb_folly_autobuild/` directory, and the script will apply the patch automatically.

## Build Environment

It is recommended to prepare at least 30 GB of available disk space and ensure that the build machine can access GitCode and GitHub over HTTPS.

### Installing System Dependencies

- For Debian or Ubuntu, run the following commands:

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

- For openEuler or other RPM-based systems, run the following commands:

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

- Folly requires the Snappy dynamic library version 1.1.8 or later. After the system dependencies are installed, run the following commands to check the Snappy version and the path of the `libsnappy.so` dynamic library.

  ```bash
  pkg-config --modversion snappy
  ldconfig -p | grep 'libsnappy\.so'
  ```

  If the Snappy version is earlier than 1.1.8, only the static library is installed, or Folly and Benchmark actually load different versions of `libsnappy.so`, the following error may occur when compiling Benchmark.

  ```output
  undefined symbol: _ZTIN6snappy6sourceE
  ```

  When this error occurs, upgrade the Snappy dynamic library first, ensure that Folly and Benchmark use the same version, and then recompile Folly and Benchmark.

### Preparing Clang 16

Use the system-installed Clang 16 preferentially.

```bash
clang-16 --version
clang++-16 --version

export CC="$(command -v clang-16)"
export CXX="$(command -v clang++-16)"
```

>![note](public_sys-resources/icon-note.gif)**NOTE**
>If Clang 16 is not available on the system, use an LLVM binary package that matches the target architecture. Folly, Fizz, Wangle, FbThrift, and Benchmark must use the same set of `CC` and `CXX`.

## Manual Compilation

The manual approach is suitable for first-time deployment and for locating build issues of individual components.

### Downloading Dependency Source Code

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

### Compiling Folly

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

### Compiling Fizz

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

### Compiling Wangle

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

### Compiling FbThrift

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

If the target CPU does not support SVE2, set `THRIFT_ENABLE_ARM_SVE2` to `OFF`. This setting affects only the Compact Protocol SVE2 path and does not disable other request path optimizations.

### Generating and Compiling Benchmarks

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

After the build is complete, the following benchmarks should exist.

```output
$BENCH/build/press_server
$BENCH/build/press_client
```

Code regeneration is only required when `press.thrift` is modified or `gen-cpp2` is missing; when only C++ source code is modified, do not regenerate, to avoid overwriting existing generated files.

### Viewing the Final Directory Structure

After all components and benchmarks are compiled, the relevant directory structure is as follows.

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

Among them, `folly/`, `fizz/`, `wangle/`, and `fbthrift/` store the source code and build artifacts, `ins/` stores the installation results of each component, and the benchmark executables are located in `$ACCLIB/fbthrift_folly_benchmark/build/`.

### Starting and Verifying Benchmark

Set up the runtime environment in both the server and client terminals first.

```bash
export WORK=/home/your-user/fbthrift-work
export INS="$WORK/ins"
export BENCH="$WORK/AccLibBenchmark/fbthrift_folly_benchmark"
export LD_LIBRARY_PATH="$INS/fbthrift/lib:$INS/fbthrift/lib64:$INS/wangle/lib:$INS/wangle/lib64:$INS/fizz/lib:$INS/fizz/lib64:$INS/folly/lib:$INS/folly/lib64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
```

1. Start the server first.

   ```bash
   "$BENCH/build/press_server" \
   --port=23456 \
   --num_io_threads=12 \
   --num_cpu_threads=12
   ```

2. Open another terminal to run the client.

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

The output should include `Success`, `Fail`, `QPS`, `Throughput`, and latency percentiles. First confirm that `Fail=0`, and then increase the number of connections, test duration, and payload. For the Rocket test, simply change `--transport=header` to `rocket`.

To verify io_uring, add `--use_io_uring=true` on both the client and server sides, and confirm that Folly and the system's `liburing` support this path.

## Build Script

The script-based approach is suitable for repeated builds, rebuilding after source code changes, and batch execution of performance matrices.

### Preparing Script Tools

1. Run the following command:

   ```bash
   git clone https://gitcode.com/boostkit/AccLibBenchmark.git
   cd AccLibBenchmark/fb_folly_autobuild
   ```

2. Confirm that the following files exist.

   ```bash
   test -f install.py
   test -f run.py
   ```

As described in [Chapter 1](#creating-a-working-directory-and-obtaining-the-public-benchmark-repository), if you build based on the FbThrift v1.1.0 release package, you need to provide the `fbthrift_folly.patch` patch and place it in the current directory.

### Adapting the Public Repository Directory

The benchmark of the public repository is located in the sibling directory of the script, while the current `install.py` clones another benchmark repository directly to `WORK/fbthrift_folly_benchmark` by default.

1. When using this public repository, the script should directly use the downloaded sibling directory.

   ```python
   BENCHMARK_DIR = SCRIPT_DIR.parent / "fbthrift_folly_benchmark"
   ```

2. At the same time, adjust `prepare_benchmark_source()` to check only the local project.

   ```python
   def prepare_benchmark_source():
    if not (BENCHMARK_DIR / "CMakeLists.txt").is_file():
        raise FileNotFoundError(
            "benchmark source not found: {}".format(BENCHMARK_DIR)
        )
   ```

In this way, the script will not clone other benchmark repositories again, and both the manual and script-based approaches will use the same `fbthrift_folly_benchmark` source code in the public repository.

### Configuring install.py

At least confirm the following configurations:

```python
WORK = Path("/home/your-user/fbthrift-work")
INS = WORK / "ins"

AUTO_REENTRY = True
SKIP_SOURCE_CLONE = False
INSTALL_PACKAGES = False
REGENERATE_THRIFT_SOURCES = False
THRIFT_ENABLE_ARM_SVE2 = True
```

- For the first build, keep `SKIP_SOURCE_CLONE=False` to allow obtaining Folly, Fizz, Wangle, and FbThrift over HTTPS.
- Keep `INSTALL_PACKAGES=False` when the system dependencies are already installed; set it to `True` when the script needs to invoke `apt-get` or `dnf`.
- Set `REGENERATE_THRIFT_SOURCES=True` when `press.thrift` is modified or `gen-cpp2` is missing.
- Set `THRIFT_ENABLE_ARM_SVE2=False` when the CPU does not support SVE2.
- If the system does not have Clang 16, place a Clang 16 archive matching the architecture in the same directory as the script, or adjust `CLANG16_TARBALL`.

### Running Automatic Build

1. Run the following commands:

   ```bash
   cd AccLibBenchmark/fb_folly_autobuild
   python3 install.py
   ```

   The script sequentially performs system dependency check, Clang detection, source code download, optimization patch application, Folly/Fizz/Wangle/FbThrift installation, Thrift code generation, and benchmark compilation.

2. Check after success.

   ```output
   WORK/ins/fbthrift/bin/thrift1
   AccLibBenchmark/fbthrift_folly_benchmark/build/press_server
   AccLibBenchmark/fbthrift_folly_benchmark/build/press_client
   ```

   When the source code directory already exists, `AUTO_REENTRY=True` reuses the Git checkout and preserves local modifications. When changing the compiler or encountering CMake Cache conflicts, clean only the build directories of each component and rerun the script. Do not delete the source code directory.

### Running Performance Matrices with run.py

1. Before running, modify the following content in `run.py`.

   - `EXECUTABLE`: point to the actual `press_client`.
   - `PREFIX_CMD`: confirm the server IP, port, transport, NUMA node, and io_uring switch.
   - `CONN_SETUPS`, `QD_SETUPS`, `DATA_SETUPS`: first use a small matrix to verify the environment.
   - `OUTPUT_CSV`: set the result file name to avoid overwriting existing results.

2. Start `press_server` first, and then run the following commands:

   ```bash
   cd AccLibBenchmark/fb_folly_autobuild
   python3 run.py
   ```

   The script runs `press_client` item by item and writes the QPS, throughput, average latency, P99 latency, success rate, and client/server CPU utilization to the CSV.

## Change History

|Release|Date|Description|
|:---|:---|:---|
|01|2026-9-30|This is the first official release.|
