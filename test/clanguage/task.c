#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <inttypes.h>
#include <assert.h>
#include <limits.h>

#define MAX_COMMAND_LENGTH 1024  // 最大命令长度
#define TEMP_FILE_TEMPLATE "/home/dushuai/study/bolt/test/perf_output_XXXXXX"  // 临时文件目录
#define TEMP_MMAP_FILE "/home/dushuai/study/bolt/test/perf_mmap"
#define BUFFER_SIZE 1024
#define MAX_MMAP_INFO 1000

/* 结构体定义 */
// mmap事件相关
#define MAX_LINE_LENGTH 1024
#define MAX_FILENAME_LENGTH 256

typedef struct {
    uint64_t Time;
    int PID;
    uint64_t MMapAddress;
    uint64_t Size;
    uint64_t Offset;
    char FileName[MAX_FILENAME_LENGTH];
    int forked;
} MMapInfo;

MMapInfo BinaryMMapInfo[MAX_MMAP_INFO];
int BinaryMMapInfoSize = 0;

/* 函数定义 */
// 执行perf命令相关
bool checkPerfDataMagic(const char *FileName);
char* find_perf_path();
void execute_perf_command(const char *perf_path, const char *filename, const char *args, int id);
// 处理mmap事件相关
void parseMMapEvents(FILE *file);
void printMMapInfo(FILE *outputFile);
int isDuplicate(MMapInfo *info);
int isValidPID(int pid);
int isDeletedFile(const char *fileName);
// 处理task events相关
MMapInfo* findMMapInfo(int pid);
void removeMMapInfo(int pid);
int parseCommExecEvent(const char *line, int *pid);
int parseTaskEvents(FILE *file);

int main(int argc, char *argv[]) {
    assert(argc >= 2 && "Usage: <filename> is required");

    if (checkPerfDataMagic(argv[1])) {
        char *perf_path = find_perf_path();
        assert(perf_path != NULL && "Failed to find perf path");
        printf("perf路径为：%s\n", perf_path);
        execute_perf_command(perf_path, argv[1], "--show-mmap-events --no-itrace", 3);
        execute_perf_command(perf_path, argv[1], "--show-task-events --no-itrace", 4);
        execute_perf_command(perf_path, argv[1], "-F pid,ip,brstack", 1);
        execute_perf_command(perf_path, argv[1], "-F pid,event,addr,ip", 2);
    } else {
        printf("File does not contain the magic number 'PERFILE'.\n");
    }

    return 0;
}

// 检查是否是perf性能文件
bool checkPerfDataMagic(const char *FileName) {
    FILE *file = fopen(FileName, "rb");
    if (file == NULL) {
        return false;
    }

    char Buf[7] = {0, 0, 0, 0, 0, 0, 0};
    size_t BytesRead = fread(Buf, 1, sizeof(Buf), file);
    fclose(file);

    if (BytesRead != 7) {
        return false;
    }

    return strncmp(Buf, "PERFILE", 7) == 0;
}

// 寻找本地的perf命令的路径
char* find_perf_path() {
    static char path[128];
    FILE *fp;

    fp = popen("which perf", "r");
    if (fp == NULL) {
        perror("popen");
        return NULL;
    }

    if (fgets(path, sizeof(path), fp) != NULL) {
        char *newline = strchr(path, '\n');
        if (newline) {
            *newline = '\0';
        }
        pclose(fp);
        return path;
    } else {
        pclose(fp);
        return NULL;
    }
}

