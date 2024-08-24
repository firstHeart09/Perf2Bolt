#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <unistd.h> // for mkstemp

#define MAX_LINE_LENGTH 1024
#define TEMP_FILE_TEMPLATE "/home/dushuai/study/bolt/test/perf_mmap_dataXXXXXX"
#define MAX_FILENAME_LENGTH 256

// MMapInfo 结构体定义
typedef struct {
    uint64_t Time;
    int PID;
    uint64_t MMapAddress;
    uint64_t Size;
    uint64_t Offset;
    char FileName[MAX_FILENAME_LENGTH];
} MMapInfo;

// MMapNode 结构体定义，用于链表存储
typedef struct MMapNode {
    MMapInfo info;
    struct MMapNode *next;
} MMapNode;

// 函数声明
void parseMMapEvents(FILE *file, MMapNode **head);
void addMMapInfo(MMapNode **head, MMapInfo *info);
void printMMapInfo(MMapNode *head, FILE *outputFile);
void freeMMapInfo(MMapNode *head);
int isDuplicate(MMapNode *head, MMapInfo *info);
int isValidPID(int pid);
int isDeletedFile(const char *fileName);

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
        return EXIT_FAILURE;
    }

    FILE *file = fopen(argv[1], "r");
    if (file == NULL) {
        perror("fopen");
        return EXIT_FAILURE;
    }

    MMapNode *head = NULL;
    parseMMapEvents(file, &head);

    // 创建临时文件
    char tempFileName[] = TEMP_FILE_TEMPLATE;
    int tempFd = mkstemp(tempFileName);
    if (tempFd == -1) {
        perror("mkstemp");
        fclose(file);
        return EXIT_FAILURE;
    }
    FILE *tempFile = fdopen(tempFd, "w");
    if (!tempFile) {
        perror("fdopen");
        close(tempFd);
        fclose(file);
        return EXIT_FAILURE;
    }

    // 将结果输出到临时文件
    printMMapInfo(head, tempFile);
    fclose(tempFile);

    // 输出临时文件的路径
    printf("Results saved to: %s\n", tempFileName);

    // 清理
    freeMMapInfo(head);
    fclose(file);

    return EXIT_SUCCESS;
}

void parseMMapEvents(FILE *file, MMapNode **head) {
    char line[MAX_LINE_LENGTH];
    while (fgets(line, sizeof(line), file)) {
        if (strstr(line, "PERF_RECORD_MMAP2")) {
            MMapInfo info;
            memset(&info, 0, sizeof(info));

            // 查找 "PERF_RECORD_MMAP2"
            const char *ptr = strstr(line, "PERF_RECORD_MMAP2");
            if (!ptr) {
                continue;
            }

            // 提取时间
            ptr = strchr(line, ':');
            if (ptr) {
                ptr++; // Skip the ':'
                char timeStr[64];
                sscanf(ptr, "%63s", timeStr); // 从 ':' 之后提取时间
                info.Time = strtoull(timeStr, NULL, 10);
            }

            // 提取 PID
            ptr = strchr(ptr, '/');
            if (ptr) {
                ptr++;
                char pidStr[64];
                const char *pidEnd = strchr(ptr, ' ');
                if (pidEnd) {
                    size_t pidLen = pidEnd - ptr;
                    strncpy(pidStr, ptr, pidLen);
                    pidStr[pidLen] = '\0';

                    // 将 PID 字符串转换为整数
                    if (sscanf(pidStr, "%d", &info.PID) != 1 || !isValidPID(info.PID)) {
                        continue; // 忽略无效的 PID
                    }
                }
            }

            // 提取地址、大小和偏移
            ptr = strchr(ptr, '[');
            if (ptr) {
                ptr++;
                if (sscanf(ptr, "0x%lx(0x%lx) @ 0x%lx", &info.MMapAddress, &info.Size, &info.Offset) != 3) {
                    continue; // 忽略解析错误
                }
            }

            // 提取文件名并去除标记字段
            ptr = strchr(ptr, ']');
            if (ptr) {
                ptr++;
                while (*ptr == ' ' || *ptr == ':') ptr++;

                // 找到文件名的开始
                char *fileNameStart = strchr(ptr, '/');
                if (fileNameStart) {
                    fileNameStart++;
                    char *fileNameEnd = strrchr(fileNameStart, '/');
                    if (fileNameEnd) {
                        fileNameEnd++;
                        char *newLine = strchr(fileNameEnd, '\n');
                        if (newLine) *newLine = '\0';

                        // 将实际文件名拷贝到 info 中
                        strncpy(info.FileName, fileNameEnd, MAX_FILENAME_LENGTH - 1);
                        info.FileName[MAX_FILENAME_LENGTH - 1] = '\0'; // 确保文件名字符串以 '\0' 结尾

                        if (strlen(info.FileName) == 0 || isDeletedFile(info.FileName)) {
                            continue; // 忽略空文件名或 "(deleted)" 文件
                        }

                        if (isDuplicate(*head, &info)) {
                            continue; // 忽略重复的文件名和 PID
                        }

                        addMMapInfo(head, &info);
                    }
                }
            }
        }
    }
}

void addMMapInfo(MMapNode **head, MMapInfo *info) {
    MMapNode *newNode = (MMapNode *)malloc(sizeof(MMapNode));
    if (!newNode) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    newNode->info = *info;
    newNode->next = *head;
    *head = newNode;
}

void printMMapInfo(MMapNode *head, FILE *outputFile) {
    MMapNode *current = head;
    while (current) {
        fprintf(outputFile, "%s : %d [0x%lx, 0x%lx @ 0x%lx]\n",
                current->info.FileName, current->info.PID, current->info.MMapAddress, current->info.Size, current->info.Offset);
        current = current->next;
    }
}

void freeMMapInfo(MMapNode *head) {
    MMapNode *current = head;
    while (current) {
        MMapNode *temp = current;
        current = current->next;
        free(temp);
    }
}

int isDuplicate(MMapNode *head, MMapInfo *info) {
    MMapNode *current = head;
    while (current) {
        if (current->info.PID == info->PID && strcmp(current->info.FileName, info->FileName) == 0) {
            return 1; // 找到重复的文件名和 PID
        }
        current = current->next;
    }
    return 0;
}

int isValidPID(int pid) {
    return pid != -1;
}

int isDeletedFile(const char *fileName) {
    return strcmp(fileName, "(deleted)") == 0; // 检查文件名是否为 "(deleted)"
}
