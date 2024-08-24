#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#define INITIAL_LBR_CAPACITY 10
#define INITIAL_LINE_SIZE 1024
#define INITIAL_EXTRA_FIELDS_SIZE 4

const uint64_t KernelBaseAddr = 0xffff800000000000;
bool IgnoreInterruptLBR = true;

int num_error = 0;
FILE *logFile;  // 用于日志文件的句柄

typedef struct {
    uint64_t from;
    uint64_t to;
    int mispred;
    char **extraFields;
    size_t extraFieldsCount;
} LBREntry;

typedef struct {
    LBREntry *LBR;
    size_t LBRCount;
    size_t LBRCapacity;
    uint64_t PC;
    uint64_t PID;
} PerfBranchSample;

/*
 * 该结构体的功能：保存二进制文件相关信息
 * */
typedef struct {
    uint64_t FirstAllocAddress;
    uint64_t LayoutStartAddress;
} BinaryLayout;


/*
 * 该函数主要功能，通过readelf -l获取的布局信息
 * */
 uint64_t getAddress(){

 }

/*
 * 该函数的主要功能：判断地址信息是否在布局信息内
 * */
bool containsAddress(uint64_t Address) {
    return Address >= FirstAllocAddress && Address < LayoutStartAddress;
} 

/*
 * 该函数的主要功能：
 * TODO:
 * */
uint64_t BC_getBinaryFunctionContainingAddress(uint64_t Address, bool CheckPastEnd, bool UseMaxSize){

}


/*
 * 该函数的主要功能：获取二进制文件的地址信息
 * */
 uint64_t DA_getBinaryFunctionContainingAddress(uint64_t Address){
    if(!containsAddress(Address)){
        return NULL;
    }
    return BC_getBinaryFunctionContainingAddress(Address, false, true);
 }

/*
 * 该函数的主要功能：解析LBR的控制函数
 * */
bool parseLBREntry(const char *str, LBREntry *entry) {
    char *token;
    char *endptr;
    size_t extraFieldsCapacity = INITIAL_EXTRA_FIELDS_SIZE;
    entry->extraFields = (char **)malloc(extraFieldsCapacity * sizeof(char *));
    if (entry->extraFields == NULL) {
        fprintf(logFile, "Error allocating memory for extraFields\n");
        return false;
    }
    entry->extraFieldsCount = 0;

    char *strCopy = strdup(str);
    if (!strCopy) {
        fprintf(logFile, "Error duplicating string for LBR entry\n");
        free(entry->extraFields);
        return false;
    }

    token = strtok(strCopy, "/");
    if (token == NULL) {
        fprintf(logFile, "Error: expected hexadecimal number for From address\n");
        free(strCopy);
        free(entry->extraFields);
        return false;
    }
    entry->from = strtoull(token, &endptr, 16);
    if (*endptr != '\0') {
        fprintf(logFile, "Error: invalid hexadecimal number for From address\n");
        free(strCopy);
        free(entry->extraFields);
        return false;
    }

    token = strtok(NULL, "/");
    if (token == NULL) {
        fprintf(logFile, "Error: expected hexadecimal number for To address\n");
        free(strCopy);
        free(entry->extraFields);
        return false;
    }
    entry->to = strtoull(token, &endptr, 16);
    if (*endptr != '\0') {
        fprintf(logFile, "Error: invalid hexadecimal number for To address\n");
        free(strCopy);
        free(entry->extraFields);
        return false;
    }

    token = strtok(NULL, "/");
    if (token == NULL || (token[0] != 'P' && token[0] != 'M' && token[0] != '-')) {
        fprintf(logFile, "Error: expected single char for mispred bit, found: %s\n", token);
        free(strCopy);
        free(entry->extraFields);
        return false;
    }
    entry->mispred = (token[0] == 'M');

    // 处理额外字段
    while ((token = strtok(NULL, "/")) != NULL) {
        if (entry->extraFieldsCount == extraFieldsCapacity) {
            extraFieldsCapacity *= 2;
            char **newExtraFields = (char **)realloc(entry->extraFields, extraFieldsCapacity * sizeof(char *));
            if (newExtraFields == NULL) {
                fprintf(logFile, "Error reallocating memory for extraFields\n");
                for (size_t i = 0; i < entry->extraFieldsCount; ++i) {
                    free(entry->extraFields[i]);
                }
                free(entry->extraFields);
                free(strCopy);
                return false;
            }
            entry->extraFields = newExtraFields;
        }
        entry->extraFields[entry->extraFieldsCount] = strdup(token);
        if (entry->extraFields[entry->extraFieldsCount] == NULL) {
            fprintf(logFile, "Error duplicating string for extraField\n");
            for (size_t i = 0; i < entry->extraFieldsCount; ++i) {
                free(entry->extraFields[i]);
            }
            free(entry->extraFields);
            free(strCopy);
            return false;
        }
        entry->extraFieldsCount++;
    }

    fprintf(logFile, "From: 0x%lx To: 0x%lx Mispred: %d", entry->from, entry->to, entry->mispred);
    for (size_t i = 0; i < entry->extraFieldsCount; i++) {
        fprintf(logFile, "  %s", entry->extraFields[i]);
    }
    fprintf(logFile, "\n");

    free(strCopy);
    return true;
}


