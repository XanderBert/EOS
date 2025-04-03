#pragma once

#define DELETE_COPY(ClassName)                   \
ClassName(const ClassName&) = delete;            \
ClassName& operator=(const ClassName&) = delete;

#define DELETE_MOVE(ClassName)                   \
ClassName (ClassName&&) = delete;                \
ClassName& operator=(ClassName&&) = delete;

#define DELETE_COPY_MOVE(ClassName)              \
DELETE_COPY(ClassName)                           \
DELETE_MOVE(ClassName)

#define PTR_SIZE (INTPTR_WIDTH / 8)
#define ARRAY_COUNT(array) (sizeof(array) / (sizeof(array[0]) * (sizeof(array) != PTR_SIZE || sizeof(array[0]) <= PTR_SIZE)))