// 执行perf命令
void execute_perf_command(const char *perf_path, const char *filename, const char *args, int id) {
    char command[MAX_COMMAND_LENGTH];
    char temp_file_path[256];
    int ret;

    snprintf(temp_file_path, sizeof(temp_file_path), TEMP_FILE_TEMPLATE);
    int fd = mkstemp(temp_file_path);
    assert(fd != -1 && "Failed to create temporary file");
    close(fd);

    int len = snprintf(command, sizeof(command), "%s script %s -f -i %s > %s", perf_path, args, filename, temp_file_path);

    if (len >= sizeof(command)) {
        printf("Warning: Command was truncated. Length = %d\n", len);
        return;
    }

    printf("Executing command: %s\n", command);
    
    ret = system(command);
    if (ret != 0) {
        printf("Failed to execute command: %s\n", command);
        printf("system() returned: %d\n", ret);
        unlink(temp_file_path);
        return;
    }

    printf("Output saved to: %s\n", temp_file_path);

    FILE *output_file = fopen(temp_file_path, "r");
    assert(output_file != NULL && "Failed to open output file");

    switch (id) {
        case 1:
            printf("Processing output for ID 1 from file: %s\n", temp_file_path);
            break;
        case 2:
            printf("Processing output for ID 2 from file: %s\n", temp_file_path);
            break;
        case 3:
            printf("Processing output for ID 3 from file: %s\n", temp_file_path);
            parseMMapEvents(output_file);
            FILE *final_output_file = fopen(TEMP_MMAP_FILE, "w");
            if (final_output_file) {
                printMMapInfo(final_output_file);
                fclose(final_output_file);
                printf("Processed mmap events saved to: %s\n", TEMP_MMAP_FILE);
            } else {
                perror("fopen final_output_path");
            }
            break;
        case 4:
            printf("Processing output for ID 4 from file: %s\n", temp_file_path);
            int result = parseTaskEvents(output_file);
            if (result != 0) {
                printf("Failed to parse task events.\n");
            }
            break;
        default:
            printf("Unknown ID: %d\n", id);
            break;
    }

    fclose(output_file);
    // unlink(temp_file_path);
}

// 解析mmap events事件
void parseMMapEvents(FILE *file) {
    char line[MAX_LINE_LENGTH];
    while (fgets(line, sizeof(line), file)) {
        if (strstr(line, "PERF_RECORD_MMAP2")) {
            MMapInfo info;
            memset(&info, 0, sizeof(info));

            const char *ptr = strstr(line, "PERF_RECORD_MMAP2");
            if (!ptr) {
                continue;
            }

            ptr = strchr(line, ':');
            if (ptr) {
                ptr++;
                char timeStr[64];
                sscanf(ptr, "%63s", timeStr);
                info.Time = strtoull(timeStr, NULL, 10);
            }

            ptr = strchr(ptr, '/');
            if (ptr) {
                ptr++;
                char pidStr[64];
                const char *pidEnd = strchr(ptr, ' ');
                if (pidEnd) {
                    size_t pidLen = pidEnd - ptr;
                    strncpy(pidStr, ptr, pidLen);
                    pidStr[pidLen] = '\0';
                    if (sscanf(pidStr, "%d", &info.PID) != 1 || !isValidPID(info.PID)) {
                        continue;
                    }
                }
            }

            ptr = strchr(ptr, '[');
            if (ptr) {
                ptr++;
                if (sscanf(ptr, "0x%lx(0x%lx) @ 0x%lx", &info.MMapAddress, &info.Size, &info.Offset) != 3) {
                    continue;
                }
            }

            ptr = strchr(ptr, ']');
            if (ptr) {
                ptr++;
                while (*ptr == ' ' || *ptr == ':') ptr++;
                char *fileNameStart = strchr(ptr, '/');
                if (fileNameStart) {
                    strncpy(info.FileName, fileNameStart, MAX_FILENAME_LENGTH - 1);
                    info.FileName[MAX_FILENAME_LENGTH - 1] = '\0';
                }
            }

            if (!isDuplicate(&info)) {
                BinaryMMapInfo[BinaryMMapInfoSize++] = info;
            }
        }
    }
}

