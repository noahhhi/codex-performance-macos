#ifndef CODEX_PERFORMANCE_PROTOCOL_H
#define CODEX_PERFORMANCE_PROTOCOL_H

#include <stdint.h>

#define PERFORMANCE_REGISTER_MAGIC 0x43505232u
#define PERFORMANCE_REGISTER_VERSION 1u
struct performance_register_request {
    uint32_t magic;
    uint32_t version;
    int32_t pid;
    uint32_t reserved;
};

#endif
