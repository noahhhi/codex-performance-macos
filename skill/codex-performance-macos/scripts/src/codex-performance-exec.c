#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <limits.h>
#include <pthread/qos.h>
#include <signal.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/file.h>
#include <unistd.h>

extern char **environ;
static char executable_path[PATH_MAX];
typedef int (*set_darwin_role_fn)(const posix_spawnattr_t *, uint64_t);
typedef int (*set_process_type_fn)(posix_spawnattr_t *, int);
#define PRIO_DARWIN_ROLE_USER_INIT 0x7u
#define PRIO_DARWIN_ROLE 6
#define POSIX_SPAWN_PROC_TYPE_APP_DEFAULT 0x00000100

#define REQUEST_MAGIC 0x43505831u
#define FRAME_OUTPUT 1u
#define FRAME_EXIT 2u
#define MAX_ITEMS 16384u
#define MAX_STRING (1024u * 1024u)
#define MAX_REQUEST (32u * 1024u * 1024u)

struct request_header {
    uint32_t magic;
    uint32_t argc;
    uint32_t envc;
    uint32_t cwd_len;
};

struct frame_header {
    uint32_t type;
    uint32_t length;
};

static int read_full(int fd, void *buffer, size_t length) {
    unsigned char *cursor = buffer;
    while (length > 0) {
        ssize_t count = read(fd, cursor, length);
        if (count == 0) return 1;
        if (count < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        cursor += (size_t)count;
        length -= (size_t)count;
    }
    return 0;
}

static int write_full(int fd, const void *buffer, size_t length) {
    const unsigned char *cursor = buffer;
    while (length > 0) {
        ssize_t count = write(fd, cursor, length);
        if (count < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        cursor += (size_t)count;
        length -= (size_t)count;
    }
    return 0;
}

static const char *qos_name(qos_class_t qos) {
    switch (qos) {
    case QOS_CLASS_USER_INTERACTIVE: return "user-interactive";
    case QOS_CLASS_USER_INITIATED: return "user-initiated";
    case QOS_CLASS_DEFAULT: return "default";
    case QOS_CLASS_UTILITY: return "utility";
    case QOS_CLASS_BACKGROUND: return "background";
    case QOS_CLASS_UNSPECIFIED: return "unspecified";
    default: return "unknown";
    }
}

static int socket_path(char *path, size_t capacity) {
    const char *override = getenv("CODEX_PERFORMANCE_BROKER_SOCKET");
    if (override && *override) {
        if (snprintf(path, capacity, "%s", override) >= (int)capacity) return -1;
        return 0;
    }
    const char *home = getenv("HOME");
    if (!home || !*home) return -1;
    if (snprintf(path, capacity, "%s/.codex/run/performance-broker.sock", home) >= (int)capacity) return -1;
    return 0;
}

static int send_frame(int fd, uint32_t type, const void *data, uint32_t length) {
    struct frame_header header = { htonl(type), htonl(length) };
    if (write_full(fd, &header, sizeof(header)) != 0) return -1;
    return length == 0 || write_full(fd, data, length) == 0 ? 0 : -1;
}

static char *read_string(int fd, size_t *total) {
    uint32_t network_length;
    if (read_full(fd, &network_length, sizeof(network_length)) != 0) return NULL;
    uint32_t length = ntohl(network_length);
    if (length > MAX_STRING || *total > MAX_REQUEST - length) return NULL;
    char *value = malloc((size_t)length + 1);
    if (!value) return NULL;
    if (read_full(fd, value, length) != 0) {
        free(value);
        return NULL;
    }
    value[length] = '\0';
    *total += length;
    return value;
}

static void free_vector(char **values, uint32_t count) {
    if (!values) return;
    for (uint32_t index = 0; index < count; ++index) free(values[index]);
    free(values);
}

static int handle_connection(int client_fd, uid_t expected_uid) {
    uid_t peer_uid;
    gid_t peer_gid;
    if (getpeereid(client_fd, &peer_uid, &peer_gid) != 0 || peer_uid != expected_uid) return 77;
    (void)peer_gid;

    struct request_header network_header;
    if (read_full(client_fd, &network_header, sizeof(network_header)) != 0) return 65;
    struct request_header header = {
        ntohl(network_header.magic), ntohl(network_header.argc),
        ntohl(network_header.envc), ntohl(network_header.cwd_len)
    };
    if (header.magic != REQUEST_MAGIC || header.argc == 0 || header.argc > MAX_ITEMS ||
        header.envc > MAX_ITEMS || header.cwd_len == 0 || header.cwd_len > PATH_MAX) return 65;

    size_t total = header.cwd_len;
    char *cwd = malloc((size_t)header.cwd_len + 1);
    char **argv = calloc((size_t)header.argc + 1, sizeof(char *));
    char **envp = calloc((size_t)header.envc + 1, sizeof(char *));
    if (!cwd || !argv || !envp || read_full(client_fd, cwd, header.cwd_len) != 0) {
        free(cwd); free(argv); free(envp); return 71;
    }
    cwd[header.cwd_len] = '\0';
    for (uint32_t index = 0; index < header.argc; ++index) {
        argv[index] = read_string(client_fd, &total);
        if (!argv[index]) { free(cwd); free_vector(argv, header.argc); free(envp); return 65; }
    }
    for (uint32_t index = 0; index < header.envc; ++index) {
        envp[index] = read_string(client_fd, &total);
        if (!envp[index]) { free(cwd); free_vector(argv, header.argc); free_vector(envp, header.envc); return 65; }
    }

    int output_pipe[2];
    if (pipe(output_pipe) != 0) return 71;
    pid_t child = fork();
    if (child < 0) return 71;
    if (child == 0) {
        close(output_pipe[0]);
        setpgid(0, 0);
        if (chdir(cwd) != 0 || dup2(output_pipe[1], STDOUT_FILENO) < 0 ||
            dup2(output_pipe[1], STDERR_FILENO) < 0) _exit(126);
        close(output_pipe[1]);
        int null_fd = open("/dev/null", O_RDONLY);
        if (null_fd >= 0) { (void)dup2(null_fd, STDIN_FILENO); close(null_fd); }
        environ = envp;
        set_darwin_role_fn set_darwin_role = (set_darwin_role_fn)dlsym(RTLD_DEFAULT, "posix_spawnattr_set_darwin_role_np");
        set_process_type_fn set_process_type = (set_process_type_fn)dlsym(RTLD_DEFAULT, "posix_spawnattr_setprocesstype_np");
        if (!set_darwin_role || !set_process_type) {
            dprintf(STDERR_FILENO, "codex-performance-exec: Darwin spawn policy API unavailable\n");
            _exit(70);
        }
        posix_spawnattr_t attributes;
        int spawn_result = posix_spawnattr_init(&attributes);
        if (spawn_result == 0) spawn_result = set_process_type(&attributes, POSIX_SPAWN_PROC_TYPE_APP_DEFAULT);
        if (spawn_result == 0) spawn_result = set_darwin_role(&attributes, PRIO_DARWIN_ROLE_USER_INIT);
        pid_t workload = -1;
        if (spawn_result == 0) spawn_result = posix_spawnp(&workload, argv[0], NULL, &attributes, argv, envp);
        posix_spawnattr_destroy(&attributes);
        if (spawn_result != 0) {
            dprintf(STDERR_FILENO, "codex-performance-exec: cannot spawn workload: %s\n", strerror(spawn_result));
            _exit(126);
        }
        int workload_status = 0;
        while (waitpid(workload, &workload_status, 0) < 0 && errno == EINTR) {}
        if (WIFEXITED(workload_status)) _exit(WEXITSTATUS(workload_status));
        if (WIFSIGNALED(workload_status)) _exit(128 + WTERMSIG(workload_status));
        _exit(125);
    }

    close(output_pipe[1]);
    unsigned char buffer[16384];
    int client_alive = 1;
    for (;;) {
        ssize_t count = read(output_pipe[0], buffer, sizeof(buffer));
        if (count == 0) break;
        if (count < 0) { if (errno == EINTR) continue; break; }
        if (client_alive && send_frame(client_fd, FRAME_OUTPUT, buffer, (uint32_t)count) != 0) {
            client_alive = 0;
            kill(-child, SIGTERM);
        }
    }
    close(output_pipe[0]);
    int status;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    uint32_t exit_code = WIFEXITED(status) ? (uint32_t)WEXITSTATUS(status) :
                         WIFSIGNALED(status) ? (uint32_t)(128 + WTERMSIG(status)) : 125u;
    uint32_t network_exit = htonl(exit_code);
    if (client_alive) (void)send_frame(client_fd, FRAME_EXIT, &network_exit, sizeof(network_exit));

    free(cwd);
    free_vector(argv, header.argc);
    free_vector(envp, header.envc);
    return (int)exit_code;
}

static int run_broker(const char *path) {
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);
    (void)pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
    umask(0077);
    char lock_path[PATH_MAX];
    if (snprintf(lock_path, sizeof(lock_path), "%s.lock", path) >= (int)sizeof(lock_path)) return 64;
    int lock_fd = open(lock_path, O_CREAT | O_RDWR, 0600);
    if (lock_fd < 0 || flock(lock_fd, LOCK_EX | LOCK_NB) != 0) return 0;
    unlink(path);

    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) return 71;
    struct sockaddr_un address;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    if (snprintf(address.sun_path, sizeof(address.sun_path), "%s", path) >= (int)sizeof(address.sun_path)) return 64;
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        chmod(path, 0600) != 0 || listen(server_fd, 16) != 0) return 71;

    uid_t owner_uid = getuid();
    for (;;) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) { if (errno == EINTR) continue; return 71; }
        pid_t handler = fork();
        if (handler == 0) {
            close(server_fd);
            signal(SIGCHLD, SIG_DFL);
            int result = handle_connection(client_fd, owner_uid);
            close(client_fd);
            _exit(result);
        }
        close(client_fd);
    }
}

