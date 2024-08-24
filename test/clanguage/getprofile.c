#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <inttypes.h>
#include <assert.h>

/*全局变量定义*/
#define MAX_COMMAND_LENGTH 1024  // 最大命令长度
#define TEMP_FILE_TEMPLATE "/home/dushuai/study/bolt/test/perf_output_XXXXXX"  // 临时文件目录
#define TEMP_MMAP_FILE "/home/dushuai/study/bolt/test/perf_mmap"

/*结构体定义*/
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

typedef struct MMapNode {
    MMapInfo info;
    struct MMapNode *next;
} MMapNode;

/*函数定义*/
// 执行perf命令相关
bool checkPerfDataMagic(const char *FileName);
char* find_perf_path();
void execute_perf_command(const char *perf_path, const char *filename, const char *args, int id);
// 处理mmap事件相关
void parseMMapEvents(FILE *file, MMapNode **head);
void addMMapInfo(MMapNode **head, MMapInfo *info);
void printMMapInfo(MMapNode *head, FILE *outputFile);
void freeMMapInfo(MMapNode *head);
int isDuplicate(MMapNode *head, MMapInfo *info);
int isValidPID(int pid);
int isDeletedFile(const char *fileName);


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
            MMapNode *head = NULL;
            parseMMapEvents(output_file, &head);
            char final_output_path[] = TEMP_MMAP_FILE;
            FILE *final_output_file = fopen(final_output_path, "w");
            if (final_output_file) {
                printMMapInfo(head, final_output_file);
                fclose(final_output_file);
                printf("Processed mmap events saved to: %s\n", final_output_path);
            } else {
                perror("fopen final_output_path");
            }
            freeMMapInfo(head);
            break;
        case 4:
            printf("Processing output for ID 4 from file: %s\n", temp_file_path);
            break;
        default:
            printf("Unknown ID: %d\n", id);
            break;
    }

    fclose(output_file);
    // unlink(temp_file_path);
}

// 解析mmap events事件
void parseMMapEvents(FILE *file, MMapNode **head) {
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
                    fileNameStart++;
                    char *fileNameEnd = strrchr(fileNameStart, '/');
                    if (fileNameEnd) {
                        fileNameEnd++;
                        char *newLine = strchr(fileNameEnd, '\n');
                        if (newLine) *newLine = '\0';
                        strncpy(info.FileName, fileNameEnd, MAX_FILENAME_LENGTH - 1);
                        info.FileName[MAX_FILENAME_LENGTH - 1] = '\0';
                        if (strlen(info.FileName) == 0 || isDeletedFile(info.FileName)) {
                            continue;
                        }
                        if (isDuplicate(*head, &info)) {
                            continue;
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
            return 1;
        }
        current = current->next;
    }
    return 0;
}

int isValidPID(int pid) {
    return pid != -1;
}

int isDeletedFile(const char *fileName) {
    return strcmp(fileName, "(deleted)") == 0;
}
