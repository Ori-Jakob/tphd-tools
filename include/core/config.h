#pragma once

#include <stddef.h>
#include <stdint.h>

struct cJSON;

namespace Ui { struct WindowState; }

namespace Config {
void Poll();
void MarkDirty();
void Flush();

bool IsLoaded();
const char* LastError();

void OnApplicationStart();

bool ReadBool(const cJSON* root, const char* key, bool* out);
bool ReadFloat(const cJSON* root, const char* key, float* out, float lo, float hi);
bool ReadInt(const cJSON* root, const char* key, int* out, int lo, int hi);
bool ReadU32(const cJSON* root, const char* key, uint32_t* out);
bool ReadString(const cJSON* root, const char* key, char* out, size_t size);

void AddBool(cJSON* root, const char* key, bool value);
void AddNumber(cJSON* root, const char* key, double value);
void AddString(cJSON* root, const char* key, const char* value);

void LoadWindow(const cJSON* root, const char* prefix, Ui::WindowState& state,
                float minWidth, float maxWidth);
void SaveWindow(cJSON* root, const char* prefix, const Ui::WindowState& state);
}
