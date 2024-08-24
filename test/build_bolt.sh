#!/bin/bash

# 设定BOLT的安装路径
INSTALL_PREFIX="$HOME/tools/bolt"
mkdir -p $INSTALL_PREFIX  # -p选项会在目录已存在时避免错误

# 进入源码安装build目录
cd $HOME/source/llvm-project/build

# 使用指定路径的cmake命令进行安装，并指定BOLT的安装路径
cmake -G Ninja ../llvm \
  -DLLVM_TARGETS_TO_BUILD="X86;AArch64" \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_ENABLE_ASSERTIONS=ON \
  -DLLVM_ENABLE_PROJECTS="bolt" \
  -DCMAKE_INSTALL_PREFIX=$INSTALL_PREFIX

# 使用ninja进行编译
ninja bolt

# 安装BOLT到指定路径
ninja install

