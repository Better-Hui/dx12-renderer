//Modify Begin:2026-08-25 by Hui
#include <Denoising/DenoiserController.h>

#include <DX12Library/Helpers.h>
#include <imgui.h>
#include <Framework/UI/NumericWidgets.h>

#include <algorithm>
#include <cstring>
#include <utility>

void DenoiserController::Initialize(FrameworkDeviceContext& deviceContext)
{
    m_NRD = std::make_unique<NRD>(deviceContext);
    m_OIDN = std::make_unique<OIDNDenoiser>(deviceContext);
    m_SVGF = std::make_unique<SVGF>(deviceContext);
    ApplySelection();
}

void DenoiserController::Shutdown()
{
    m_SVGF.reset();
    m_OIDN.reset();
    m_NRD.reset();
}

void DenoiserController::SetAlgorithm(const Algorithm algorithm)
{
    m_Algorithm = algorithm;
    ApplySelection();
}

void DenoiserController::SetAlgorithmFromName(const char* algorithmName)
{
    if (algorithmName == nullptr)
    {
        return;
    }

    if (std::strcmp(algorithmName, "nrd") == 0)
    {
        SetAlgorithm(Algorithm::NRD);
        return;
    }

    if (std::strcmp(algorithmName, "svgf") == 0)
    {
        SetAlgorithm(Algorithm::SVGF);
        return;
    }

    if (std::strcmp(algorithmName, "oidn") == 0)
    {
        SetAlgorithm(Algorithm::OIDN);
        return;
    }

    SetAlgorithm(Algorithm::Off);
}

void DenoiserController::ResetHistory()
{
    if (m_NRD != nullptr)
    {
        m_NRD->ResetHistory();
    }

    if (m_SVGF != nullptr)
    {
        m_SVGF->ResetHistory();
    }

    if (m_OIDN != nullptr)
    {
        m_OIDN->ResetHistory();
    }
}

void DenoiserController::ResetOIDNHistory()
{
    if (m_OIDN != nullptr)
    {
        m_OIDN->ResetHistory();
    }
}

void DenoiserController::SetOIDNStaticSpp(const uint32_t spp)
{
    const uint32_t clampedSpp = std::clamp(spp, 1u, 4096u);
    if (m_OIDNStaticSpp == clampedSpp)
    {
        return;
    }
    m_OIDNStaticSpp = clampedSpp;
    if (m_OIDN != nullptr)
    {
        m_OIDN->ResetHistory();
    }
}

void DenoiserController::OnResourcesRecreated(const uint32_t width, const uint32_t height)
{
    if (m_OIDN != nullptr)
    {
        m_OIDN->OnResourcesRecreated(width, height);
    }
}

bool DenoiserController::BeginOIDNReadback(
    const bool accumulationEnabled,
    const uint32_t accumulationFrameIndex)
{
    return IsOIDNEnabled() && m_OIDN != nullptr &&
        m_OIDN->BeginReadback(accumulationEnabled, accumulationFrameIndex, m_OIDNStaticSpp);
}

bool DenoiserController::RecordOIDNReadback(
    CommandList& commandList,
    const std::shared_ptr<Texture>& source)
{
    return IsOIDNEnabled() && m_OIDN != nullptr && m_OIDN->RecordReadback(commandList, source);
}

void DenoiserController::EndOIDNReadback(const uint64_t submittedFenceValue)
{
    if (m_OIDN != nullptr)
    {
        m_OIDN->EndReadback(submittedFenceValue);
    }
}

void DenoiserController::CancelOIDNReadback()
{
    if (m_OIDN != nullptr)
    {
        m_OIDN->CancelReadback();
    }
}

void DenoiserController::PollOIDN(CommandQueue& directQueue)
{
    if (m_OIDN != nullptr)
    {
        m_OIDN->Poll(directQueue);
    }
}

NRD::DenoiserMode DenoiserController::GetNRDMode() const
{
    return m_NRD != nullptr ? m_NRD->GetSettings().Mode : NRD::DenoiserMode::ReblurDiffuse;
}

uint32_t DenoiserController::GetSVGFAtrousIterations() const
{
    return m_SVGF != nullptr ? m_SVGF->GetSettings().AtrousIterations : 1u;
}

