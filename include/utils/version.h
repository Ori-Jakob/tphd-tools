// version.h -- single source of truth for the TPHD Tools version + author.
//
// Used by the load toast (menu.cpp), the boot log banner (logger.cpp), and the
// Aroma plugin metadata (aroma/plugin.cpp). Bump TPHD_TOOLS_VERSION on release.
#pragma once

#ifdef TPHD_TOOLS_DEBUG
#define TPHD_TOOLS_BUILD_MODE "DEBUG"
#else
#define TPHD_TOOLS_BUILD_MODE "RELEASE"
#endif

#ifndef TPHD_TOOLS_VERSION_BASE
#define TPHD_TOOLS_VERSION_BASE "CRC32-UNKNOWN"
#endif

#ifdef TPHD_TOOLS_EXPERIMENTAL
#define TPHD_TOOLS_BUILD_CHANNEL "EXPERIMENTAL "
#else
#define TPHD_TOOLS_BUILD_CHANNEL ""
#endif

#define TPHD_TOOLS_VERSION TPHD_TOOLS_VERSION_BASE " " TPHD_TOOLS_BUILD_CHANNEL TPHD_TOOLS_BUILD_MODE
#define TPHD_TOOLS_AUTHOR  "n0ted"
