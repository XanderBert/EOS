#pragma once
#include <volk.h>


#define VK_ASSERT(func)                                             \
{                                                                   \
    const VkResult result = func;                                   \
    if (result != VK_SUCCESS)                                       \
    {                                                               \
        fprint("Vulkan Assert failed: %s:%i\n  %s\n  %s\n",         \
        __FILE__,                                                   \
        __LINE__,                                                   \
        #func,                                                      \
        #result);                                                   \
        assert(false);                                              \
    }                                                               \
}