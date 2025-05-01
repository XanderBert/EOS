#pragma once
//This file is a include wrapper to resolve naming conflicts with X11
//X11 defines None and Bool which are also used as enum values in Slang

#if defined(EOS_PLATFORM_X11)

#pragma push_macro("None")
#undef None

#pragma push_macro("Bool")
#undef Bool

#include <slang.h>
#include <slang-com-ptr.h>
#include <slang-com-helper.h>

#pragma pop_macro("Bool")
#pragma pop_macro("None")

#else

#include <slang.h>
#include <slang-com-ptr.h>
#include <slang-com-helper.h>

#endif