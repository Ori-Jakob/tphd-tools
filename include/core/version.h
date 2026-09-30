#pragma once

#if __has_include("tphd_version.h")
#include "tphd_version.h"
#endif

#ifndef TPHD_TOOLS_VERSION_BASE
#define TPHD_TOOLS_VERSION_BASE "dev"
#endif

#ifdef TPHD_TOOLS_DEBUG
#define TPHD_TOOLS_VERSION TPHD_TOOLS_VERSION_BASE "-debug"
#else
#define TPHD_TOOLS_VERSION TPHD_TOOLS_VERSION_BASE
#endif

#define TPHD_TOOLS_AUTHOR "n0ted"
