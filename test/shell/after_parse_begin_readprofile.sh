#!/bin/bash

# 检查参数数量
if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <perf.data> <exec>"
    exit 1
fi

# 获取传入参数的绝对路径
get_absolute_path() {
    local file="$1"
    if command -v realpath >/dev/null 2>&1; then
        # 使用 realpath 获取绝对路径
        if [ -e "$file" ]; then
            echo "$(realpath "$file")"
        else
            echo "文件不存在: $file"
            exit 1
        fi
    else
        echo "realpath 命令未找到，请安装 realpath 或使用支持的命令"
        exit 1
    fi
}

# 复制参数路径
arg1=$(get_absolute_path "$1")
arg2=$(get_absolute_path "$2")
executable_name=$(basename "$arg2")  # 获取可执行文件的名称（去除路径）

# 获取 objdump 路径
objdump_path=$(which objdump)

# 使用 objdump 进行反汇编
$objdump_path -d "$arg2" | awk '
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

# 执行 readelf 命令，获取关于可执行文件的相关信息
readelf_path=$(which readelf)
$readelf_path -l "$arg2" | awk '
BEGIN {
    # 初始化变量
    FirstAllocAddress = 0xFFFFFFFFFFFFFFFF  # 设置为无穷大
    NextAvailableAddress = 0
    NextAvailableOffset = 0
    pagesize = 2097152  # 2MB
    UseGnuStack = 0
    IsLinuxKernel = 0
    phnum = 0 # 程序段首部表的个数
    ELF64LEPhdrTy_Size = 56  # 来自我的输出（不确定一定对）

    print "  Type    Offset    VirtAddr    PhysAddr   FileSiz    MemSiz    Flags  Align" > "perf_temp_readelf_temp.log"
    print "---------------------------------------------------------------" >> "perf_temp_readelf_temp.log"
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
    split(line_load, fields, " ")
    # 解析地址和大小
    vaddr = strtonum(fields[3])
    memsz = strtonum(fields[6])
    offset = strtonum(fields[2])
    filesz = strtonum(fields[5])
    # 更新变量
    if (vaddr < FirstAllocAddress) {
        FirstAllocAddress = vaddr
    }
    if ((vaddr + memsz) > NextAvailableAddress) {
        NextAvailableAddress = vaddr + memsz
    }
    if ((offset + filesz) > NextAvailableOffset) {
        NextAvailableOffset = offset + filesz
    }
    # 输出当前 LOAD 段
    print line_load  >> "perf_temp_readelf_temp.log"
}

# 下面这部分脚本主要用于计算程序段首部表的表项个数
/Section to Segment mapping:/ {
    startProcessing = 1
    next
}

startProcessing {
    # 打印当前行以调试
    if (/[0-9]+/) {
        # 处理以数字开头的行
        phnum += 1
    }
}

END {
    FirstNonAllocatableOffset = NextAvailableOffset

    # 对齐操作
    NextAvailableAddress = int((NextAvailableAddress + pagesize - 1) / pagesize) * pagesize
    NextAvailableOffset = int((NextAvailableOffset + pagesize - 1) / pagesize) * pagesize

    # 大页操作省略

    # 用于在 ELF 文件中创建新的程序头表(PHDR table),特别是在使用 BOLT 工具进行二进制重排时
    if (! UseGnuStack  && !IsLinuxKernel){
        if (NextAvailableOffset <= NextAvailableAddress - FirstAllocAddress)
            NextAvailableOffset = NextAvailableAddress - FirstAllocAddress
        else
            NextAvailableAddress = NextAvailableOffset + FirstAllocAddress
        PHDRTableAddress = NextAvailableAddress
        PHDRTableOffset = NextAvailableOffset

        # Reserve space for 3 extra pheaders.
        phnum += 3

        NextAvailableAddress += phnum * ELF64LEPhdrTy_Size
        NextAvailableOffset += phnum * ELF64LEPhdrTy_Size
    }
    fi
    # 根据cacheline对其
    NextAvailableAddress = int((NextAvailableAddress + 64 - 1) / 64) * 64
    NextAvailableOffset = int((NextAvailableOffset + 64 - 1) / 64) * 64

    NewTextSegmentAddress = NextAvailableAddress;
    NewTextSegmentOffset = NextAvailableOffset;
    LayoutStartAddress = NextAvailableAddress;
    
    # 输出结果
    print "FirstAllocAddress", sprintf("%d", FirstAllocAddress)  >> "perf_temp_readelf_temp.log"
    print "LayoutStartAddress", sprintf("%d", LayoutStartAddress)  >> "perf_temp_readelf_temp.log"
}
'

