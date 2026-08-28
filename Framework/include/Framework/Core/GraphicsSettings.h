#pragma once
#include <cstdint>

struct GraphicsSettings
{
	struct ShadowsSettings
	{
		uint32_t m_Resolution;
		float m_DepthBias;
		float m_NormalBias;
		float m_PoissonSpread;
	};

	bool VSync = false;
//Modify Begin:2026-08-28 by Hui
	float ProfilerDisplayRefreshIntervalSeconds = 1.0f;
    bool Hdr10Output = false;
//Modify End

	ShadowsSettings DirectionalLightShadows{ 2048, 1.0f, 0.002f, 750.0f };
	// resolution is only a single cubemap side
	ShadowsSettings PointLightShadows{ 256, 0.5f, 0.1f, 250.0f };

	ShadowsSettings SpotLightShadows{ 256, 1.0f, 1.0f, 250.0f };

	uint32_t DynamicReflectionsResolution = 128;
};
