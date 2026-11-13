# 使用Debian 12官方基础镜像
FROM swr.cn-north-4.myhuaweicloud.com/cloud_boostkit/debian12:latest

# 设置非交互模式与时区
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/Shanghai

# ==========================================
# 1. 配置 APT 源 + 系统更新 + 安装基础依赖
# ==========================================
RUN echo "配置华为云APT源..." && \
    mkdir -p /etc/apt/preferences.d && \
    # 使用 echo 逐行写入华为云镜像（兼容传统 Docker Builder）
    echo "deb https://mirrors.huaweicloud.com/debian bookworm main contrib non-free non-free-firmware" > /etc/apt/sources.list && \
    echo "deb https://mirrors.huaweicloud.com/debian bookworm-updates main contrib non-free non-free-firmware" >> /etc/apt/sources.list && \
    echo "deb https://mirrors.huaweicloud.com/debian-security bookworm-security main contrib non-free non-free-firmware" >> /etc/apt/sources.list && \
    # 验证写入结果
    cat /etc/apt/sources.list && \
    # 更新并安装系统级依赖
    apt-get update && \
    apt-get upgrade -y && \
    apt-get install -y --no-install-recommends \
        apt-utils bash curl wget ca-certificates gnupg2 lsb-release \
        software-properties-common aptitude gcc g++ make cmake ninja-build \
        autoconf automake libtool pkg-config git maven python3-pip unzip \
        rsync numactl sudo rpm cpio meson byacc bison flex binutils \
        binutils-dev doxygen dos2unix patch zip zlib1g zlib1g-dev strace \
        expect openssh-client openssh-server jq libc6-dev uuid-dev \
        libaio-dev libncurses5-dev libncursesw5-dev libcunit1 libcunit1-dev \
        libjson-c-dev libcmocka-dev libiscsi-dev libboost-all-dev \
        libbenchmark-dev python3 python3-dev python3-requests python3-pyelftools \
        python3-gevent python3-marshmallow python3-yaml python3-pandas \
        python3-xlrd python3-retrying python3-jenkins python3-concurrent-log-handler \
        python3-openpyxl alien perl perl-modules libperl-dev tar bzip2 \
        openssl libhdf5-dev build-essential libssl-dev vim lcov libedit-dev \
        libxml2-dev libtinfo-dev liblzma-dev libffi-dev xz-utils \
        openjdk-17-jdk openjdk-17-jre libgoogle-perftools-dev clang-format \
        libdouble-conversion-dev libgflags-dev libgoogle-glog-dev libevent-dev \
        libsodium-dev liblz4-dev libsnappy-dev libgtest-dev libgmock-dev \
        clang-16 clang libdwarf-dev libiberty-dev libbz2-dev && \
    apt-get clean && rm -rf /var/lib/apt/lists/* && \
    ln -sf /bin/bash /bin/sh

# ==========================================
# 2. 配置 Git 代理 & 编译器环境变量
# ==========================================
RUN mkdir -p /root && \
    echo '[url "https://gh-proxy.test.osinfra.cn/https://github.com/"]' >> /root/.gitconfig && \
    echo '    insteadOf = https://github.com/' >> /root/.gitconfig

# 设置编译器与库搜索路径（持久化到镜像）
ENV CC=/usr/bin/clang-16
ENV CXX=/usr/bin/clang++-16
ENV LD_LIBRARY_PATH=/usr/local/lib:/usr/local/fmt/lib:/usr/local/folly/lib:/usr/local/fizz/lib:/usr/local/wangle/lib:$LD_LIBRARY_PATH
ENV CMAKE_PREFIX_PATH=/usr/local/fmt;/usr/local/folly;/usr/local/fizz;/usr/local/wangle

WORKDIR /tmp/build_deps

# ==========================================
# 3. 编译安装 zstd-1.4.5
# ==========================================
RUN wget --no-check-certificate https://buildtools.obs.cn-north-4.myhuaweicloud.com/zstd-1.4.5.tar.gz && \
    tar xzf zstd-1.4.5.tar.gz && cd zstd-1.4.5 && \
    make -j$(nproc) && make PREFIX=/usr/local install && \
    ldconfig && cd /tmp/build_deps && rm -rf zstd-1.4.5*

# ==========================================
# 4. 编译安装 fmt-8.0.1
# ==========================================
RUN wget --no-check-certificate https://buildtools.obs.cn-north-4.myhuaweicloud.com/fmt-8.0.1.tar.gz && \
    tar xzf fmt-8.0.1.tar.gz && cd fmt-8.0.1 && \
    mkdir build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local/fmt && \
    make -j$(nproc) && make install && \
    ldconfig && cd /tmp/build_deps && rm -rf fmt-8.0.1*

# ==========================================
# 5. 编译安装 zlib-1.2.13
# ==========================================
RUN wget --no-check-certificate https://buildtools.obs.cn-north-4.myhuaweicloud.com/zlib-1.2.13.tar.gz && \
    tar xzf zlib-1.2.13.tar.gz && cd zlib-1.2.13 && \
    ./configure --prefix=/usr/local && \
    make -j$(nproc) && make install && \
    ldconfig && cd /tmp/build_deps && rm -rf zlib-1.2.13*

# ==========================================
# 6. 编译安装 boost_1_78_0
# ==========================================
RUN wget --no-check-certificate https://buildtools.obs.cn-north-4.myhuaweicloud.com/boost_1_78_0.tar.gz && \
    tar xzf boost_1_78_0.tar.gz && cd boost_1_78_0 && \
    ./bootstrap.sh --with-toolset=clang --prefix=/usr/local/boost_1_78_0 && \
    ./b2 install -j$(nproc) && \
    ldconfig && cd /tmp/build_deps && rm -rf boost_1_78_0*

# ==========================================
# 7. 编译安装 folly-2022.11.14.00
# ==========================================
RUN wget --no-check-certificate https://buildtools.obs.cn-north-4.myhuaweicloud.com/folly-2022.11.14.00.zip && \
    unzip folly-2022.11.14.00.zip && cd folly-2022.11.14.00 && \
    mkdir -p _build && cd _build && \
    cmake .. \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_STANDARD=17 \
      -DBUILD_BENCHMARKS=OFF \
      -DBUILD_TESTS=OFF \
      -DCMAKE_INSTALL_PREFIX=/usr/local/folly \
      -DBUILD_SHARED_LIBS=ON \
      -DCMAKE_PREFIX_PATH="/usr/local/fmt/lib/cmake/fmt/" && \
    make -j$(nproc) && make install && \
    ldconfig && cd /tmp/build_deps && rm -rf folly-2022.11.14.00*

# ==========================================
# 8. 编译安装 fizz-2022.11.14.00
# ==========================================
RUN wget --no-check-certificate https://buildtools.obs.cn-north-4.myhuaweicloud.com/fizz-2022.11.14.00.zip && \
    unzip fizz-2022.11.14.00.zip && cd fizz-2022.11.14.00 && \
    mkdir build_ && cd build_ && \
    cmake ../fizz/ \
      -Dfmt_DIR=/usr/local/fmt/lib/cmake/fmt \
      -Dfolly_DIR=/usr/local/folly/lib/cmake/folly \
      -DCMAKE_INSTALL_PREFIX=/usr/local/fizz && \
    make -j$(nproc) && make install && \
    ldconfig && cd /tmp/build_deps && rm -rf fizz-2022.11.14.00*

# ==========================================
# 9. 编译安装 wangle-2022.11.14.00
# ==========================================
RUN wget --no-check-certificate https://buildtools.obs.cn-north-4.myhuaweicloud.com/wangle-2022.11.14.00.zip && \
    unzip wangle-2022.11.14.00.zip && cd wangle-2022.11.14.00 && \
    mkdir build_ && cd build_ && \
    cmake ../wangle \
      -Dfmt_DIR=/usr/local/fmt/lib/cmake/fmt \
      -Dfolly_DIR=/usr/local/folly/lib/cmake/folly \
      -Dfizz_DIR=/usr/local/fizz/lib/cmake/fizz \
      -DCMAKE_INSTALL_PREFIX=/usr/local/wangle \
      -DBUILD_SHARED_LIBS=ON && \
    make -j$(nproc) && make install && \
    ldconfig && cd /tmp/build_deps && rm -rf wangle-2022.11.14.00*

# 清理编译临时文件
RUN rm -rf /tmp/build_deps

# 工作目录重置
WORKDIR /workspace

# 默认启动命令
CMD ["/bin/bash"]