void DenoiserController::FillCameraConstants(
    uint32_t& nrdDenoiserMode,
    DirectX::XMFLOAT4& nrdReblurHitDistanceParameters) const
{
    if (!IsNRDEnabled() || m_NRD == nullptr)
    {
        return;
    }

    const NRD::Settings& nrdSettings = m_NRD->GetSettings();
    nrdDenoiserMode = static_cast<uint32_t>(nrdSettings.Mode);
    nrdReblurHitDistanceParameters = {
        nrdSettings.ReblurHitDistanceA,
        nrdSettings.ReblurHitDistanceB,
        nrdSettings.ReblurHitDistanceC,
        0.0f
    };
}

bool DenoiserController::DrawImGui()
{
    bool changed = false;

    const char* denoiserNames[] = { "Off", "NRD", "SVGF", "OIDN" };
    int selectedDenoiser = static_cast<int>(m_Algorithm);
    if (ImGui::Combo("Denoiser##DenoiserAlgorithm", &selectedDenoiser, denoiserNames, 4))
    {
        SetAlgorithm(static_cast<Algorithm>(selectedDenoiser));
        ResetHistory();
        changed = true;
    }

    if (IsOIDNEnabled())
    {
        ImGui::TextDisabled(
            m_OIDN != nullptr && m_OIDN->IsUsingCuda()
                ? "OIDN backend: CUDA, D3D12 shared GPU memory, Quality::Fast."
                : "OIDN backend: CPU fallback, Quality::Fast.");
        ImGui::TextDisabled("OIDN automatically accumulates static samples and holds its denoised result until the scene changes.");
        int oidnStaticSpp = static_cast<int>(m_OIDNStaticSpp);
        if (FrameworkImGui::SliderInt("OIDN Static SPP", &oidnStaticSpp, 1, 4096))
        {
            SetOIDNStaticSpp(static_cast<uint32_t>(oidnStaticSpp));
        }
    }

    if (IsNRDEnabled() && m_NRD != nullptr && ImGui::CollapsingHeader("NRD Settings"))
    {
        NRD::Settings& nrdSettings = m_NRD->GetSettings();
        bool nrdChanged = false;

        const char* nrdDenoiserNames[] = { "RELAX Diffuse", "ReBLUR Diffuse" };
        int denoiserMode = static_cast<int>(nrdSettings.Mode);
        if (ImGui::Combo("Denoiser##NRDMode", &denoiserMode, nrdDenoiserNames, 2))
        {
            nrdSettings.Mode = static_cast<NRD::DenoiserMode>(denoiserMode);
            nrdChanged = true;
        }

        nrdChanged |= FrameworkImGui::SliderFloat("Denoising Range", &nrdSettings.DenoisingRange, 1.0f, 500000.0f, "%.0f");

        auto sliderUint = [&nrdChanged](const char* label, uint32_t& value, int minValue, int maxValue)
        {
            int temporaryValue = static_cast<int>(value);
            if (FrameworkImGui::SliderInt(label, &temporaryValue, minValue, maxValue))
            {
                value = static_cast<uint32_t>(temporaryValue);
                nrdChanged = true;
            }
        };

        if (nrdSettings.Mode == NRD::DenoiserMode::RelaxDiffuse)
        {
            sliderUint("Relax History", nrdSettings.RelaxDiffuseMaxAccumulatedFrameNum, 0, 255);
            sliderUint("Relax Fast History", nrdSettings.RelaxDiffuseMaxFastAccumulatedFrameNum, 0, 255);
            sliderUint("Relax History Fix", nrdSettings.RelaxHistoryFixFrameNum, 0, 3);
            sliderUint("Relax History Stride", nrdSettings.RelaxHistoryFixBasePixelStride, 1, 32);
            nrdChanged |= FrameworkImGui::SliderFloat("Relax History Sigma", &nrdSettings.RelaxFastHistoryClampingSigmaScale, 1.0f, 3.0f, "%.2f");
            nrdChanged |= FrameworkImGui::SliderFloat("Relax Prepass Radius", &nrdSettings.RelaxDiffusePrepassBlurRadius, 0.0f, 80.0f, "%.1f");
            nrdChanged |= FrameworkImGui::SliderFloat("Relax Min Hit Weight", &nrdSettings.RelaxMinHitDistanceWeight, 0.001f, 0.2f, "%.3f");
            sliderUint("Relax Variance History", nrdSettings.RelaxSpatialVarianceEstimationHistoryThreshold, 0, 16);
            nrdChanged |= FrameworkImGui::SliderFloat("Relax Phi Luminance", &nrdSettings.RelaxDiffusePhiLuminance, 0.1f, 16.0f, "%.2f");
            nrdChanged |= FrameworkImGui::SliderFloat("Relax Lobe Fraction", &nrdSettings.RelaxLobeAngleFraction, 0.01f, 1.0f, "%.2f");
            nrdChanged |= FrameworkImGui::SliderFloat("Relax Roughness Fraction", &nrdSettings.RelaxRoughnessFraction, 0.01f, 1.0f, "%.2f");
            sliderUint("Relax A-Trous Iterations", nrdSettings.RelaxAtrousIterationNum, 2, 8);
            nrdChanged |= FrameworkImGui::SliderFloat("Relax Depth Threshold", &nrdSettings.RelaxDepthThreshold, 0.0001f, 0.05f, "%.4f");
            nrdChanged |= FrameworkImGui::SliderFloat("Relax Luma Relax", &nrdSettings.RelaxLuminanceEdgeStoppingRelaxation, 0.0f, 2.0f, "%.2f");
            nrdChanged |= FrameworkImGui::SliderFloat("Relax Normal Relax", &nrdSettings.RelaxNormalEdgeStoppingRelaxation, 0.0f, 2.0f, "%.2f");
            nrdChanged |= FrameworkImGui::SliderFloat("Relax Roughness Relax", &nrdSettings.RelaxRoughnessEdgeStoppingRelaxation, 0.0f, 2.0f, "%.2f");
            nrdChanged |= ImGui::Checkbox("Relax Anti-Firefly", &nrdSettings.RelaxEnableAntiFirefly);
            nrdChanged |= ImGui::Checkbox("Relax Roughness Stop", &nrdSettings.RelaxEnableRoughnessEdgeStopping);
        }
        else
        {
            nrdChanged |= FrameworkImGui::SliderFloat("ReBLUR Hit A", &nrdSettings.ReblurHitDistanceA, 0.001f, 20.0f, "%.3f");
            nrdChanged |= FrameworkImGui::SliderFloat("ReBLUR Hit B", &nrdSettings.ReblurHitDistanceB, 0.001f, 2.0f, "%.3f");
            nrdChanged |= FrameworkImGui::SliderFloat("ReBLUR Hit C", &nrdSettings.ReblurHitDistanceC, 1.0f, 100.0f, "%.1f");
            sliderUint("ReBLUR History", nrdSettings.ReblurMaxAccumulatedFrameNum, 0, 63);
            sliderUint("ReBLUR Fast History", nrdSettings.ReblurMaxFastAccumulatedFrameNum, 0, 63);
            sliderUint("ReBLUR History Fix", nrdSettings.ReblurHistoryFixFrameNum, 0, 16);
            sliderUint("ReBLUR History Stride", nrdSettings.ReblurHistoryFixBasePixelStride, 1, 32);
            nrdChanged |= FrameworkImGui::SliderFloat("ReBLUR History Sigma", &nrdSettings.ReblurFastHistoryClampingSigmaScale, 1.0f, 3.0f, "%.2f");
            nrdChanged |= FrameworkImGui::SliderFloat("ReBLUR Prepass Radius", &nrdSettings.ReblurDiffusePrepassBlurRadius, 0.0f, 80.0f, "%.1f");
            nrdChanged |= FrameworkImGui::SliderFloat("ReBLUR Min Hit Weight", &nrdSettings.ReblurMinHitDistanceWeight, 0.001f, 0.2f, "%.3f");
            nrdChanged |= FrameworkImGui::SliderFloat("ReBLUR Min Radius", &nrdSettings.ReblurMinBlurRadius, 0.0f, 16.0f, "%.1f");
            nrdChanged |= FrameworkImGui::SliderFloat("ReBLUR Max Radius", &nrdSettings.ReblurMaxBlurRadius, 0.0f, 80.0f, "%.1f");
            nrdChanged |= FrameworkImGui::SliderFloat("ReBLUR Lobe Fraction", &nrdSettings.ReblurLobeAngleFraction, 0.01f, 1.0f, "%.2f");
            nrdChanged |= FrameworkImGui::SliderFloat("ReBLUR Roughness Fraction", &nrdSettings.ReblurRoughnessFraction, 0.01f, 1.0f, "%.2f");
            nrdChanged |= FrameworkImGui::SliderFloat("ReBLUR Plane Sensitivity", &nrdSettings.ReblurPlaneDistanceSensitivity, 0.001f, 0.2f, "%.3f");
            nrdChanged |= FrameworkImGui::SliderFloat("ReBLUR Firefly Scale", &nrdSettings.ReblurFireflySuppressorMinRelativeScale, 1.0f, 3.0f, "%.2f");
            nrdChanged |= ImGui::Checkbox("ReBLUR Anti-Firefly", &nrdSettings.ReblurEnableAntiFirefly);
        }

        if (nrdChanged)
        {
            m_NRD->ResetHistory();
            changed = true;
        }
    }

    if (IsSVGFEnabled() && m_SVGF != nullptr && ImGui::CollapsingHeader("SVGF Settings"))
    {
        SVGF::Settings& svgfSettings = m_SVGF->GetSettings();
        bool svgfChanged = false;
        int atrousIterations = static_cast<int>(svgfSettings.AtrousIterations);
        if (FrameworkImGui::SliderInt("SVGF A-Trous Iterations", &atrousIterations, 1, 8))
        {
            svgfSettings.AtrousIterations = static_cast<uint32_t>(atrousIterations);
            svgfChanged = true;
        }
        svgfChanged |= FrameworkImGui::SliderFloat("SVGF Temporal Alpha", &svgfSettings.TemporalAlpha, 0.001f, 1.0f, "%.3f");
        svgfChanged |= FrameworkImGui::SliderFloat("SVGF Moments Alpha", &svgfSettings.MomentsAlpha, 0.001f, 1.0f, "%.3f");
        svgfChanged |= FrameworkImGui::SliderFloat("SVGF Phi Color", &svgfSettings.PhiColor, 0.1f, 32.0f, "%.2f");
        svgfChanged |= FrameworkImGui::SliderFloat("SVGF Phi Normal", &svgfSettings.PhiNormal, 1.0f, 256.0f, "%.1f");
        svgfChanged |= FrameworkImGui::SliderFloat("SVGF Phi Depth", &svgfSettings.PhiDepth, 0.001f, 10.0f, "%.3f");
        if (svgfChanged)
        {
            m_SVGF->ResetHistory();
            changed = true;
        }
    }

    return changed;
}

