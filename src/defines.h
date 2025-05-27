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

#ifndef INTPTR_WIDTH
#if defined __x86_64__ && !defined __ILP32__
# define INTPTR_WIDTH	64
#else
# define INTPTR_WIDTH	32
#endif
#endif

#define PTR_SIZE (INTPTR_WIDTH / 8)
#define ARRAY_COUNT(array) (sizeof(array) / (sizeof(array[0]) * (sizeof(array) != PTR_SIZE || sizeof(array[0]) <= PTR_SIZE)))

#define EOS_MAX_MIP_LEVELS 16
#define EOS_MAX_COLOR_ATTACHMENTS 8