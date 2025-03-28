#pragma once

#pragma region DEFINES
#define DELETE_COPY(ClassName)                   \
ClassName(const ClassName&) = delete;            \
ClassName& operator=(const ClassName&) = delete;

#define DELETE_MOVE(ClassName)                   \
ClassName (ClassName&&) = delete;                \
ClassName& operator=(ClassName&&) = delete;

#define DELETE_COPY_MOVE(ClassName)              \
DELETE_COPY(ClassName)                           \
DELETE_MOVE(ClassName)

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

//TODO: This is maybe not the place to specify this
#define MAX_MIP_LEVELS 6
#pragma endregion