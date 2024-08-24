#!/bin/bash

# 定义readelf路径和输入文件
readelf_path=$(which readelf)  # 修改为实际的readelf路径
input_file="$1"  # 输入文件名

if [ ! -x "$readelf_path" ]; then
    echo "readelf not found or not executable."
    exit 1
fi

if [ ! -f "$input_file" ]; then
    echo "Input file not found."
    exit 1
fi

# 使用awk处理readelf的输出
$readelf_path -l "$input_file" | awk '
BEGIN {
    # 输出文件的标题行
    print "  Type    Offset    VirtAddr    PhysAddr   FileSiz    MemSiz    Flags  Align" > "readelf_temp.log"
    print "---------------------------------------------------------------" >> "readelf_temp.log"
}

/LOAD/ {
    # 记录当前 LOAD 行
    line_load = $0
    # 将多个空格替换为一个空格
    gsub(/ +/, " ", line_load)
    # 读取下一行
    getline next_line
    # 替换前导空格为单个空格
    gsub(/^ +/, "", next_line)
    # 处理所有与 LOAD 相关的行
    if (match(next_line, /0x[0-9a-fA-F]+/)) {
        line_load = line_load " " next_line
    }
    # 输出当前 LOAD 段
    print line_load >> "readelf_temp.log"
}
'