# 处理函数符号信息
$readelf_path -s "$arg2" | awk '
BEGIN {
    flag = 0
    while((getline line < "perf_temp_objdump.log") > 0){
	    # print line
	    split(line, arr)
	    Address = arr[2]
	    Size = arr[3]
	    name = arr[1]
	    data[name] = Address
	    size[name] = Size
	    names[++n] = name
    }
    close("perf_temp_objdump.log")
}

/Symbol table \047\.symtab\047 contains/ {
    flag = 1
    next
}

flag && $4 == "FUNC" {
    func_size = strtonum($3)
    func_address = strtonum("0x" $2)
    func_name = $NF

    # 处理函数符号
    if (func_name ~ /@GLIBC/ || func_size == 0) {
        # 使用从 perf_temp_objdump.log 中读取的数据
        func_name = name
        func_address = data[func_name]
        func_size = size[func_name]
    }
    
    # 存储函数信息
    data[func_name] = func_address
    size[func_name] = func_size
    names[++n] = func_name
}

END {
    # 按地址排序
    PROCINFO["sorted_in"] = "@val_num_asc"

    for (i = 1; i <= n; i++) {
        name = names[i]
        addr_arr[name] = data[name]
        size_arr[name] = size[name]
    }

    for (name in addr_arr) {
        printf "%-40s %-20d %d\n", name, addr_arr[name], size_arr[name] > "perf_temp_func.log"
    }
}
'

# 获取readelf命令执行结果文件所在的绝对路径
perf_readelf_path=$(get_absolute_path "perf_temp_readelf_temp.log")

# 打印参数
echo "要解析的perf.data文件的路径为：$arg1"

# 获取 perf 可执行文件路径
perf_path=$(which perf)

# 判断 perf 是否存在
if [ -z "$perf_path" ]; then
    echo "perf 未安装"
    exit 1
fi

# 打印 perf 路径
echo "perf的可执行文件路径：$perf_path"

# 执行 perf 命令获取输出结果
$perf_path script -F pid,ip,brstack -f -i "$arg1" &> perf_branch.log
$perf_path script -F pid,event,addr,ip -f -i "$arg1" &> perf_mem.log
$perf_path script --show-mmap-events --no-itrace -f -i "$arg1" &> perf_mmap.log
$perf_path script --show-task-events --no-itrace -f -i "$arg1" &> perf_task.log


# 输出完成信息
echo "perf 命令执行完毕，结果已保存到 perf_branch.log、perf_event.log、perf_mmap.log 和 perf_task.log"

# 提取 mmap 信息
perf_mmap_path=$(get_absolute_path "perf_mmap.log")
awk -v exec_name="$executable_name" '
BEGIN {
    print "当前正在解析mmap事件："
    HasFixedLoadAddress=0
    BasicAddress=0
    print "parse mmap events" > "perf_temp_mmap.log"
}

