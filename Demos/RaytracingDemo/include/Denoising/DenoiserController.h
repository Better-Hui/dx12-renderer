//Modify Begin:2026-07-27 by BestHui
#pragma once

#include <Framework/Rendering/Denoising/NRD.h>
#include <Framework/Rendering/Denoising/SVGF.h>
#include <RenderGraph/RaytracingDemoGraphResources.h>

#include <DirectXMath.h>

#include <cstdint>
#include <memory>

class CommandList;
//Modify Begin:2026-07-30 by BestHui
class FrameworkDeviceContext;
//Modify End

class DenoiserController final
{
public:
    enum class Algorithm : uint32_t
    {
        Off = 0,
        NRD = 1,
        SVGF = 2,
    };

    void Initialize(FrameworkDeviceContext& deviceContext);
    void Shutdown();

    void SetAlgorithm(Algorithm algorithm);
    void SetAlgorithmFromName(const char* algorithmName);
    Algorithm GetAlgorithm() const { return m_Algorithm; }

    bool IsEnabled() const { return m_Algorithm != Algorithm::Off; }
    bool IsNRDEnabled() const { return m_Algorithm == Algorithm::NRD; }
    bool IsSVGFEnabled() const { return m_Algorithm == Algorithm::SVGF; }
    void ResetHistory();
    void FillCameraConstants(uint32_t& nrdDenoiserMode, DirectX::XMFLOAT4& nrdReblurHitDistanceParameters) const;
    bool DrawImGui();

    void Execute(
        CommandList& commandList,
        const NRD::FrameMatrices& frameMatrices,
        const RaytracingDemoRenderGraph::FrameGBufferResources& gbuffer,
        const RaytracingDemoRenderGraph::LightingResources& lighting,
        const RaytracingDemoRenderGraph::NRDResources& nrdResources,
        uint32_t width,
        uint32_t height);

private:
    void ApplySelection();

    std::unique_ptr<NRD> m_NRD;
    std::unique_ptr<SVGF> m_SVGF;
    Algorithm m_Algorithm = Algorithm::NRD;
};
//Modify End
