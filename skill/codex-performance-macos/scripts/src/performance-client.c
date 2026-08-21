#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "performance-client.h"
#include "performance-protocol.h"

int performance_default_socket_path(char path[PATH_MAX]) {
    const char *override = getenv("CODEX_PERFORMANCE_KEEPER_SOCKET");
    if (override && *override) {
        return snprintf(path, PATH_MAX, "%s", override) < PATH_MAX ? 0 : -1;
    }
    const char *home = getenv("HOME");
    if (!home || !*home) return -1;
    return snprintf(path, PATH_MAX, "%s/.codex/run/performance-keeper.sock", home) <
                   PATH_MAX ? 0 : -1;
}

int performance_register_pid(const char *socket_path, pid_t pid) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_un address;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    if (snprintf(address.sun_path, sizeof(address.sun_path), "%s", socket_path) >=
        (int)sizeof(address.sun_path)) {
        close(fd);
        errno = ENAMETOOLONG;
        return -1;
    }
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }
    struct performance_register_request request = {
        htonl(PERFORMANCE_REGISTER_MAGIC),
        htonl(PERFORMANCE_REGISTER_VERSION),
        (int32_t)htonl((uint32_t)pid),
        0,
    };
    const unsigned char *cursor = (const unsigned char *)&request;
    size_t remaining = sizeof(request);
    while (remaining > 0) {
        ssize_t written = write(fd, cursor, remaining);
        if (written < 0) {
            if (errno == EINTR) continue;
            close(fd);
            return -1;
        }
        cursor += (size_t)written;
        remaining -= (size_t)written;
    }
    close(fd);
    return 0;
}