/PERF_RECORD_MMAP2/ {
	PID=$2            # 提取PID
    OFFSET=$8         # 提取偏移量
    MMAPAddr=$6       # 提取mmap地址信息
    split(MMAPAddr, addr_size, /[\(\)\[\]]/)  # 使用split函数分割地址和大小
    ADDR=addr_size[2] # 分割后的第二部分是地址
    SIZE=addr_size[3] # 分割后的第三部分是大小
    filename=$NF
    gsub(".*/", "", filename)

    # print "可执行文件名: " filename, "  exec: " exec_name

    if (filename !~ /\[.*\]/) { # 排除包含方括号的行
        if (!(PID in mmap_info)){
            mmap_info[PID]="unfork"
        }
        print "filename " filename, "  PID " PID, "  MMapAddr " ADDR, "  SIZE " SIZE, "  OFFSET " OFFSET >> "perf_temp_mmap.log"
    }

    # 关闭文件以重置文件指针
    close("perf_temp_readelf_temp.log")

    # 读取readelf -l命令的输出，获取其中的段表项，通过计算获取到可执行文件的基地址
    # 在这里首先判断当前mmap信息的filename是不是可执行文件
    if (filename == exec_name){
        while ((getline line < "perf_temp_readelf_temp.log") > 0) {
            # print "Read line: " line
            if (line ~ /LOAD/) {
                # print "Matching line: " line
                # 按照空格切分该行
                split(line, fields, " ")
                SegInfo_FileOffset = strtonum(fields[2])
                SegInfo_Address = strtonum(fields[3])
                SegInfo_Alignment = strtonum(fields[length(fields)])
                    
                # 处理bolt源码中alignDown(SegInfo.FileOffset, SegInfo.Alignment)这一句
                if(and(SegInfo_FileOffset, (SegInfo_Alignment - 1)) != 0){
                    SegInfo_FileOffset -= and(SegInfo_FileOffset, (SegInfo_Alignment - 1))
                }
                # 处理bolt源码中alignDown(FileOffset, SegInfo.Alignment))这一句
                if(and(OFFSET, (SegInfo_Alignment - 1)) != 0){
                    OFFSET -= and(OFFSET, (SegInfo_Alignment - 1))
                }
                
                if (SegInfo_FileOffset == strtonum(OFFSET)){
                    # # 获取每行对应的mmapAddress
                    # print "当前是哪一行: " line
                    # print "自定义：SegInfo_FileOffset" SegInfo_FileOffset, " OFFSET: " strtonum(OFFSET)
                    # print "自定义：strtonum(ADDR): " strtonum(ADDR)
                    BasicAddress = strtonum(ADDR) - (SegInfo_Address - SegInfo_FileOffset + strtonum(OFFSET))
                    break
                }
            }
        }   
    }
    # print "该行处理完成\n\n"
}

END {
	# 将pid输出到文件中，方便后面解析task事件的时候使用
    for (pid in mmap_info) {
        print "pid " pid, "IsForked " mmap_info[pid] >> "perf_temp_mmap.log"
    } 
    print "BasicAddress: " BasicAddress >> "perf_temp_mmap.log"
    print "mmap事件解析完成"
}
' "$perf_mmap_path"

# 提取 task 信息
perf_task_path=$(get_absolute_path "perf_task.log")
awk '
BEGIN {
    print "当前正在解析task事件："
    # 读取 mmap_info 数据
    while ((getline line < "perf_temp_mmap.log") > 0) {
        if (line ~ /pid/) {
            # 按照空格拆分该行
            split(line, fields, " ")
            pid=fields[2]
            isfork=fields[4]
            print "pid: " pid, "  isfork: " isfork
            mmap_info[pid] = isfork
        }
    }
    close("perf_temp_mmap.log")
}

/PERF_RECORD_COMM/ {
    Mess = $NF
    split(Mess, pid_tid, "[:/]")
    if (length(pid_tid) >= 3) {
        filename = pid_tid[1]
        pid = pid_tid[2]
        tid = pid_tid[3]
        if (pid in mmap_info && mmap_info[pid] == "forked") {
            print "Deleting forked PID:", pid
            delete mmap_info[pid]
        }
    }
}

/PERF_RECORD_FORK/ {
    if (match($0, /PERF_RECORD_FORK\(([0-9]+):[0-9]+\):\(([0-9]+):[0-9]+\)/, arr)) {
        child_pid = arr[1]
        parent_pid = arr[3]
        if (parent_pid != child_pid && parent_pid in mmap_info) {
            print "Adding forked PID:", child_pid
            mmap_info[child_pid] = "forked"
        }
    }
}

END {
    if (length(mmap_info) > 0) {
        print "Keys in mmap_info:"
        for (pid in mmap_info) {
            print "  PID: " pid
        }
    } else {
        print "No PID information found"
    }
}
' "$perf_task_path"

# 初始化关联数组
declare -A FallthroughLBRs
declare -A BranchLBRs

