#include "ui/panels.h"

#include "ui/quick_access.h"

#include "imgui.h"

namespace Ui {
namespace Panels {
void RegisterControls()
{
    RegisterGameplayControls();
}

void DrawPractice()
{
    QuickAccess::DrawMenuItem();
}
}
}
