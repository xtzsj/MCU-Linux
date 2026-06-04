// AT 响应解析辅助函数，处理常见数值格式。
#include "at_parser.h"
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

// 跳过空白字符，提升解析容错。
static const char *skip_spaces(const char *p) {
    while (*p != '\0' && isspace((unsigned char)*p)) {
        ++p;
    }
    return p;
}

int at_has_prefix(const char *line, const char *prefix) {
    if (line == NULL || prefix == NULL) {
        return 0;
    }
    return strncmp(line, prefix, strlen(prefix)) == 0;
}

// 解析冒号后的第 N 个整数，例如 "+CSQ: 22,99"。
int at_parse_int_after_colon(const char *line, int index, int *out_value) {
    const char *p = strchr(line, ':');
    int current = 0;

    if (p == NULL || out_value == NULL) {
        return 0;
    }

    p = skip_spaces(p + 1);
    while (*p != '\0') {
        char *end_ptr = NULL;
        long val = strtol(p, &end_ptr, 10);
        if (p == end_ptr) {
            return 0;
        }
        if (current == index) {
            *out_value = (int)val;
            return 1;
        }
        current += 1;
        p = end_ptr;
        while (*p != '\0' && *p != ',') {
            ++p;
        }
        if (*p == ',') {
            p = skip_spaces(p + 1);
        }
    }
    return 0;
}

int at_parse_int_pair(const char *line, int *out_a, int *out_b) {
    if (out_a == NULL || out_b == NULL) {
        return 0;
    }
    if (!at_parse_int_after_colon(line, 0, out_a)) {
        return 0;
    }
    if (!at_parse_int_after_colon(line, 1, out_b)) {
        return 0;
    }
    return 1;
}
