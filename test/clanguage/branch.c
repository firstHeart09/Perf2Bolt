#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint64_t from;
    uint64_t to;
    char mispred;
} LBREntry;

void reportError(const char *message) {
    fprintf(stderr, "Error: %s\n", message);
}

int parseHexField(const char *str, uint64_t *num) {
    char *endptr;
    *num = strtoull(str, &endptr, 16);
    if (*endptr != '\0') {
        return 1;
    }
    return 0;
}

int parseLBREntry(const char *str, LBREntry *entry) {
    char *token;
    char tempStr[256];
    strncpy(tempStr, str, 256);

    // Parse From address
    token = strtok(tempStr, "/");
    if (parseHexField(token, &entry->from)) {
        reportError("expected hexadecimal number with From address");
        return 1;
    }

    // Parse To address
    token = strtok(NULL, "/");
    if (parseHexField(token, &entry->to)) {
        reportError("expected hexadecimal number with To address");
        return 1;
    }

    // Parse Misprediction bit
    token = strtok(NULL, "/");
    if (token == NULL || (token[0] != 'P' && token[0] != 'M' && token[0] != '-')) {
        reportError("expected single char for mispred bit");
        return 1;
    }
    entry->mispred = token[0];

    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
        return 1;
    }

    FILE *file = fopen(argv[1], "r");
    if (!file) {
        perror("Error opening file");
        return 1;
    }

    char line[1024];
    while (fgets(line, sizeof(line), file)) {
        char *token = strtok(line, " ");
        while (token != NULL) {
            LBREntry entry;
            if (strchr(token, '/') && parseLBREntry(token, &entry) == 0) {
                printf("From: 0x%lx, To: 0x%lx, Mispred: %c\n", entry.from, entry.to, entry.mispred);
            }
            token = strtok(NULL, " ");
        }
    }

    fclose(file);
    return 0;
}