static int send_string(int fd, const char *value) {
    size_t length = strlen(value);
    if (length > UINT32_MAX) return -1;
    uint32_t network_length = htonl((uint32_t)length);
    return write_full(fd, &network_length, sizeof(network_length)) == 0 &&
           write_full(fd, value, length) == 0 ? 0 : -1;
}

static int run_client(int argc, char *argv[], const char *path) {
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) return 71;
    uint32_t envc = 0;
    while (environ[envc]) ++envc;

    int self_test = argc == 2 && strcmp(argv[1], "--self-test") == 0;
    char *probe_argv[] = { argv[0], "--probe", NULL };
    char **command_argv = self_test ? probe_argv : &argv[1];
    uint32_t command_argc = self_test ? 2u : (uint32_t)(argc - 1);
    if (command_argc == 0) {
        fprintf(stderr, "usage: codex-performance-exec command [argument ...]\n");
        return 64;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return 71;
    struct sockaddr_un address;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    if (snprintf(address.sun_path, sizeof(address.sun_path), "%s", path) >= (int)sizeof(address.sun_path)) {
        close(fd);
        return 64;
    }
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        close(fd);
        pid_t starter = fork();
        if (starter < 0) return 71;
        if (starter == 0) {
            if (setsid() < 0) _exit(71);
            int null_fd = open("/dev/null", O_RDWR);
            if (null_fd >= 0) {
                (void)dup2(null_fd, STDIN_FILENO);
                (void)dup2(null_fd, STDOUT_FILENO);
                (void)dup2(null_fd, STDERR_FILENO);
                if (null_fd > STDERR_FILENO) close(null_fd);
            }
            char *broker_argv[] = {
                "/usr/sbin/taskpolicy", "-a", executable_path, "--broker", NULL
            };
            execv(broker_argv[0], broker_argv);
            _exit(71);
        }
        fd = -1;
        for (int attempt = 0; attempt < 50; ++attempt) {
            usleep(100000);
            fd = socket(AF_UNIX, SOCK_STREAM, 0);
            if (fd >= 0 && connect(fd, (struct sockaddr *)&address, sizeof(address)) == 0) break;
            if (fd >= 0) close(fd);
            fd = -1;
        }
        if (fd < 0) {
            fprintf(stderr, "codex-performance-exec: performance broker failed to start\n");
            return 69;
        }
    }

    struct request_header header = {
        htonl(REQUEST_MAGIC), htonl(command_argc), htonl(envc), htonl((uint32_t)strlen(cwd))
    };
    if (write_full(fd, &header, sizeof(header)) != 0 || write_full(fd, cwd, strlen(cwd)) != 0) return 74;
    for (uint32_t index = 0; index < command_argc; ++index) if (send_string(fd, command_argv[index]) != 0) return 74;
    for (uint32_t index = 0; index < envc; ++index) if (send_string(fd, environ[index]) != 0) return 74;
    shutdown(fd, SHUT_WR);

    for (;;) {
        struct frame_header network_frame;
        int result = read_full(fd, &network_frame, sizeof(network_frame));
        if (result != 0) return 74;
        uint32_t type = ntohl(network_frame.type);
        uint32_t length = ntohl(network_frame.length);
        if (length > MAX_STRING) return 65;
        unsigned char *payload = malloc(length ? length : 1);
        if (!payload || read_full(fd, payload, length) != 0) { free(payload); return 74; }
        if (type == FRAME_OUTPUT) {
            if (write_full(STDOUT_FILENO, payload, length) != 0) { free(payload); return 74; }
        } else if (type == FRAME_EXIT && length == sizeof(uint32_t)) {
            uint32_t network_exit;
            memcpy(&network_exit, payload, sizeof(network_exit));
            free(payload);
            return (int)ntohl(network_exit);
        } else {
            free(payload);
            return 65;
        }
        free(payload);
    }
}

