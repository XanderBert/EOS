#pragma once
#include <csignal>

#define DELETE_COPY(ClassName)                   \
ClassName(const ClassName&) = delete;            \
ClassName& operator=(const ClassName&) = delete;

#define DELETE_MOVE(ClassName)                   \
ClassName (ClassName&&) = delete;                \
ClassName& operator=(ClassName&&) = delete;

#define DELETE_COPY_MOVE(ClassName)              \
DELETE_COPY(ClassName)                           \
DELETE_MOVE(ClassName)


#define PTR_SIZE (UINTPTR_MAX == 0xFFFFFFFF ? 4 : 8)
#define ARRAY_COUNT(array) (sizeof(array) / (sizeof(array[0]) * (sizeof(array) != PTR_SIZE || sizeof(array[0]) <= PTR_SIZE)))

#define EOS_MAX_MIP_LEVELS 16
#define EOS_MAX_COLOR_ATTACHMENTS 8

#define EOS_SHADER_CHECKSUM 0x53505256 // "SPRV"

#ifdef _WIN32
#include <intrin.h>
#define DEBUG_BREAK() __debugbreak()
#elif defined(__APPLE__) || defined(__MACH__)
#include <TargetConditionals.h>
#include <unistd.h>
#include <signal.h>
#define DEBUG_BREAK() raise(SIGTRAP)
#else // Linux / Unix
#include <signal.h>
#define DEBUG_BREAK() raise(SIGTRAP)
#endif