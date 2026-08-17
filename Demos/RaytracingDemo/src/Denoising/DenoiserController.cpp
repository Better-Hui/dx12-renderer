//Modify Begin:2026-07-27 by Hui
#include <Denoising/DenoiserController.h>

#include <DX12Library/Texture.h>

#include <imgui.h>

#include <cstring>

void DenoiserController::Initialize(FrameworkDeviceContext& deviceContext)
{
    m_NRD = std::make_unique<NRD>(deviceContext);
    m_SVGF = std::make_unique<SVGF>(deviceContext);
    ApplySelection();
}

void DenoiserController::Shutdown()
{
    m_SVGF.reset();
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
}

void DenoiserController::FillCameraConstants(
    uint32_t& nrdDenoiserMode,
    DirectX::XMFLOAT4& nrdReblurHitDistanceParameters) const
{
    if (m_NRD == nullptr)
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

    const char* denoiserNames[] = { "Off", "NRD", "SVGF" };
    int selectedDenoiser = static_cast<int>(m_Algorithm);
    if (ImGui::Combo("Denoiser##DenoiserAlgorithm", &selectedDenoiser, denoiserNames, 3))
    {
        SetAlgorithm(static_cast<Algorithm>(selectedDenoiser));
        ResetHistory();
        changed = true;
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

        nrdChanged |= ImGui::SliderFloat("Denoising Range", &nrdSettings.DenoisingRange, 1.0f, 500000.0f, "%.0f");

        auto sliderUint = [&nrdChanged](const char* label, uint32_t& value, int minValue, int maxValue)
        {
            int temporaryValue = static_cast<int>(value);
            if (ImGui::SliderInt(label, &temporaryValue, minValue, maxValue))
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
            nrdChanged |= ImGui::SliderFloat("Relax History Sigma", &nrdSettings.RelaxFastHistoryClampingSigmaScale, 1.0f, 3.0f, "%.2f");
            nrdChanged |= ImGui::SliderFloat("Relax Prepass Radius", &nrdSettings.RelaxDiffusePrepassBlurRadius, 0.0f, 80.0f, "%.1f");
            nrdChanged |= ImGui::SliderFloat("Relax Min Hit Weight", &nrdSettings.RelaxMinHitDistanceWeight, 0.001f, 0.2f, "%.3f");
            sliderUint("Relax Variance History", nrdSettings.RelaxSpatialVarianceEstimationHistoryThreshold, 0, 16);
            nrdChanged |= ImGui::SliderFloat("Relax Phi Luminance", &nrdSettings.RelaxDiffusePhiLuminance, 0.1f, 16.0f, "%.2f");
            nrdChanged |= ImGui::SliderFloat("Relax Lobe Fraction", &nrdSettings.RelaxLobeAngleFraction, 0.01f, 1.0f, "%.2f");
            nrdChanged |= ImGui::SliderFloat("Relax Roughness Fraction", &nrdSettings.RelaxRoughnessFraction, 0.01f, 1.0f, "%.2f");
            sliderUint("Relax A-Trous Iterations", nrdSettings.RelaxAtrousIterationNum, 2, 8);
            nrdChanged |= ImGui::SliderFloat("Relax Depth Threshold", &nrdSettings.RelaxDepthThreshold, 0.0001f, 0.05f, "%.4f");
            nrdChanged |= ImGui::SliderFloat("Relax Luma Relax", &nrdSettings.RelaxLuminanceEdgeStoppingRelaxation, 0.0f, 2.0f, "%.2f");
            nrdChanged |= ImGui::SliderFloat("Relax Normal Relax", &nrdSettings.RelaxNormalEdgeStoppingRelaxation, 0.0f, 2.0f, "%.2f");
            nrdChanged |= ImGui::SliderFloat("Relax Roughness Relax", &nrdSettings.RelaxRoughnessEdgeStoppingRelaxation, 0.0f, 2.0f, "%.2f");
            nrdChanged |= ImGui::Checkbox("Relax Anti-Firefly", &nrdSettings.RelaxEnableAntiFirefly);
            nrdChanged |= ImGui::Checkbox("Relax Roughness Stop", &nrdSettings.RelaxEnableRoughnessEdgeStopping);
        }
        else
        {
            nrdChanged |= ImGui::SliderFloat("ReBLUR Hit A", &nrdSettings.ReblurHitDistanceA, 0.001f, 20.0f, "%.3f");
            nrdChanged |= ImGui::SliderFloat("ReBLUR Hit B", &nrdSettings.ReblurHitDistanceB, 0.001f, 2.0f, "%.3f");
            nrdChanged |= ImGui::SliderFloat("ReBLUR Hit C", &nrdSettings.ReblurHitDistanceC, 1.0f, 100.0f, "%.1f");
            sliderUint("ReBLUR History", nrdSettings.ReblurMaxAccumulatedFrameNum, 0, 63);
            sliderUint("ReBLUR Fast History", nrdSettings.ReblurMaxFastAccumulatedFrameNum, 0, 63);
            sliderUint("ReBLUR History Fix", nrdSettings.ReblurHistoryFixFrameNum, 0, 16);
            sliderUint("ReBLUR History Stride", nrdSettings.ReblurHistoryFixBasePixelStride, 1, 32);
            nrdChanged |= ImGui::SliderFloat("ReBLUR History Sigma", &nrdSettings.ReblurFastHistoryClampingSigmaScale, 1.0f, 3.0f, "%.2f");
            nrdChanged |= ImGui::SliderFloat("ReBLUR Prepass Radius", &nrdSettings.ReblurDiffusePrepassBlurRadius, 0.0f, 80.0f, "%.1f");
            nrdChanged |= ImGui::SliderFloat("ReBLUR Min Hit Weight", &nrdSettings.ReblurMinHitDistanceWeight, 0.001f, 0.2f, "%.3f");
            nrdChanged |= ImGui::SliderFloat("ReBLUR Min Radius", &nrdSettings.ReblurMinBlurRadius, 0.0f, 16.0f, "%.1f");
            nrdChanged |= ImGui::SliderFloat("ReBLUR Max Radius", &nrdSettings.ReblurMaxBlurRadius, 0.0f, 80.0f, "%.1f");
            nrdChanged |= ImGui::SliderFloat("ReBLUR Lobe Fraction", &nrdSettings.ReblurLobeAngleFraction, 0.01f, 1.0f, "%.2f");
            nrdChanged |= ImGui::SliderFloat("ReBLUR Roughness Fraction", &nrdSettings.ReblurRoughnessFraction, 0.01f, 1.0f, "%.2f");
            nrdChanged |= ImGui::SliderFloat("ReBLUR Plane Sensitivity", &nrdSettings.ReblurPlaneDistanceSensitivity, 0.001f, 0.2f, "%.3f");
            nrdChanged |= ImGui::SliderFloat("ReBLUR Firefly Scale", &nrdSettings.ReblurFireflySuppressorMinRelativeScale, 1.0f, 3.0f, "%.2f");
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
        if (ImGui::SliderInt("SVGF A-Trous Iterations", &atrousIterations, 1, 8))
        {
            svgfSettings.AtrousIterations = static_cast<uint32_t>(atrousIterations);
            svgfChanged = true;
        }
        svgfChanged |= ImGui::SliderFloat("SVGF Temporal Alpha", &svgfSettings.TemporalAlpha, 0.001f, 1.0f, "%.3f");
        svgfChanged |= ImGui::SliderFloat("SVGF Moments Alpha", &svgfSettings.MomentsAlpha, 0.001f, 1.0f, "%.3f");
        svgfChanged |= ImGui::SliderFloat("SVGF Phi Color", &svgfSettings.PhiColor, 0.1f, 32.0f, "%.2f");
        svgfChanged |= ImGui::SliderFloat("SVGF Phi Normal", &svgfSettings.PhiNormal, 1.0f, 256.0f, "%.1f");
        svgfChanged |= ImGui::SliderFloat("SVGF Phi Depth", &svgfSettings.PhiDepth, 0.001f, 10.0f, "%.3f");
        if (svgfChanged)
        {
            m_SVGF->ResetHistory();
            changed = true;
        }
    }

    return changed;
}

void DenoiserController::Execute(
    CommandList& commandList,
    const NRD::FrameMatrices& frameMatrices,
    const RaytracingDemoRenderGraph::FrameGBufferResources& gbuffer,
    const RaytracingDemoRenderGraph::LightingResources& lighting,
    const RaytracingDemoRenderGraph::NRDResources& nrdResources,
    const uint32_t width,
    const uint32_t height)
{
    if (!IsEnabled())
    {
        return;
    }

    if (IsNRDEnabled())
    {
        if (m_NRD == nullptr)
        {
            return;
        }

        m_NRD->PrepareDenoiserInputs(
            commandList,
            frameMatrices,
            gbuffer.SpecularSmoothness,
            gbuffer.Normal,
            gbuffer.Position,
            gbuffer.Depth,
            gbuffer.MotionVector,
            nrdResources.NormalRoughness,
            nrdResources.ViewZ,
            nrdResources.Motion,
            width,
            height);

        m_NRD->Execute(
            commandList,
            frameMatrices,
            lighting.NRDNoisyRadiance,
            gbuffer.AlbedoOcclusion,
            gbuffer.EmissionMetallic,
            gbuffer.Depth,
            nrdResources.NormalRoughness,
            nrdResources.ViewZ,
            nrdResources.Motion,
            nrdResources.DenoisedRadiance,
            lighting.SceneColor,
            width,
            height);
        return;
    }

    if (IsSVGFEnabled())
    {
        if (m_SVGF == nullptr)
        {
            return;
        }

        m_SVGF->Execute(
            commandList,
            lighting.NoisyRadiance,
            gbuffer.Normal,
            gbuffer.Position,
            gbuffer.MotionVector,
            gbuffer.Depth,
            lighting.SceneColor,
            width,
            height);
    }
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
}
//Modify End
