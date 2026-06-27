// config.h -- persist TPHD Tools settings.
//
// Both front ends use the SD path tphd_tools/config.json.
// JSON is produced/parsed with cJSON.
#pragma once

namespace Config {

// Read the config file and apply it to the live settings. Safe to call once FS
// is up (in-game); no-op + defaults if the file is missing or unreadable.
void Load();

// Per frame: if any persisted setting changed since the last write, save it.
void Update();

} // namespace Config
