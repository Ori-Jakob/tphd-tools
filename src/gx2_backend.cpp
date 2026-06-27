// Compile only the GX2 renderer backend from external/imgui/backends/wiiu.
//
// We deliberately do NOT compile imgui_impl_wiiu.cpp (the platform/input
// backend): it pulls in nn::swkbd and an FSClient, which we don't want to
// initialize inside the running game. Input is handled directly in overlay.cpp.
//
// The include is resolved via -I external/imgui/backends/wiiu (set in the
// Makefile). imgui_impl_gx2.cpp's own `#include "shaders/shader.h"` resolves
// against that same path.
#include "imgui_impl_gx2.cpp"
