// Compile only the GX2 renderer backend from external/imgui/backends/wiiu.
//
// We deliberately do NOT compile imgui_impl_wiiu.cpp (the platform/input
// backend): it pulls in nn::swkbd and an FSClient, which we don't want to
// initialize inside the running game. Input is handled directly in overlay.cpp.
//
// The include is resolved via -I external/imgui/backends/wiiu (set in the
// Makefile). imgui_impl_gx2.cpp's own `#include "shaders/shader.h"` resolves
// against that same path.
#ifdef __WUPS__
// Aroma plugins use a private 0x8xxxxxxx heap. GX2 resources cannot be handed
// directly to the GPU from there, so adapt the stock backend's allocation and
// GX2R calls to MemoryMappingModule-backed memory. Keeping the substitutions in
// this unity shim leaves the Cemu backend byte-for-byte on its normal path.
#include <coreinit/debug.h>
#include <gx2/mem.h>
#include <gx2/utils.h>
#include <gx2r/surface.h>
#include <malloc.h>
#include <memory/mappedmemory.h>
#include <stddef.h>

#include "aroma/gfx_shader_mappedmem.h"

static void* TphdMappedMemalign(size_t alignment, size_t size)
{
    void* ptr = MEMAllocFromMappedMemoryForGX2Ex((uint32_t)size,
                                                 (int32_t)alignment);
    if (!ptr)
        OSReport("[tphd_tools][gfx] mapped GX2 alloc failed "
                 "(%u bytes, align %u)\n",
                 (unsigned)size, (unsigned)alignment);
    return ptr;
}

static void TphdMappedFree(void* ptr)
{
    if (ptr)
        MEMFreeToMappedMemory(ptr);
}

static BOOL TphdMappedCreateSurface(GX2Surface* surface,
                                    GX2RResourceFlags flags)
{
    GX2CalcSurfaceSizeAndAlignment(surface);
    surface->image = MEMAllocFromMappedMemoryForGX2Ex(
        surface->imageSize, (int32_t)surface->alignment);
    if (!surface->image) {
        OSReport("[tphd_tools][gfx] font surface alloc failed "
                 "(%ux%u, %u bytes)\n",
                 (unsigned)surface->width, (unsigned)surface->height,
                 (unsigned)surface->imageSize);
        return FALSE;
    }

    // This is user/mapped memory, not an allocation owned by GX2R.
    surface->resourceFlags = flags;
    return TRUE;
}

static void* TphdMappedLockSurface(GX2Surface* surface,
                                   int32_t level,
                                   GX2RResourceFlags flags)
{
    (void)flags;
    return level == 0 ? surface->image : NULL;
}

static void TphdMappedUnlockSurface(GX2Surface* surface,
                                    int32_t level,
                                    GX2RResourceFlags flags)
{
    (void)flags;
    if (level == 0 && surface->image)
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE,
                      surface->image, surface->imageSize);
}

static BOOL TphdMappedDestroySurface(GX2Surface* surface,
                                     GX2RResourceFlags flags)
{
    (void)flags;
    if (surface->image) {
        MEMFreeToMappedMemory(surface->image);
        surface->image = NULL;
    }
    if (surface->mipmaps) {
        MEMFreeToMappedMemory(surface->mipmaps);
        surface->mipmaps = NULL;
    }
    return TRUE;
}

#define IMGUI_IMPL_GX2_ALLOC(size, alignment) \
    TphdMappedMemalign((alignment), (size))
#define IMGUI_IMPL_GX2_FREE(ptr) \
    TphdMappedFree(ptr)
#define IMGUI_IMPL_GX2_CREATE_SURFACE(surface, flags) \
    TphdMappedCreateSurface((surface), (flags))
#define IMGUI_IMPL_GX2_LOCK_SURFACE(surface, level, flags) \
    TphdMappedLockSurface((surface), (level), (flags))
#define IMGUI_IMPL_GX2_UNLOCK_SURFACE(surface, level, flags) \
    TphdMappedUnlockSurface((surface), (level), (flags))
#define IMGUI_IMPL_GX2_DESTROY_SURFACE(surface, flags) \
    TphdMappedDestroySurface((surface), (flags))
#define IMGUI_IMPL_GX2_LOAD_SHADER_GROUP(group, index, file) \
    TphdGfxLoadGFDShaderGroupMapped((group), (index), (file))
#define IMGUI_IMPL_GX2_INIT_FETCH_SHADER(group) \
    TphdGfxInitFetchShaderMapped(group)
#define IMGUI_IMPL_GX2_FREE_SHADER_GROUP(group) \
    TphdGfxFreeShaderGroupMapped(group)
#endif

#include "imgui_impl_gx2.cpp"

#ifdef __WUPS__
#undef IMGUI_IMPL_GX2_ALLOC
#undef IMGUI_IMPL_GX2_FREE
#undef IMGUI_IMPL_GX2_CREATE_SURFACE
#undef IMGUI_IMPL_GX2_LOCK_SURFACE
#undef IMGUI_IMPL_GX2_UNLOCK_SURFACE
#undef IMGUI_IMPL_GX2_DESTROY_SURFACE
#undef IMGUI_IMPL_GX2_LOAD_SHADER_GROUP
#undef IMGUI_IMPL_GX2_INIT_FETCH_SHADER
#undef IMGUI_IMPL_GX2_FREE_SHADER_GROUP
#endif
