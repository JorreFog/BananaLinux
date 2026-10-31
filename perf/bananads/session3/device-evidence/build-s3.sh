#!/bin/bash
# KMS-capable BananaDS cross-builds of the four phase heads, exactly as the Buildroot package configures them
H=/home/joar/BananaLinux/output/rgds-plus/host
export PATH=$H/bin:$PATH PKG_CONFIG=$H/bin/pkg-config
TAR=/tmp/claude-1000/-home-joar/9d034c73-4406-4c92-9aec-b3102cd387de/scratchpad/melonds-dl/906e9ebb27da8c6a715cd7abab4abfe8a8d29427.tar.gz
for n in p3 base p1 p2; do
  M=/home/joar/sd-s3/melonds-$n; O=/home/joar/sd-s3/obj-$n
  rm -rf $M; mkdir -p $M; tar xzf $TAR --strip-components=1 -C $M
  echo "== $n: cmake $(date +%H:%M:%S)"
  cmake -S /home/joar/sd-s3/$n/bananads -B $O -G Ninja -DCMAKE_TOOLCHAIN_FILE=$H/share/buildroot/toolchainfile.cmake \
    -DCMAKE_BUILD_TYPE=Release -DBANANADS_MELONDS_DIR=$M -DBANANADS_MELONDS_PATCHES=ON -DBANANADS_JIT=melonds \
    -DBANANADS_SDL=OFF -DBANANADS_ALSA=ON -DBANANADS_KMS=ON -DBANANADS_LAN=OFF -DBANANADS_CPU=cortex-a55 -DBANANADS_LTO=ON \
    -DCMAKE_AR=$H/bin/aarch64-linux-gcc-ar -DCMAKE_RANLIB=$H/bin/aarch64-linux-gcc-ranlib > $O.cmake.log 2>&1 \
    || { echo "$n cmake FAILED"; tail -25 $O.cmake.log; continue; }
  grep -c 'melonDS: applied' $O.cmake.log | sed "s/^/$n patches applied: /"
  echo "== $n: build $(date +%H:%M:%S)"
  nice cmake --build $O -j16 --target bananads > $O.build.log 2>&1 \
    && echo "$n OK $(date +%H:%M:%S) $(ls -la $O/bananads | cut -c25-)" \
    || { echo "$n build FAILED"; grep -E 'error|FAILED' $O.build.log | head -20; }
done
echo "builds done $(date +%H:%M:%S)"