# 提取branch信息
perf_branch_path=$(get_absolute_path "perf_branch.log")
awk -v execname="$executable_name" '
# 函数定义放在 BEGIN 块之外
function initializeFTInfo(trace) {
    FallthroughLBRs[trace] = "0 0"  # InternCount ExternCount
}

function initializeBranchInfo(trace) {
    BranchLBRs[trace] = "0 0 0"  # TakenCount MispredCount HasProfileAvailable
}

BEGIN {
	print "正在处理branch分支事件"
	print "parse branch events" > "perf_temp_branch1.log"
	NumTotalSamples = 0
	NumEntries = 0
	NumSamples = 0
	NumSamplesNoLBR = 0
	NumTrace = 0
    TraceBF = ""
    num_line

	# 该部分变量值要么是写死的，要么是通过命令行获取的
	IgnoreInterruptLBR=1  # 命令行参数
	KernelBaseAddr=0xffff800000000000  # 该地址在源码中是写死的
	HasFixedLoadAddress=0
	UseEventPC = 0  # 由命令行参数给出，在我的测试中该值一直为0
	NeedsSkylakeFix = false

	# 从文件中读取到的数值
	basicAddress = 0  # 基地址
	mmapAddress = 0
	mmapSize = 0
	FirstAllocAddress = 0
	LayoutStartAddress = 0

	lbr_count = 0  # 每个解析出来的LBR条目中拥有的LBR的数量（即每行中LBR的数量）

	# 从mmap解析的结果中获取到部分信息，如基地址信息
	while ((getline line < "perf_temp_mmap.log") > 0){
		# 读取mmap结果文件中的信息获取基地址等信息
		if (line ~ /filename/){
			# 说明此时是reorder等信息
			split(line, arr, " ")
			exec_name = arr[2]
			# print "exec_name" exec_name 
			if (exec_name == execname){
				mmapAddress = arr[6]
				mmapSize = arr[8]
				print "mmapAddress: " mmapAddress, " mmapSize: " mmapSize >> "perf_temp_branch1.log"
			}
		}
		if (line ~ /BasicAddress/){
			# 获取到基地址
			split(line, arr_, " ")
			basicAddress=arr_[2]
			print "basicAddress" basicAddress
		}
	} 
	# 现在已经从perf_temp_mmap.log文件中读取到相关变量，为了不影响下面有文件的读取，再次关闭文件指针
	close("perf_temp_mmap.log")
	# 现在需要打开readelf命令执行的临时文件，获取到FirstAllocAddress和LayoutStartAddress参数
	while((getline line < "perf_temp_readelf_temp.log") > 0){
		# print line
		if (line ~ /FirstAllocAddress/){
			split(line, arr)
			FirstAllocAddress = arr[2]
		}
		if (line ~ /LayoutStartAddress/){
			split(line, arr)
			LayoutStartAddress = arr[2]
		}
	}
	close("perf_temp_readelf.log")
	# 在这里测试两个值读取是否成功
	print "FirstAllocAddress: " FirstAllocAddress, "   LayoutStartAddress: " LayoutStartAddress
	# 读取函数的相关信息
	while((getline line < "perf_temp_func.log") > 0){
		split(line, arr)
		name = arr[1]
		Address = arr[2]
		Size = arr[3]
		data[name] = Address
		size[name] = Size
		names[++n] = name
	}
	close("perf_temp_func.log")
}

# 自定义函数，方便后续处理
function containsAddress_1(Address) {
    return ((Address >= FirstAllocAddress) && (Address < LayoutStartAddress))
}

function containsAddress_2(PC, Address, Size) {
    return ((Address <= PC) && (PC < (Address + Size)))
}

