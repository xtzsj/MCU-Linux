#ifndef AT_CLIENT_H
#define AT_CLIENT_H

#include <stddef.h>
#include <stdint.h>

int at_send_command(const char *cmd, char *resp, size_t resp_size, uint32_t timeout_ms);
int at_wait_ok(const char *resp);
int at_wait_error(const char *resp);

#endif