int main(int argc, char *argv[]) {
    if (!realpath(argv[0], executable_path)) {
        if (snprintf(executable_path, sizeof(executable_path), "%s", argv[0]) >= (int)sizeof(executable_path)) return 64;
    }
    char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    if (socket_path(path, sizeof(path)) != 0) return 64;
    if (argc == 2 && strcmp(argv[1], "--broker") == 0) return run_broker(path);
    if (argc >= 3 && strcmp(argv[1], "--trampoline") == 0) {
        int qos_result = pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
        if (qos_result != 0) {
            fprintf(stderr, "codex-performance-exec: cannot set User Initiated QoS: %s\n", strerror(qos_result));
            return 70;
        }
        execvp(argv[2], &argv[2]);
        fprintf(stderr, "codex-performance-exec: cannot run %s: %s\n", argv[2], strerror(errno));
        return errno == ENOENT ? 127 : 126;
    }
    if (argc == 2 && strcmp(argv[1], "--probe") == 0) {
        errno = 0;
        int role = getpriority(PRIO_DARWIN_ROLE, (id_t)getpid());
        printf("qos=%s role=%d nice=%d pid=%d ppid=%d\n", qos_name(qos_class_self()), role, nice(0), getpid(), getppid());
        return errno == 0 && role == (int)PRIO_DARWIN_ROLE_USER_INIT ? 0 : 1;
    }
    return run_client(argc, argv, path);
}
