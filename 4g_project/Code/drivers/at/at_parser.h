#ifndef AT_PARSER_H
#define AT_PARSER_H

#include <stdint.h>

int at_parse_int_after_colon(const char *line, int index, int *out_value);
int at_parse_int_pair(const char *line, int *out_a, int *out_b);
int at_has_prefix(const char *line, const char *prefix);

#endif
