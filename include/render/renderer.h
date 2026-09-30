#pragma once

struct GX2ColorBuffer;
struct ImFont;

namespace Renderer {
bool IsReady();
void Init(float logicalWidth, float logicalHeight);
void NewFrame(float logicalWidth, float logicalHeight, float deltaTime);
void FinishFrame();

void SetUiScale(float scale);
float UiScale();

void DrawPrepared(GX2ColorBuffer* target, bool gameScreen, bool withHud);
void DrawPreparedTopLayer(GX2ColorBuffer* target);
void DrawGamePadOnlyLayer(GX2ColorBuffer* target);

void SetGamePadOnlyList(const void* drawList);

void MarkGameScreenOnly(const void* drawList);
void MarkToastList(const void* drawList);

bool HasTopLayerContent();
bool HasGamePadOnlyContent();
bool HasWorldContent();
bool HasGameScreenListContent();
bool HasToastContent();
void DrawGameLayers(GX2ColorBuffer* target, bool withHud);

ImFont* Font();
ImFont* BoldFont();
ImFont* ShadowFont();

void ResetDeviceObjects();
float BackdropAlpha(float displayAlpha);
}
