// renderer.h -- ImGui + native GX2 setup/draw for the overlay.
#pragma once

struct GX2ColorBuffer;

namespace Renderer {

bool IsReady();

// One-time: create the ImGui context + GX2 backend. `display` supplies the
// logical UI size; TPHD Tools uses the TV buffer for this.
void Init(GX2ColorBuffer* display);

// Per frame, before building UI: set the logical display size and start a frame.
void NewFrame(GX2ColorBuffer* display, float deltaTime);

// Finalize the current ImGui frame once, then draw it to one or more buffers.
// Draw scales the logical UI to the target buffer's dimensions.
void FinishFrame();
void Draw(GX2ColorBuffer* target);

} // namespace Renderer
