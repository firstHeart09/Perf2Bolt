#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <inttypes.h>


#define BUFFER_SIZE 1024
#define MAX_MMAP_INFO 1000

typedef struct {
    int PID;
    uint64_t MMapAddress;
    uint64_t Size;
    int forked;
} MMapInfo;

typedef struct {
    int ParentPID;
    int ChildPID;
    unsigned long Time;
} ForkInfo;

MMapInfo MMapInfoArray[MAX_MMAP_INFO];
int MMapInfoSize = 0;

// 查找指定PID的MMapInfo
MMapInfo* findMMapInfo(int pid) {
    for (int i = 0; i < MMapInfoSize; ++i) {
        if (MMapInfoArray[i].PID == pid) {
            return &MMapInfoArray[i];
        }
    }
    return NULL;
}

// 插入新的MMapInfo
void insertMMapInfo(MMapInfo *info) {
    if (MMapInfoSize < MAX_MMAP_INFO) {
        MMapInfoArray[MMapInfoSize++] = *info;
    } else {
        fprintf(stderr, "MMapInfoArray is full!\n");
    }
}

// 从列表中移除PID
void removeMMapInfo(int pid) {
    for (int i = 0; i < MMapInfoSize; ++i) {
        if (MMapInfoArray[i].PID == pid && MMapInfoArray[i].forked) {
            for (int j = i; j < MMapInfoSize - 1; ++j) {
                MMapInfoArray[j] = MMapInfoArray[j + 1];
            }
            MMapInfoSize--;
            return;
        }
    }
}

// 解析 "PERF_RECORD_COMM exec" 行中的 PID
int parseCommExecEvent(const char *line, int *pid) {
    const char *search_str = "PERF_RECORD_COMM exec:";
    const char *pos = strstr(line, search_str);
    if (!pos) return -1;
    pos += strlen(search_str);

    char pid_str[32];
    if (sscanf(pos, " %31[^/]/%31s", pid_str, pid_str) != 2) return -1;

    char *endptr;
    long parsed_pid = strtol(pid_str, &endptr, 10);
    if (*endptr != '\0' || parsed_pid > INT_MAX || parsed_pid < INT_MIN) return -1;

    *pid = (int)parsed_pid;
    return 0;
}

// 解析 "FORK" 行中的信息
int parseForkEvent(const char *line, ForkInfo *info) {
    return sscanf(line, "FORK %d %d %lu", &info->ParentPID, &info->ChildPID, &info->Time) == 3 ? 0 : -1;
}

// 处理task events的数据
int parseTaskEvents(FILE *file) {
    char buffer[BUFFER_SIZE];
    printf("PERF2BOLT: parsing perf-script task events output\n");

    while (fgets(buffer, sizeof(buffer), file)) {
        int pid;
        if (parseCommExecEvent(buffer, &pid) == 0) {
            printf("Parsed PID: %d\n", pid);

            // 获取MMapInfo
            MMapInfo *info = findMMapInfo(pid);
            if (info && info->forked) {
                // 打印格式: PID: (Address: Size)
                printf("%d: (0x%lx: 0x%lx)\n", info->PID, info->MMapAddress, info->Size);
            } else {
                printf("No MMapInfo found for PID %d\n", pid);
            }
            continue;
        }

        ForkInfo forkInfo;
        if (parseForkEvent(buffer, &forkInfo) != 0) continue;

        if (forkInfo.ParentPID == forkInfo.ChildPID || forkInfo.Time == 0) continue;

        // 添加调试信息
        printf("Processing ForkEvent: ParentPID: %d, ChildPID: %d, Time: %lu\n",
               forkInfo.ParentPID, forkInfo.ChildPID, forkInfo.Time);

        MMapInfo *parentInfo = findMMapInfo(forkInfo.ParentPID);
        if (!parentInfo) continue;

        MMapInfo childInfo = *parentInfo;
        childInfo.PID = forkInfo.ChildPID;
        childInfo.forked = 1;
        insertMMapInfo(&childInfo);

        // 打印插入的子进程信息
        printf("Inserted Child MMapInfo: PID: %d, Address: 0x%lx, Size: 0x%lx\n",
               childInfo.PID, childInfo.MMapAddress, childInfo.Size);
    }

    return 0;
}

// 主函数
int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <task_events_file>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *filename = argv[1];
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Error opening file");
        return EXIT_FAILURE;
    }

    int result = parseTaskEvents(file);
    fclose(file);

    return result;
}
