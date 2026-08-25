//Modify Begin:2026-08-25 by Hui
#pragma once

#include <Framework/Rendering/Denoising/NRD.h>
#include <Framework/Rendering/Denoising/OIDN.h>
#include <Framework/Rendering/Denoising/SVGF.h>

#include <DirectXMath.h>

#include <cstdint>
#include <memory>

class FrameworkDeviceContext;
class CommandList;
class CommandQueue;
class Texture;

class DenoiserController final
{
public:
    enum class Algorithm : uint32_t
    {
        Off = 0,
        NRD = 1,
        SVGF = 2,
        OIDN = 3,
    };

    void Initialize(FrameworkDeviceContext& deviceContext);
    void Shutdown();

    void SetAlgorithm(Algorithm algorithm);
    void SetAlgorithmFromName(const char* algorithmName);
    Algorithm GetAlgorithm() const { return m_Algorithm; }

    bool IsEnabled() const { return m_Algorithm != Algorithm::Off; }
    bool IsNRDEnabled() const { return m_Algorithm == Algorithm::NRD; }
    bool IsSVGFEnabled() const { return m_Algorithm == Algorithm::SVGF; }
    bool IsOIDNEnabled() const { return m_Algorithm == Algorithm::OIDN; }
    bool RequiresAccumulation() const { return IsOIDNEnabled(); }
    bool UsesRenderGraphPasses() const { return IsNRDEnabled() || IsSVGFEnabled() || IsOIDNEnabled(); }
    void SetOIDNStaticSpp(uint32_t spp);
    uint32_t GetOIDNStaticSpp() const { return m_OIDNStaticSpp; }
    NRD::DenoiserMode GetNRDMode() const;
    uint32_t GetSVGFAtrousIterations() const;
    void ResetHistory();
    void ResetOIDNHistory();
    void OnResourcesRecreated(uint32_t width, uint32_t height);
    bool BeginOIDNReadback(bool accumulationEnabled, uint32_t accumulationFrameIndex);
    bool RecordOIDNReadback(CommandList& commandList, const std::shared_ptr<Texture>& source);
    void EndOIDNReadback(uint64_t submittedFenceValue);
    void CancelOIDNReadback();
    void PollOIDN(CommandQueue& directQueue);
    bool HasOIDNResult() const { return m_OIDN != nullptr && m_OIDN->HasUploadedResult(); }
    uint64_t GetOIDNGeneration() const { return m_OIDN != nullptr ? m_OIDN->GetGeneration() : 0u; }
    void FillCameraConstants(uint32_t& nrdDenoiserMode, DirectX::XMFLOAT4& nrdReblurHitDistanceParameters) const;
    bool DrawImGui();

    void AddNRDPasses(RenderGraph::RenderGraphBuilder& builder, NRD::GraphInputs inputs);
    void AddSVGFPasses(RenderGraph::RenderGraphBuilder& builder, SVGF::GraphInputs inputs);
    void AddOIDNPasses(RenderGraph::RenderGraphBuilder& builder, OIDNDenoiser::GraphInputs inputs);

private:
    void ApplySelection();

    std::unique_ptr<NRD> m_NRD;
    std::unique_ptr<OIDNDenoiser> m_OIDN;
    std::unique_ptr<SVGF> m_SVGF;
    Algorithm m_Algorithm = Algorithm::NRD;
    uint32_t m_OIDNStaticSpp = 16u;
};
//Modify End
