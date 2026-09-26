#pragma once
#include "UI/UI.h"

namespace EOS::UI::Internal
{

    void ApplyTheme(Theme theme);
    void ReapplyCurrentTheme();
    extern Theme CurrentTheme;
}
