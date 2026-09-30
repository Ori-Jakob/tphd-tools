#pragma once

#if __has_include("tphd_version.h")
#include "tphd_version.h"
#endif

#ifndef TPHD_TOOLS_VERSION_BASE
#define TPHD_TOOLS_VERSION_BASE "dev"
#endif

#ifdef TPHD_TOOLS_EXPERIMENTAL
#define TPHD_TOOLS_FLAVOUR "-experimental"
#else
#define TPHD_TOOLS_FLAVOUR ""
#endif

#ifdef TPHD_TOOLS_DEBUG
#define TPHD_TOOLS_VERSION TPHD_TOOLS_VERSION_BASE TPHD_TOOLS_FLAVOUR "-debug"
#else
#define TPHD_TOOLS_VERSION TPHD_TOOLS_VERSION_BASE TPHD_TOOLS_FLAVOUR
#endif

#define TPHD_TOOLS_AUTHOR "n0ted"
