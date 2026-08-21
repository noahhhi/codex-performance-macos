#ifndef CODEX_PERFORMANCE_CLIENT_H
#define CODEX_PERFORMANCE_CLIENT_H

#include <limits.h>
#include <sys/types.h>

int performance_default_socket_path(char path[PATH_MAX]);
int performance_register_pid(const char *socket_path, pid_t pid);

#endif