// 打印mmap信息到文件
void printMMapInfo(FILE *outputFile) {
    for (int i = 0; i < BinaryMMapInfoSize; ++i) {
        fprintf(outputFile, "Time: %" PRIu64 "\n", BinaryMMapInfo[i].Time);
        fprintf(outputFile, "PID: %d\n", BinaryMMapInfo[i].PID);
        fprintf(outputFile, "MMapAddress: 0x%lx\n", BinaryMMapInfo[i].MMapAddress);
        fprintf(outputFile, "Size: 0x%lx\n", BinaryMMapInfo[i].Size);
        fprintf(outputFile, "Offset: 0x%lx\n", BinaryMMapInfo[i].Offset);
        fprintf(outputFile, "FileName: %s\n", BinaryMMapInfo[i].FileName);
        fprintf(outputFile, "Forked: %d\n\n", BinaryMMapInfo[i].forked);
    }
}

// 判断是否有重复
int isDuplicate(MMapInfo *info) {
    for (int i = 0; i < BinaryMMapInfoSize; ++i) {
        if (BinaryMMapInfo[i].PID == info->PID &&
            BinaryMMapInfo[i].MMapAddress == info->MMapAddress &&
            BinaryMMapInfo[i].Size == info->Size &&
            BinaryMMapInfo[i].Offset == info->Offset &&
            strcmp(BinaryMMapInfo[i].FileName, info->FileName) == 0) {
            return 1;
        }
    }
    return 0;
}

// 判断PID是否有效
int isValidPID(int pid) {
    return pid >= 0 && pid <= INT_MAX;
}

// 判断文件是否被删除
int isDeletedFile(const char *fileName) {
    return strstr(fileName, "(deleted)") != NULL;
}

// 查找PID
MMapInfo* findMMapInfo(int pid) {
    for (int i = 0; i < BinaryMMapInfoSize; ++i) {
        if (BinaryMMapInfo[i].PID == pid) {
            return &BinaryMMapInfo[i];
        }
    }
    return NULL;
}

// 从列表中移除PID
void removeMMapInfo(int pid) {
    if (BinaryMMapInfoSize == 0) return;  // 如果数组为空，直接返回

    for (int i = 0; i < BinaryMMapInfoSize; ++i) {
        if (BinaryMMapInfo[i].PID == pid && BinaryMMapInfo[i].forked) {
            for (int j = i; j < BinaryMMapInfoSize - 1; ++j) {
                BinaryMMapInfo[j] = BinaryMMapInfo[j + 1];
            }
            BinaryMMapInfoSize--;
            return;  // 找到并移除后直接返回
        }
    }
}

// 解析 "PERF_RECORD_COMM exec" 行中的 PID
int parseCommExecEvent(const char *line, int *pid) {
    const char *search_str = "PERF_RECORD_COMM exec:";
    const char *pos = strstr(line, search_str);
    if (!pos) {
        return -1;
    }
    pos += strlen(search_str);

    const char *colon = strchr(pos, ':');
    if (!colon) {
        return -1;
    }
    const char *slash = strchr(colon, '/');
    if (!slash) {
        return -1;
    }

    char pid_str[32];
    strncpy(pid_str, colon + 1, slash - colon - 1);
    pid_str[slash - colon - 1] = '\0';

    char *endptr;
    long int parsed_pid = strtol(pid_str, &endptr, 10);
    if (*endptr != '\0' || parsed_pid > INT_MAX || parsed_pid < INT_MIN) {
        return -1;
    }

    *pid = (int)parsed_pid;
    return 0;
}

// 处理task events的数据
int parseTaskEvents(FILE *file) {
    char buffer[BUFFER_SIZE];
    while (fgets(buffer, sizeof(buffer), file)) {
        int pid;
        if (parseCommExecEvent(buffer, &pid) == 0) {
            printf("Parsed PID: %d\n", pid);

            MMapInfo *info = findMMapInfo(pid);
            if (info && info->forked) {  // 如果找到了info并且它是forked
                removeMMapInfo(pid);
            }
        }
    }
    return 0;
}
