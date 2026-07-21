// GX2-visible shader allocation for the Aroma build.
//
// Derived from the MIT-licensed mapped-memory ImGui backend in
// GaryOderNichts/imgui_overlay_plugin. WUPS plugins live in their own mapped
// heaps, so shader headers can use ordinary mapped memory while shader programs
// must use the GX2 mapping.

#include "gfx_shader_mappedmem.h"

#include <coreinit/debug.h>     // OSReport: these failures are otherwise silent
#include <gfd.h>
#include <gx2/mem.h>
#include <gx2/shaders.h>
#include <gx2/utils.h>
#include <memory/mappedmemory.h>
#include <string.h>

static GX2PixelShader* loadPixelShader(uint32_t index, const void* file)
{
    GX2PixelShader* shader = NULL;
    void* program = NULL;
    const char* fail = NULL;

    if (index >= GFDGetPixelShaderCount(file)) {
        fail = "GFD index/count";
        goto error;
    }

    const uint32_t headerSize = GFDGetPixelShaderHeaderSize(index, file);
    const uint32_t programSize = GFDGetPixelShaderProgramSize(index, file);
    if (!headerSize || !programSize) {
        fail = "GFD header/program size";
        goto error;
    }

    shader = (GX2PixelShader*)MEMAllocFromMappedMemoryEx(headerSize, 64);
    program = MEMAllocFromMappedMemoryForGX2Ex(
        programSize, GX2_SHADER_PROGRAM_ALIGNMENT);
    if (!shader || !program) {
        fail = "mapped memory alloc";
        goto error;
    }

    if (!GFDGetPixelShader(shader, program, index, file)) {
        fail = "GFDGetPixelShader";
        goto error;
    }

    shader->program = program;
    shader->size = programSize;
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER,
                  shader->program, shader->size);
    return shader;

error:
    OSReport("[tphd_tools][gfx] pixel shader load failed: %s\n", fail);
    if (program)
        MEMFreeToMappedMemory(program);
    if (shader)
        MEMFreeToMappedMemory(shader);
    return NULL;
}

static GX2VertexShader* loadVertexShader(uint32_t index, const void* file)
{
    GX2VertexShader* shader = NULL;
    void* program = NULL;
    const char* fail = NULL;

    if (index >= GFDGetVertexShaderCount(file)) {
        fail = "GFD index/count";
        goto error;
    }

    const uint32_t headerSize = GFDGetVertexShaderHeaderSize(index, file);
    const uint32_t programSize = GFDGetVertexShaderProgramSize(index, file);
    if (!headerSize || !programSize) {
        fail = "GFD header/program size";
        goto error;
    }

    shader = (GX2VertexShader*)MEMAllocFromMappedMemoryEx(headerSize, 64);
    program = MEMAllocFromMappedMemoryForGX2Ex(
        programSize, GX2_SHADER_PROGRAM_ALIGNMENT);
    if (!shader || !program) {
        fail = "mapped memory alloc";
        goto error;
    }

    if (!GFDGetVertexShader(shader, program, index, file)) {
        fail = "GFDGetVertexShader";
        goto error;
    }

    shader->program = program;
    shader->size = programSize;
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER,
                  shader->program, shader->size);
    return shader;

error:
    OSReport("[tphd_tools][gfx] vertex shader load failed: %s\n", fail);
    if (program)
        MEMFreeToMappedMemory(program);
    if (shader)
        MEMFreeToMappedMemory(shader);
    return NULL;
}

static void freePixelShader(GX2PixelShader* shader)
{
    if (!shader)
        return;
    if (shader->program)
        MEMFreeToMappedMemory(shader->program);
    MEMFreeToMappedMemory(shader);
}

static void freeVertexShader(GX2VertexShader* shader)
{
    if (!shader)
        return;
    if (shader->program)
        MEMFreeToMappedMemory(shader->program);
    MEMFreeToMappedMemory(shader);
}

BOOL TphdGfxLoadGFDShaderGroupMapped(WHBGfxShaderGroup* group,
                                     uint32_t index,
                                     const void* file)
{
    memset(group, 0, sizeof(*group));
    group->vertexShader = loadVertexShader(index, file);
    group->pixelShader = loadPixelShader(index, file);
    if (!group->vertexShader || !group->pixelShader) {
        TphdGfxFreeShaderGroupMapped(group);
        return FALSE;
    }
    return TRUE;
}

BOOL TphdGfxInitFetchShaderMapped(WHBGfxShaderGroup* group)
{
    const uint32_t size = GX2CalcFetchShaderSizeEx(
        group->numAttributes,
        GX2_FETCH_SHADER_TESSELLATION_NONE,
        GX2_TESSELLATION_MODE_DISCRETE);

    group->fetchShaderProgram = MEMAllocFromMappedMemoryForGX2Ex(
        size, GX2_SHADER_PROGRAM_ALIGNMENT);
    if (!group->fetchShaderProgram) {
        OSReport("[tphd_tools][gfx] fetch shader alloc failed (%u bytes)\n",
                 size);
        return FALSE;
    }

    GX2InitFetchShaderEx(&group->fetchShader,
                         group->fetchShaderProgram,
                         group->numAttributes,
                         group->attributes,
                         GX2_FETCH_SHADER_TESSELLATION_NONE,
                         GX2_TESSELLATION_MODE_DISCRETE);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER,
                  group->fetchShaderProgram, size);
    return TRUE;
}

BOOL TphdGfxFreeShaderGroupMapped(WHBGfxShaderGroup* group)
{
    if (!group)
        return TRUE;

    if (group->fetchShaderProgram) {
        MEMFreeToMappedMemory(group->fetchShaderProgram);
        group->fetchShaderProgram = NULL;
    }
    freePixelShader(group->pixelShader);
    group->pixelShader = NULL;
    freeVertexShader(group->vertexShader);
    group->vertexShader = NULL;
    return TRUE;
}
