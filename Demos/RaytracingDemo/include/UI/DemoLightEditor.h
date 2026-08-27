#pragma once

//Modify Begin:2026-08-26 by Hui
#include <DirectXMath.h>

class SceneLightManager;

class DemoLightEditor final
{
public:
    bool Draw(SceneLightManager& lightManager);

private:
    DirectX::XMFLOAT3 m_NewDirectionalLightDirection = { -0.35f, 0.8f, -0.48f };
    DirectX::XMFLOAT3 m_NewDirectionalLightColor = { 1.0f, 0.95f, 0.82f };
    float m_NewDirectionalLightIntensity = 1.0f;
    float m_NewDirectionalLightAngularRadius = 0.0f;

    DirectX::XMFLOAT3 m_NewPointLightColor = { 1.0f, 0.85f, 0.55f };
    float m_NewPointLightIntensity = 18.0f;
    float m_NewPointLightRange = 24.0f;
    float m_NewPointLightSourceRadius = 0.25f;
    float m_RandomPointLightSpawnRadius = 28.0f;

    DirectX::XMFLOAT3 m_NewSpotLightPosition = { 0.0f, 4.0f, 0.0f };
    DirectX::XMFLOAT3 m_NewSpotLightDirection = { 0.0f, -1.0f, 0.0f };
    DirectX::XMFLOAT3 m_NewSpotLightColor = { 1.0f, 0.85f, 0.55f };
    float m_NewSpotLightIntensity = 18.0f;
    float m_NewSpotLightRange = 24.0f;
    float m_NewSpotLightInnerAngleDegrees = 20.0f;
    float m_NewSpotLightOuterAngleDegrees = 30.0f;

    DirectX::XMFLOAT3 m_NewAreaLightPosition = { 0.0f, 4.0f, 0.0f };
    DirectX::XMFLOAT3 m_NewAreaLightNormal = { 0.0f, -1.0f, 0.0f };
    DirectX::XMFLOAT2 m_NewAreaLightSize = { 2.0f, 2.0f };
    DirectX::XMFLOAT3 m_NewAreaLightColor = { 1.0f, 0.85f, 0.55f };
    float m_NewAreaLightIntensity = 8.0f;
    float m_NewAreaLightRange = 35.0f;
};
//Modify End
