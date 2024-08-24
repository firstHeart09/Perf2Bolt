#!/bin/bash

# 获取 objdump 路径
objdump_path=$(which objdump)

# 使用 objdump 进行反汇编
$objdump_path -d "$1" | awk '
BEGIN {
    in_disassembly_section = 0
    func_name = ""
    func_address = 0
    num_inst = 0
}

/^Disassembly of section/ {
    in_disassembly_section = 1
    next
}

in_disassembly_section && /^[0-9a-f]{16} <[a-zA-Z0-9_@\.]+>:/ {
    if (func_name != "") {
        # 如果 func_name 不在 data 中，则存储函数名、地址和指令大小
        if (!(func_name in data)) {
            print func_name, func_address, num_inst > "perf_temp_objdump.log"
        }
    }
    # 更新当前函数名和地址
    func_instructions = ""
    address = $1
    if (match($0, /<([a-zA-Z0-9_@\.]+)>:/, arr)) {
        func_name = arr[1]
        # 检查是否是 ".plt"
        if (func_name ~ /\.plt$/) {
            func_name = "__BOLT_PSEUDO_" func_name
            # print "Renamed function: " func_name
        }
    } else {
        func_name = "unknown"
    }
    func_address = strtonum("0x" address)
    num_inst = 0  # Reset instruction count for the new function
}

in_disassembly_section && /^[ \t]+[0-9a-f]+:/ {
    # 存储指令并计数
    for (i=2; i<=NF; i++) {
        if ($i ~ /^[0-9a-f]{2}$/) {
            func_instructions = func_instructions $i " "
            num_inst++
        }
    }
}

END {
    if (func_name != "") {
        print func_name, func_address, num_inst > "perf_temp_objdump.log"
    }
}
'
