# Quick Start

## Environment Requirements

- Verified OSs: Linux systems that support the Arm SVE2 instruction set, such as Debian 12
- Verified compilers: Clang 16 or later
- CPU requirements: Compact Protocol SVE2 optimization requires a CPU that supports the SVE2 instruction set; Binary Protocol optimization has no special CPU requirements.
- System dependencies: Related dependency packages need to be installed.

## Enabling fbthrift Serialization Optimization

This solution optimizes batch integer array encoding for fbthrift's Compact Protocol and Binary Protocol. The Compact Protocol introduces batch Varint encoding based on Arm SVE2, and the Binary Protocol introduces automatic compiler vectorization based on `memcpy` and `bswap`. Both optimizations automatically select the optimal path at compile time via the SFINAE mechanism, ensuring zero intrusion into downstream application code.

1. Obtain the optimized fbthrift source code.

   ```bash
   git clone -b dev_20221114 https://gitcode.com/boostkit/fbthrift.git
   cd fbthrift
   ```

2. On a Debian system, install the following dependencies.

   ```bash
   apt install libboost-all-dev libdouble-conversion-dev libgflags-dev \
   libgoogle-glog-dev libevent-dev libsodium-dev liblz4-dev libsnappy-dev \
   liblzma-dev libgtest-dev libgmock-dev libssl-dev libaio-dev \
   libunwind-dev libdwarf-dev binutils-dev libiberty-dev zlib1g-dev libbz2-dev
   ```

3. Install the prerequisite dependencies. You are advised to install the prerequisite dependency packages in a fixed path and use the unified compiler Clang 16.

   ```bash
   export CC=/usr/bin/clang-16
   export CXX=/usr/bin/clang++-16
   ```

   1. Install zstd.

      ```bash
      wget https://github.com/facebook/zstd/releases/download/v1.4.5/zstd-1.4.5.tar.gz
      tar xzf zstd-1.4.5.tar.gz && cd zstd-1.4.5
      make -j
      make PREFIX=/usr/local/ install
      ```

   2. Install fmt.

      ```bash
      wget https://github.com/fmtlib/fmt/archive/refs/tags/8.0.1.tar.gz
      tar xzf 8.0.1.tar.gz && cd fmt-8.0.1
      mkdir build && cd build
      cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local/fmt
      make -j && make install
      ```

   3. Install zlib.

      ```bash
      wget https://www.zlib.net/fossils/zlib-1.2.13.tar.gz
      tar xzf zlib-1.2.13.tar.gz && cd zlib-1.2.13
      ./configure --prefix=/usr/local/
      make -j && make install
      ```

   4. Install boost.

      ```bash
      wget https://archives.boost.io/release/1.78.0/source/boost_1_78_0.tar.gz
      tar xzf boost_1_78_0.tar.gz && cd boost_1_78_0
      ./bootstrap.sh --with-toolset=clang --prefix=/usr/local/boost_1_81_0
      ./b2 install
      ```

4. Compile folly.

   ```bash
   git clone -b v2022.11.14.00 https://github.com/facebook/folly.git
   cd folly
   mkdir -p _build && cd _build
   cmake .. \
   -DCMAKE_BUILD_TYPE=Release \
   -DCMAKE_CXX_STANDARD=17 \
   -DBUILD_BENCHMARKS=OFF \
   -DBUILD_TESTS=OFF \
   -DCMAKE_INSTALL_PREFIX=/usr/local/folly \
   -DBUILD_SHARED_LIBS=ON \
   -DCMAKE_PREFIX_PATH="/usr/local/fmt/lib/cmake/fmt/"
   make -j && make install
   ```

   > **NOTE**
   > - You can set `DCMAKE_INSTALL_PREFIX` to a custom folly installation destination.
   > - You can set `DCMAKE_PREFIX_PATH` to the location where a dependency package is installed, for example, the CMake path for installing fmt.

5. Compile fizz.

   ```bash
   git clone -b v2022.11.14.00 https://github.com/facebookincubator/fizz.git
   cd fizz
   mkdir build_ && cd build_
   cmake ../fizz/ \
   -Dfmt_DIR=/usr/local/fmt/lib/cmake/fmt \
   -Dfolly_DIR=/usr/local/folly/lib/cmake/folly \
   -DCMAKE_INSTALL_PREFIX=/usr/local/fizz
   make -j && make install
   ```

6. Compile wangle.

   ```bash
   git clone -b v2022.11.14.00 https://github.com/facebook/wangle.git
   cd wangle
   mkdir build_ && cd build_
   cmake ../wangle \
   -Dfmt_DIR=/usr/local/fmt/lib/cmake/fmt \
   -Dfolly_DIR=/usr/local/folly/lib/cmake/folly \
   -Dfizz_DIR=/usr/local/fizz/lib/cmake/fizz \
   -DCMAKE_INSTALL_PREFIX=/usr/local/wangle \
   -DBUILD_SHARED_LIBS=ON
   make -j && make install
   ```

7. Compile fbthrift.

   ```bash
   cd fbthrift
   mkdir build_ && cd build_
   cmake .. \
   -DCMAKE_BUILD_TYPE=Release \
   -DTHRIFT_ENABLE_ARM_SVE2=ON \
   -DCMAKE_INSTALL_PREFIX=/usr/local/fbthrift \
   -Dfmt_DIR=/usr/local/fmt/lib/cmake/fmt \
   -Dfolly_DIR=/usr/local/folly/lib/cmake/folly \
   -Dfizz_DIR=/usr/local/fizz/lib/cmake/fizz \
   -Dwangle_DIR=/usr/local/wangle/lib/cmake/wangle
   make -j && make install
   ```

   > **NOTE**
   > - `-DTHRIFT_ENABLE_ARM_SVE2=ON`: Enables the SVE2 optimization for the Compact Protocol. If the target CPU does not support SVE2, this option can be omitted; in this case, only the Binary Protocol optimization takes effect, and the Compact Protocol falls back to the scalar path.
   > - You can set `DCMAKE_INSTALL_PREFIX` to a custom fbthrift installation destination.
   > - Set each `Dxxx_DIR` parameter to the actual installation path.

## Performance Benchmarking

The optimization solution is tested on an AArch64 machine that supports SVE2 using the Google Benchmark framework.

1. Obtain the test framework code.

   ```bash
   git clone https://gitcode.com/boostkit/AccLibBenchmark.git
   cd AccLibBenchmark/fbthrift-opt-benchmarks
   ```

2. Change the value of `FBTHRIFT_HOME` in `config.sh` to the actual fbthrift installation directory.

   ```bash
   export FBTHRIFT_HOME=/usr/local/fbthrift
   ```

3. Perform compilation and running.

   * Test the Compact Protocol (default).

     ```bash
     bash compile-and-run.sh
     ```

   * Test the Binary Protocol.

     ```bash
     bash compile-and-run.sh binary
     ```
