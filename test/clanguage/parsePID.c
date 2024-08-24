#include <stdio.h>
#include <stdlib.h>
#include <regex.h>
#include <string.h>

#define BUFFER_SIZE 1024

void extract_pids(const char *input_file_path, const char *output_file_path) {
    FILE *input_file;
    FILE *output_file;
    char *data;
    size_t file_size;
    regex_t regex;
    regmatch_t matches[2];
    const char *pattern = "^([0-9]+)\\s";
    size_t read_size;

    // 编译正则表达式
    if (regcomp(&regex, pattern, REG_EXTENDED) != 0) {
        fprintf(stderr, "Could not compile regex\n");
        exit(1);
    }

    // 打开输入文件
    input_file = fopen(input_file_path, "rb");
    if (input_file == NULL) {
        perror("Error opening input file");
        regfree(&regex);
        exit(1);
    }

    // 获取文件大小
    fseek(input_file, 0, SEEK_END);
    file_size = ftell(input_file);
    fseek(input_file, 0, SEEK_SET);

    // 分配内存并读取整个文件内容
    data = (char *)malloc(file_size + 1);
    if (data == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        fclose(input_file);
        regfree(&regex);
        exit(1);
    }

    read_size = fread(data, 1, file_size, input_file);
    if (read_size != file_size) {
        fprintf(stderr, "Error reading file\n");
        free(data);
        fclose(input_file);
        regfree(&regex);
        exit(1);
    }
    data[file_size] = '\0'; // 添加字符串终止符

    // 打开输出文件
    output_file = fopen(output_file_path, "w");
    if (output_file == NULL) {
        perror("Error opening output file");
        free(data);
        fclose(input_file);
        regfree(&regex);
        exit(1);
    }

    // 在整个文件内容中匹配正则表达式并写入输出文件
    const char *current_pos = data;
    while (regexec(&regex, current_pos, 2, matches, 0) == 0) {
        // 写入匹配的 PID 到输出文件
        fprintf(output_file, "%.*s\n", (int)(matches[1].rm_eo - matches[1].rm_so), current_pos + matches[1].rm_so);
        current_pos += matches[1].rm_eo;
    }

    // 释放资源
    free(data);
    fclose(input_file);
    fclose(output_file);
    regfree(&regex);
}

int main(int argc, char *argv[]) {
    // 检查命令行参数
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input_file_path> <output_file_path>\n", argv[0]);
        return 1;
    }

    // 调用提取函数
    extract_pids(argv[1], argv[2]);

    return 0;
}