function bc_getBinaryFunctionContainingAddress(Address, CheckPastEnd, UseMaxSize) {
    prev_name = ""
    UsedSize = 0

    # 处理函数的大小，考虑是否使用最大大小
    if (UseMaxSize) {
        size_adjustment = 15
    } else {
        size_adjustment = 0
    }

    for (i_1 = 0; i_1 < n; i_1++) {
        name = names[i_1]
        addr_arr[name] = data[name]
        size_arr[name] = size[name]

        # 查找函数的起始地址小于或等于指定地址
        if (addr_arr[name] <= Address) {
            prev_name = name
            continue
        }

        # 如果没有找到匹配的函数，返回空字符串
        if (i_1 == 0) {
            return ""
        }

        # 计算函数的实际使用大小
        if (UseMaxSize) {
            UsedSize = int((size_arr[prev_name] + size_adjustment) / 16) * 16
        } else {
            UsedSize = size_arr[prev_name]
        }

        # 检查地址是否在函数范围内（考虑 CheckPastEnd 参数）
        if (Address < addr_arr[prev_name] + UsedSize + (CheckPastEnd ? 1 : 0)) {
            address = addr_arr[prev_name]
            return prev_name " " address " " UsedSize
        }

        return ""
    }

    return ""
}


function da_getBinaryFunctionContainingAddress(Address) {
    result_c = containsAddress_1(Address)
    if (!result_c) {
        return ""
    }

    result = bc_getBinaryFunctionContainingAddress(Address, 0, 1)

    if (result == "") {
        return ""
    }

    # print "\n\nReturning result from da_getBinaryFunctionContainingAddress: " result
    return result
}

function create_trace(from, to) {
    return from "," to
}

function compare_traces(trace1, trace2) {
    return trace1 == trace2
}

{
    num_line += 1
    print "当前正在处理第" num_line "行"
	NumTotalSamples += 1
    ++NumSamples
	# 处理文件的每一行
	pid=$1
	pc=$2
	# print "\n\npid: " pid, "  pc: " pc >> "perf_temp_branch1.log"
	if (NF == 2){
		next  # 开始处理下一行
	}
	NextPC = UseEventPC ? pc : 0	
	NumEntry = 0

	for (i=3; i<=NF; i++){
        ++NumEntry
        # print i " : " $i > "1.log"
        # 处理parseBranchSample
		# 循环遍历LBRSamples,并对其进行处理
		# 按照 / 分割LBR Samples
        # print "\n\n" $i >> "perf_temp_branch1.log"
		split($i, arr, "/")
		from = arr[1]
		to = arr[2]
		mispred = (arr[3] ~ "M")
		# print "\n\n解析出来的from: " from, "  to: " to, "  mispred: " mispred >> "perf_temp_branch1.log"
		# print KernelBaseAddr >> "perf_temp_branch1.log"
		from = strtonum(from)
		to = strtonum(to)
		mmapAddress = strtonum(mmapAddress)
		mmapSize = strtonum(mmapSize)
        # print "自定义输出: mmapAddress: " mmapAddress, "   mmapSize: " mmapSize
		# # 上述代码已经从文件中读取出来了LBR条目,接下来就是通过判断是否是内存地址,并且调整LBR值
		# print "判断  from >= KernelBaseAddr: " (from >= KernelBaseAddr) >> "perf_temp_branch1.log"
		# print "判断  to >= KernelBaseAddr: " (to >= KernelBaseAddr)  >> "perf_temp_branch1.log"
		if ((IgnoreInterruptLBR) && ((from >= KernelBaseAddr) || (to >= KernelBaseAddr))){
			continue
		}
		if (!HasFixedLoadAddress){
			if ((from >= mmapAddress) && (from < (mmapAddress + mmapSize))){
				from = from - basicAddress
			} else if (from < mmapSize){
				from = -1
			}
			if ((to >= mmapAddress) && (to < (mmapAddress + mmapSize))){
				to = to - basicAddress
                        } else if (to < mmapSize){
                                to = -1
                        }
			# print "adjust   from: " from, "  to: " to >> "perf_temp_branch1.log"
		}
		# 实现parseparseLBRSample的函数逻辑
        ++NumEntries
		if (and(NeedsSkylakeFix, (NumEntry <= 2))){
			continue
		}
		# print "当前正在处理的PC：NextPC：" NextPC
        # print "当前处理的from和to： " from, "    to: " to
		if(NextPC){
			TraceFrom = to
			TraceTo   = NextPC
            # print "Before da_getBinaryFunctionContainingAddress: TraceFrom:  "  TraceFrom, "   TraceTo: " TraceTo >> "perf_temp_branch1.log"
            TraceBF = da_getBinaryFunctionContainingAddress(TraceFrom)
            # print "After da_getBinaryFunctionContainingAddress: TraceBF:  "  TraceBF >> "perf_temp_branch1.log"
			if(TraceBF){
				split(TraceBF, arr, " ")
				TraceBF_addr = arr[2]
				TraceBF_size = arr[3]
                # print "在调用containsAddress_2之前的判断: 函数名:" arr[1], " addr: " TraceBF_addr, "  size: " TraceBF_size >> "perf_temp_branch1.log"
				if(containsAddress_2(TraceTo, TraceBF_addr, TraceBF_size)){
					trace = create_trace(TraceFrom, TraceTo)
                    if (!(trace in FallthroughLBRs)) {
                        initializeFTInfo(trace)
                    }
                    info = FallthroughLBRs[trace]
                    split(info, counts, " ")
                    InternCount = counts[1]
                    ExternCount = counts[2]
                    # print "containsAddress_2(from, TraceBF_addr, TraceBF_size)的返回值：" containsAddress_2(from, TraceBF_addr, TraceBF_size) >> "perf_temp_branch1.log"
                    if (containsAddress_2(from, TraceBF_addr, TraceBF_size)) {
                        InternCount++
                    } else {
                        ExternCount++
                    }
                    FallthroughLBRs[trace] = InternCount " " ExternCount
		    	}else{
					ToFunc = da_getBinaryFunctionContainingAddress(TraceTo)
				}
			}
			++NumTraces
		}
		NextPC = from
        # print "Before From = da_getBinaryFunctionContainingAddress(from) ? from : 0"
		From = da_getBinaryFunctionContainingAddress(from) ? from : 0
		To = da_getBinaryFunctionContainingAddress(to) ? to : 0
        # print "From: " From, ",  To: " To  >> "perf_temp_branch1.log"
		if (!From && !To){
			continue
		}
		trace = create_trace(From, To)
        if (!(trace in BranchLBRs)) {
            initializeBranchInfo(trace)
        }
        info = BranchLBRs[trace]
        split(info, counts, " ")
        TakenCount = counts[1]
        MispredCount = counts[2]
        HasProfileAvailable = counts[3]
        TakenCount++
        MispredCount += mispred
        BranchLBRs[trace] = TakenCount " " MispredCount " " HasProfileAvailable
	}
}

