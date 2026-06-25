# 快速入门

## 环境要求

- 已验证的OS：Debian 12等支持ARM SVE2指令集的Linux系统。
- 已验证的编译器：clang-16或更高版本。
- CPU要求：Compact Protocol SVE2优化需要支持SVE2指令集的CPU，Binary Protocol优化无特殊CPU要求。
- 系统依赖：需要安装相关依赖包。

## 使能FbThrift序列化优化

本优化方案针对FbThrift的Compact Protocol和Binary Protocol进行整型数组批量编码优化。Compact Protocol引入基于ARM SVE2的批量Varint编码，Binary Protocol引入基于`memcpy`和`bswap`的编译器自动向量化优化。两种优化均通过SFINAE机制在编译期自动选择最优路径，对下游业务代码零侵入。

1. 获取优化后的FbThrift源码。

   ```bash
   git clone -b dev_20221114 https://gitcode.com/boostkit/fbthrift.git
   cd fbthrift
   ```

2. 在Debian系系统上，需要安装以下依赖。

   ```bash
   apt install libboost-all-dev libdouble-conversion-dev libgflags-dev \
   libgoogle-glog-dev libevent-dev libsodium-dev liblz4-dev libsnappy-dev \
   liblzma-dev libgtest-dev libgmock-dev libssl-dev libaio-dev \
   libunwind-dev libdwarf-dev binutils-dev libiberty-dev zlib1g-dev libbz2-dev
   ```

3. 安装前置依赖。建议将前置依赖包安装在一个固定路径，使用统一编译器Clang 16。

   ```bash
   export CC=/usr/bin/clang-16
   export CXX=/usr/bin/clang++-16
   ```

   1. 安装zstd。

      ```bash
      wget https://github.com/facebook/zstd/releases/download/v1.4.5/zstd-1.4.5.tar.gz
      tar xzf zstd-1.4.5.tar.gz && cd zstd-1.4.5
      make -j
      make PREFIX=/usr/local/ install
      ```

   2. 安装fmt。

      ```bash
      wget https://github.com/fmtlib/fmt/archive/refs/tags/8.0.1.tar.gz
      tar xzf 8.0.1.tar.gz && cd fmt-8.0.1
      mkdir build && cd build
      cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local/fmt
      make -j && make install
      ```

   3. 安装zlib。

      ```bash
      wget https://www.zlib.net/fossils/zlib-1.2.13.tar.gz
      tar xzf zlib-1.2.13.tar.gz && cd zlib-1.2.13
      ./configure --prefix=/usr/local/
      make -j && make install
      ```

   4. 安装boost。

      ```bash
      wget https://archives.boost.io/release/1.78.0/source/boost_1_78_0.tar.gz
      tar xzf boost_1_78_0.tar.gz && cd boost_1_78_0
      ./bootstrap.sh --with-toolset=clang --prefix=/usr/local/boost_1_78_0
      ./b2 install
      ```

4. 编译folly。

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

   > **说明：**
   > - `-DCMAKE_INSTALL_PREFIX`可替换为自定义的folly安装目的地址。
   > - `-DCMAKE_PREFIX_PATH`可替换为依赖包安装的位置，如fmt安装的cmake路径。

5. 编译fizz。

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

6. 编译wangle。

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

7. 编译FbThrift。

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

   > **说明：**
   > - `-DTHRIFT_ENABLE_ARM_SVE2=ON`：启用Compact Protocol的SVE2优化。若目标CPU不支持SVE2，可省略此选项，此时仅Binary Protocol优化生效，Compact Protocol回退到scalar路径。
   > - `-DCMAKE_INSTALL_PREFIX`可替换为自定义的FbThrift安装目的地址。
   > - 各`-Dxxx_DIR`参数需替换为实际安装路径。

## 性能基准测试（Benchmark）

优化方案在支持SVE2的AArch64机器上，使用Google Benchmark框架进行测试。

1. 获取测试框架代码。

   ```bash
   git clone https://gitcode.com/boostkit/AccLibBenchmark.git
   cd AccLibBenchmark/fbthrift-opt-benchmarks
   ```

2. 修改`config.sh`中的`FBTHRIFT_HOME`为实际FbThrift安装目录。

   ```bash
   export FBTHRIFT_HOME=/usr/local/fbthrift
   ```

3. 编译与运行。

   - 测试Compact Protocol（默认）

     ```bash
     bash compile-and-run.sh
     ```

   - 测试Binary Protocol

     ```bash
     bash compile-and-run.sh binary
     ```
