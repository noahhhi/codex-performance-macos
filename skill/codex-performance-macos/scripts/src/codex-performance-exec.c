#include <errno.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <pthread.h>
#include <pthread/qos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

static qos_class_t current_qos_class(void) {
    qos_class_t qos = QOS_CLASS_UNSPECIFIED;
    int relative_priority = 0;
    (void)pthread_get_qos_class_np(pthread_self(), &qos, &relative_priority);
    return qos;
}

static int request_user_initiated(void) {
    int result = pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
    if (result != 0) {
        fprintf(stderr, "codex-performance-exec: cannot request User Initiated QoS: %s\n",
                strerror(result));
    }
    return result;
}

static int run_direct(char *const command_argv[]) {
    if (!command_argv[0]) return 64;
    if (request_user_initiated() != 0) return 70;
    execvp(command_argv[0], command_argv);
    fprintf(stderr, "codex-performance-exec: cannot run %s: %s\n",
            command_argv[0], strerror(errno));
    return errno == ENOENT ? 127 : 126;
}

static int resolve_executable_path(char executable_path[PATH_MAX]) {
    char unresolved_path[PATH_MAX];
    uint32_t path_size = sizeof(unresolved_path);
    if (_NSGetExecutablePath(unresolved_path, &path_size) != 0) {
        fprintf(stderr, "codex-performance-exec: launcher path is too long\n");
        return -1;
    }
    if (!realpath(unresolved_path, executable_path)) {
        fprintf(stderr, "codex-performance-exec: cannot resolve launcher path: %s\n",
                strerror(errno));
        return -1;
    }
    return 0;
}

static int run_with_application_policy(int argc, char *argv[]) {
    char executable_path[PATH_MAX];
    if (resolve_executable_path(executable_path) != 0) return 70;

    size_t argument_count = (size_t)argc + 4;
    char **policy_argv = calloc(argument_count, sizeof(*policy_argv));
    if (!policy_argv) return 71;
    policy_argv[0] = "/usr/sbin/taskpolicy";
    policy_argv[1] = "-a";
    policy_argv[2] = executable_path;
    policy_argv[3] = "--direct";
    for (int index = 1; index < argc; ++index) {
        policy_argv[(size_t)index + 3] = argv[index];
    }

    execv(policy_argv[0], policy_argv);
    int saved_errno = errno;
    fprintf(stderr, "codex-performance-exec: cannot start taskpolicy: %s\n",
            strerror(saved_errno));
    free(policy_argv);
    return saved_errno == ENOENT ? 127 : 126;
}

static int self_test(void) {
    if (request_user_initiated() != 0) return 1;
    qos_class_t qos = current_qos_class();
    printf("launcher=v2 qos=%s privileged=no persistent=no\n", qos_name(qos));
    return qos == QOS_CLASS_USER_INITIATED || qos == QOS_CLASS_USER_INTERACTIVE ? 0 : 1;
}

static void usage(void) {
    fprintf(stderr, "usage: codex-performance-exec command [arguments ...]\n");
}

int main(int argc, char *argv[]) {
    if (argc == 2 && strcmp(argv[1], "--self-test") == 0) return self_test();
    if (argc == 2 && strcmp(argv[1], "--probe") == 0) {
        printf("qos=%s pid=%d ppid=%d\n", qos_name(current_qos_class()), getpid(), getppid());
        return 0;
    }
    if (argc >= 3 && strcmp(argv[1], "--direct") == 0) {
        return run_direct(&argv[2]);
    }
    if (argc < 2) {
        usage();
        return 64;
    }
    return run_with_application_policy(argc, argv);
}