END {
    print "read " NumSamples, " samples and " NumEntries " LBR entries" >> "perf_temp_branch1.log"
    print "Fallthrough LBRs:"  >> "perf_temp_branch1.log"
    for (trace in FallthroughLBRs) {
        split(FallthroughLBRs[trace], counts, " ")
        print trace " InternCount: " counts[1] " ExternCount: " counts[2] >> "perf_temp_branch1.log"
    }
    print "\n\nBranch LBRs:"  >> "perf_temp_branch1.log"
    for (trace in BranchLBRs) {
        split(BranchLBRs[trace], counts, " ")  # 从BranchLBRs中拆分出数据和Trace
        split(trace, trace_arr, ",")  # 从trace中拆分出from和to信息
        for(trace_index=1; trace_index<=2; trace_index++){
            temp_addr = trace_arr[trace_index]  # 这个值是bolt源码中的for (const uint64_t Addr : {Trace.From, Trace.To})这一部分
            if(new_func = da_getBinaryFunctionContainingAddress(temp_addr)){
                # BranchLBRs中设置该行Trace数据有用
                HasProfileAvailable = 1
                BranchLBRs[trace] = counts[1] " " counts[2] " " HasProfileAvailable
		split(BranchLBRs[trace], counts, " ")
                print trace " TakenCount: " counts[1] " MispredCount: " counts[2] " HasProfileAvailable: " counts[3] >> "perf_temp_branch1.log"
                print "找到的func信息为： " new_func >> "perf_temp_branch1.log"
            }
        }
        # print trace " TakenCount: " counts[1] " MispredCount: " counts[2] " HasProfileAvailable: " counts[3] >> "perf_temp_branch1.log"
    }
}
' "$perf_branch_path"
