#pragma once

//Modify Begin:2026-09-10 by Hui
#include <DirectXMath.h>

struct SkyLightData
{
    DirectX::XMFLOAT4 ColorAndIntensity = { 1.0f, 1.0f, 1.0f, 1.0f };
};

struct DirectionalLightData
{
    DirectX::XMFLOAT4 DirectionAndAngularRadius = { 0.0f, -1.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT4 ColorAndIntensity = { 1.0f, 1.0f, 1.0f, 1.0f };
};

struct PointLightData
{
    DirectX::XMFLOAT4 PositionAndRange = { 0.0f, 0.0f, 0.0f, 1.0f };
    DirectX::XMFLOAT4 ColorAndIntensity = { 1.0f, 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT4 Attenuation = { 1.0f, 0.0f, 0.0f, 0.0f };
};

struct SpotLightData
{
    DirectX::XMFLOAT4 PositionAndRange = { 0.0f, 0.0f, 0.0f, 1.0f };
    DirectX::XMFLOAT4 DirectionAndCosOuter = { 0.0f, 0.0f, 1.0f, 0.0f };
    DirectX::XMFLOAT4 ColorAndIntensity = { 1.0f, 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT4 AttenuationAndCosInner = { 1.0f, 0.0f, 0.0f, 1.0f };
};

struct AreaLightData
{
    // xyz: rectangle center, w: hard maximum illumination distance in world units.
    DirectX::XMFLOAT4 PositionAndRange = { 0.0f, 0.0f, 0.0f, 1.0f };
    // xyz: normalized one-sided emission direction, w: reserved.
    DirectX::XMFLOAT4 NormalAndType = { 0.0f, -1.0f, 0.0f, 0.0f };
    // xyz: normalized width axis, w: half width in world units.
    DirectX::XMFLOAT4 AxisUAndExtent = { 1.0f, 0.0f, 0.0f, 0.5f };
    // xyz: normalized height axis, w: half height in world units.
    DirectX::XMFLOAT4 AxisVAndExtent = { 0.0f, 0.0f, 1.0f, 0.5f };
    // rgb: linear radiance tint, w: radiance multiplier.
    DirectX::XMFLOAT4 ColorAndIntensity = { 1.0f, 1.0f, 1.0f, 1.0f };
};
//Modify End
