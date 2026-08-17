#pragma once

#include <DirectXMath.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

//Modify Begin:2026-08-06 by Hui
inline constexpr uint32_t SurfaceEmitterInvalidMaterialIndex = (std::numeric_limits<uint32_t>::max)();
inline constexpr uint32_t SurfaceEmitterInstanceFlagUseMaterialEmission = 1u << 0u;

struct SurfaceEmitterGeometryData
{
    uint32_t TriangleOffset = 0;
    uint32_t TriangleCount = 0;
    uint32_t TriangleCdfOffset = 0;
    uint32_t Reserved = 0;
};

struct SurfaceEmitterTriangleData
{
    DirectX::XMFLOAT4 Position0 = { 0.0f, 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT4 Position1 = { 0.0f, 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT4 Position2 = { 0.0f, 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT4 Uv0Uv1 = { 0.0f, 0.0f, 1.0f, 0.0f };
    DirectX::XMFLOAT4 Uv2AndPadding = { 0.0f, 1.0f, 0.0f, 0.0f };
};

struct SurfaceEmitterInstanceData
{
    DirectX::XMFLOAT4 OriginAndRange = { 0.0f, 0.0f, 0.0f, 10000.0f };
    DirectX::XMFLOAT4 AxisX = { 1.0f, 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT4 AxisY = { 0.0f, 1.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT4 AxisZ = { 0.0f, 0.0f, 1.0f, 0.0f };
    DirectX::XMFLOAT4 EmissionAndIntensity = { 0.0f, 0.0f, 0.0f, 1.0f };
    uint32_t GeometryIndex = 0;
    uint32_t MaterialIndex = SurfaceEmitterInvalidMaterialIndex;
    uint32_t Flags = 0;
    float SurfaceArea = 0.0f;
};

struct SurfaceEmitterSceneData
{
    std::vector<SurfaceEmitterGeometryData> Geometries;
    std::vector<SurfaceEmitterTriangleData> Triangles;
    std::vector<float> TriangleCdf;
    std::vector<SurfaceEmitterInstanceData> Instances;
};

static_assert(sizeof(SurfaceEmitterGeometryData) == 16);
static_assert(sizeof(SurfaceEmitterTriangleData) == 80);
static_assert(sizeof(SurfaceEmitterInstanceData) == 96);
//Modify End
