echo 'hello'
set -e # 遇到错误立即退出

cat <<EOF >> /root/.gitconfig
[url "https://gh-proxy.test.osinfra.cn/https://github.com/"]
insteadOf = https://github.com/
EOF

echo "-------- [INFO !!!] branch '${branch}' --------"
case "${branch}" in
	master|br_bk26_930)
    
        git clone --recurse-submodules --branch v2022.11.14.00 --single-branch https://github.com/facebook/fbthrift.git fbthrift
        cd fbthrift
        patch -p1 < ../fbthrift_folly.patch

		;;

    *)
        ;;
esac

#编译fbthrift
# 强制清理旧的 build_ 目录，防止 CMakeCache.txt 残留导致 "extra path" 警告
rm -rf build_
mkdir build_ && cd build_

# 2. 验证上一级目录确实是 fbthrift 源码根目录
if [ ! -f "../CMakeLists.txt" ]; then
    echo "❌ 错误: 上一级目录 ($(pwd)/..) 中没有找到 CMakeLists.txt！"
    echo "当前工作目录可能不在 fbthrift 源码根目录下。"
    ls -la ..
    exit 1
fi

cmake .. -DCMAKE_BUILD_TYPE=Release -DTHRIFT_ENABLE_ARM_SVE2=ON  \
 -DCMAKE_INSTALL_PREFIX=/usr/local/fbthrift  \
 -Dfmt_DIR=/usr/local/fmt/lib/cmake/fmt  \
 -Dfolly_DIR=/usr/local/folly/lib/cmake/folly  \
 -Dfizz_DIR=/usr/local/fizz/lib/cmake/fizz  \
 -Dwangle_DIR=/usr/local/wangle
make -j && make install