/*
 * 该函数的主要功能：判断地址是否位于内存范围
 * */
bool ignoreKernelInterrupt(LBREntry *LBR) {
    return IgnoreInterruptLBR &&
           (LBR->from >= KernelBaseAddr || LBR->to >= KernelBaseAddr);
}


/*
 * 该函数的主要功能：具体的处理Branch事件的代码
 * */
PerfBranchSample parseBranchSample(const char *line) {
    PerfBranchSample sample = {0};
    sample.LBR = NULL;
    sample.LBRCount = 0;
    sample.LBRCapacity = INITIAL_LBR_CAPACITY;
    sample.PC = 0;
    sample.PID = 0;

    char *lineCopy = strdup(line);
    if (!lineCopy) {
        fprintf(logFile, "Error duplicating line\n");
        return sample;
    }

    char *pidStr = strtok(lineCopy, " ");
    if (!pidStr) {
        fprintf(logFile, "Error: PID not found.\n");
        free(lineCopy);
        return sample;
    }
    sample.PID = strtoull(pidStr, NULL, 10);

    char *pcStr = strtok(NULL, " ");
    if (!pcStr) {
        fprintf(logFile, "Error: PC not found.\n");
        free(lineCopy);
        return sample;
    }
    sample.PC = strtoull(pcStr, NULL, 16);

    fprintf(logFile, "\n\nPID: %ld  PC: 0x%lx\n", sample.PID, sample.PC);

    char *restStr = strtok(NULL, "\n");
    if (!restStr) {
        fprintf(logFile, "Error: Rest of line not found.\n");
        free(lineCopy);
        return sample;
    } else {
        sample.LBR = (LBREntry *)malloc(sample.LBRCapacity * sizeof(LBREntry));
        if (!sample.LBR) {
            fprintf(logFile, "Error allocating memory for LBR entries\n");
            free(lineCopy);
            return sample;
        }

        char *restStrCopy = strdup(restStr);
        char *token;
        char *saveptr;

        token = strtok_r(restStrCopy, " ", &saveptr);
        while (token != NULL) {
            if (sample.LBRCount >= sample.LBRCapacity) {
                sample.LBRCapacity *= 2;
                LBREntry *newLBR = (LBREntry *)realloc(sample.LBR, sample.LBRCapacity * sizeof(LBREntry));
                if (!newLBR) {
                    fprintf(logFile, "Error reallocating memory for LBR entries\n");
                    for (size_t i = 0; i < sample.LBRCount; ++i) {
                        for (size_t j = 0; j < sample.LBR[i].extraFieldsCount; ++j) {
                            free(sample.LBR[i].extraFields[j]);
                        }
                        free(sample.LBR[i].extraFields);
                    }
                    free(sample.LBR);
                    free(lineCopy);
                    free(restStrCopy);
                    sample.LBR = NULL;
                    return sample;
                }
                sample.LBR = newLBR;
            }

            char *buffer = strdup(token);
            if (buffer == NULL) {
                fprintf(logFile, "Error duplicating string for LBR entry\n");
                for (size_t i = 0; i < sample.LBRCount; ++i) {
                    for (size_t j = 0; j < sample.LBR[i].extraFieldsCount; ++j) {
                        free(sample.LBR[i].extraFields[j]);
                    }
                    free(sample.LBR[i].extraFields);
                }
                free(sample.LBR);
                free(lineCopy);
                free(restStrCopy);
                sample.LBR = NULL;
                return sample;
            }

            if (!parseLBREntry(buffer, &sample.LBR[sample.LBRCount])) {
                for (size_t i = 0; i < sample.LBRCount; ++i) {
                    for (size_t j = 0; j < sample.LBR[i].extraFieldsCount; ++j) {
                        free(sample.LBR[i].extraFields[j]);
                    }
                    free(sample.LBR[i].extraFields);
                }
                free(sample.LBR);
                free(lineCopy);
                free(restStrCopy);
                free(buffer);
                sample.LBR = NULL;
                return sample;
            }
            free(buffer);
            if (ignoreKernelInterrupt(&sample.LBR[sample.LBRCount])) {
                for (size_t j = 0; j < sample.LBR[sample.LBRCount].extraFieldsCount; ++j) {
                    free(sample.LBR[sample.LBRCount].extraFields[j]);
                }
                free(sample.LBR[sample.LBRCount].extraFields);
                sample.LBRCount--;
            }
            sample.LBRCount++;
            token = strtok_r(NULL, " ", &saveptr);
        }
        free(lineCopy);
        free(restStrCopy);
    }
    // fprintf(logFile, "sample.LBRCount: %d\n", sample.LBRCount);
    return sample;
}

