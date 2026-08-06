#pragma once

#include <DirectXMath.h>
#include <cstdint>

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

struct AreaLightData
{
    DirectX::XMFLOAT4 PositionAndRange = { 0.0f, 0.0f, 0.0f, 1.0f };
    DirectX::XMFLOAT4 NormalAndType = { 0.0f, -1.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT4 AxisUAndExtent = { 1.0f, 0.0f, 0.0f, 0.5f };
    DirectX::XMFLOAT4 AxisVAndExtent = { 0.0f, 0.0f, 1.0f, 0.5f };
    DirectX::XMFLOAT4 ColorAndIntensity = { 1.0f, 1.0f, 1.0f, 1.0f };
};
