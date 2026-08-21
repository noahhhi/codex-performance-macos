#include <errno.h>
#include <limits.h>
#include <pthread/qos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

#include "performance-client.h"

#define PRIO_DARWIN_ROLE 6
#define PRIO_DARWIN_ROLE_USER_INIT 0x7

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "usage: codex-performance-register command [argument ...]\n");
        return 64;
    }
    char socket_path[PATH_MAX];
    if (performance_default_socket_path(socket_path) == 0) {
        (void)performance_register_pid(socket_path, getpid());
    }
    (void)setpriority(PRIO_DARWIN_ROLE, (id_t)getpid(), PRIO_DARWIN_ROLE_USER_INIT);
    (void)pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
    execvp(argv[1], &argv[1]);
    fprintf(stderr, "codex-performance-register: cannot run %s: %s\n",
            argv[1], strerror(errno));
    return errno == ENOENT ? 127 : 126;
}