/*
 * 该函数主要功能：解析LBR信息的函数
 * TODO:
 * */
uint64_t parseLBRSample(PerfBranchSample sample, bool needsSkylakeFix) {
    uint64_t numTraces = 0;
    uint64_t NextPC = 0;
    uint32_t NumEntry = 0;
    for (size_t i = 0; i < sample.LBRCount; ++i) {
        ++NumEntry;
        if (needsSkylakeFix && NumEntry <= 2){
            continue;
        }
        if (NextPC){
            const uint64_t TraceFrom = sample.LBR.To;
            const uint64_t TraceTo = NextPC;
            

        }
        NextPC = sample.LBR[i].From;
        uint64_t From = getBinaryFunctionContainingAddress(sample.LBR.From) ? sample.LBR.From : 0;
        uint64_t To = getBinaryFunctionContainingAddress(sample.LBR.To) ? sample.LBR.To : 0;
        if (!From && !To) {
            continue;
        }
        // TakenBranchInfo &Info = BranchLBRs[Trace(From, To)];
        // ++Info.TakenCount;
        // Info.MispredCount += LBR.Mispred;
    }
    return numTraces;
}


/*
 * 该函数的主要功能：解析分支的主控函数
 * */
int parseBranchEvents(const char *filename) {
    logFile = fopen("branch_events.log", "w");  // 打开日志文件
    if (!logFile) {
        perror("Error opening log file");
        return 1;
    }

    uint64_t NumTotalSamples = 0;
    uint64_t NumEntries = 0;
    uint64_t NumSamples = 0;
    uint64_t NumSamplesNoLBR = 0;
    uint64_t NumTraces = 0;
    bool NeedsSkylakeFix = false;

    FILE *file = fopen(filename, "r");
    if (!file) {
        fprintf(logFile, "Error opening file: %s\n", filename);
        fclose(logFile);
        return 1;
    }

    char *line = NULL;
    size_t len = INITIAL_LINE_SIZE;
    ssize_t read = 0;

    line = (char *)malloc(len);
    if (!line) {
        fprintf(logFile, "Error allocating memory for line\n");
        fclose(file);
        fclose(logFile);
        return 1;
    }

    while ((read = getline(&line, &len, file)) != -1) {
        ++NumTotalSamples;
        PerfBranchSample sample = parseBranchSample(line);
        if (sample.LBR == NULL) {
            continue;
        }
        ++NumSamples;

        NumEntries += sample.LBRCount;
        fprintf(logFile, "sample.LBRCount: %d\n", sample.LBRCount);
        if (sample.LBRCount == 0) {
            NumSamplesNoLBR++;
        } 
        NumTraces += parseLBRSample(sample, NeedsSkylakeFix);
    }

    free(line);
    fclose(file);

    fprintf(logFile, "Total Samples: %ld\n", NumTotalSamples);
    fprintf(logFile, "Total Entries: %ld\n", NumEntries);
    fprintf(logFile, "Total Samples Parsed: %ld\n", NumSamples);
    fprintf(logFile, "Total Samples with No LBR: %ld\n", NumSamplesNoLBR);
    fprintf(logFile, "Total Traces: %ld\n", NumTraces);
    fprintf(logFile, "Total Errors: %d\n", num_error);

    fclose(logFile);  // 关闭日志文件
    return 0;
}

/*
 * main函数定义
 * */
int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
        return 1;
    }

    return parseBranchEvents(argv[1]);
}