void DenoiserController::AddNRDPasses(
    RenderGraph::RenderGraphBuilder& builder,
    NRD::GraphInputs inputs)
{
    Assert(IsNRDEnabled() && m_NRD != nullptr, "NRD graph registration requires the NRD selection.");
    m_NRD->AddPasses(builder, std::move(inputs));
}

void DenoiserController::AddSVGFPasses(
    RenderGraph::RenderGraphBuilder& builder,
    SVGF::GraphInputs inputs)
{
    Assert(IsSVGFEnabled() && m_SVGF != nullptr, "SVGF graph registration requires the SVGF selection.");
    m_SVGF->AddPasses(builder, std::move(inputs));
}

void DenoiserController::AddOIDNPasses(
    RenderGraph::RenderGraphBuilder& builder,
    OIDNDenoiser::GraphInputs inputs)
{
    Assert(IsOIDNEnabled() && m_OIDN != nullptr, "OIDN graph registration requires the OIDN selection.");
    m_OIDN->AddPasses(builder, std::move(inputs));
}

void DenoiserController::ApplySelection()
{
    if (m_NRD != nullptr)
    {
        m_NRD->SetEnabled(IsNRDEnabled());
    }

    if (m_SVGF != nullptr)
    {
        m_SVGF->SetEnabled(IsSVGFEnabled());
    }

    if (m_OIDN != nullptr)
    {
        m_OIDN->SetEnabled(IsOIDNEnabled());
    }
}
//Modify End
