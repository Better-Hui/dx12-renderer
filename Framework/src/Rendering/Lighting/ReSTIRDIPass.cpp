#include <Framework/Rendering/Lighting/ReSTIRDIPass.h>

//Modify Begin:2026-07-30 by BestHui
#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/Texture.h>
#include <Framework/Core/FrameworkDeviceContext.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/Pipeline/ComputePipelineStateBuilder.h>
#include <Framework/Rendering/Pipeline/ComputeShader.h>
//Modify Begin:2026-07-30 by BestHui
#include <Framework/Rendering/Pipeline/ShaderTargetProfile.h>
//Modify End
#include <Framework/Rendering/Texture/RenderTexture.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>
#include <Framework/Rendering/Texture/UnorderedAccessView.h>

#include <iterator>
#include <unordered_map>

struct ReSTIRDIPass::PipelineSet
{
    bool UseSoftShadowVariant = false;
    uint32_t EnvironmentProjectionVariant = 0u;
    std::unordered_map<uint32_t, std::unique_ptr<ComputeShader>> RISVariants;
    std::unordered_map<uint32_t, std::unique_ptr<ComputeShader>> TemporalVariants;
    std::unordered_map<uint32_t, std::unique_ptr<ComputeShader>> SpatialVariants;
    std::unordered_map<uint32_t, std::unique_ptr<ComputeShader>> ShadeVariants;
};

struct ReSTIRDIPass::InternalResources
{
    std::shared_ptr<Texture> ReservoirA;
    std::shared_ptr<Texture> ReservoirB;
    std::shared_ptr<Texture> ReservoirAState;
    std::shared_ptr<Texture> ReservoirBState;
    std::shared_ptr<Texture> HistoryPositionA;
    std::shared_ptr<Texture> HistoryPositionB;
    std::shared_ptr<Texture> HistoryNormalRoughnessA;
    std::shared_ptr<Texture> HistoryNormalRoughnessB;
    std::shared_ptr<Texture> HistoryDiffuseMetallicA;
    std::shared_ptr<Texture> HistoryDiffuseMetallicB;
    std::shared_ptr<Texture> HistorySpecularOcclusionA;
    std::shared_ptr<Texture> HistorySpecularOcclusionB;
    std::shared_ptr<Texture> InitialReservoir;
    std::shared_ptr<Texture> InitialReservoirState;
    std::shared_ptr<Texture> TemporalReservoir;
    std::shared_ptr<Texture> TemporalReservoirState;
    std::shared_ptr<Texture> SpatialReservoir;
    std::shared_ptr<Texture> SpatialReservoirState;
};

namespace
{
    constexpr DXGI_FORMAT RESERVOIR_FORMAT = DXGI_FORMAT_R32G32B32A32_UINT;
    constexpr DXGI_FORMAT HISTORY_POSITION_FORMAT = DXGI_FORMAT_R32G32B32A32_FLOAT;
    constexpr DXGI_FORMAT HISTORY_SHADING_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT;

    void InsertUavBarrier(CommandList& commandList, const std::shared_ptr<Texture>& texture)
    {
        commandList.UavBarrier(*texture);
    }
}

ReSTIRDIPass::ReSTIRDIPass(
    FrameworkDeviceContext& deviceContext,
    ReSTIRDIShaderSources shaderSources)
    : m_DeviceContext(deviceContext)
    , m_ShaderSources(std::move(shaderSources))
{
}

ReSTIRDIPass::~ReSTIRDIPass() = default;

void ReSTIRDIPass::EnsurePipelines(
    const bool useSoftShadowVariant,
    const uint32_t environmentProjectionVariant,
    const ReSTIRDIFrameConstants& constants,
    const MaterialShadingModel shadingModel)
{
    PipelineSet& pipelines = GetPipelines(useSoftShadowVariant, environmentProjectionVariant);
    GetStageShader(pipelines, ReSTIRDIStage::RIS, constants, shadingModel);
    GetStageShader(pipelines, ReSTIRDIStage::Temporal, constants, shadingModel);
    GetStageShader(pipelines, ReSTIRDIStage::Spatial, constants, shadingModel);
    GetStageShader(pipelines, ReSTIRDIStage::Shade, constants, shadingModel);
}

void ReSTIRDIPass::Execute(
    CommandList& commandList,
    const ReSTIRDIExecutionInputs& inputs)
{
    if (!inputs.FrameState.Enabled)
    {
        return;
    }

    Assert(static_cast<bool>(inputs.BindSceneInputs), "ReSTIR DI requires scene input bindings.");
    Assert(inputs.DirectLighting != nullptr, "ReSTIR DI requires a direct lighting output.");
    Assert(inputs.MotionVector != nullptr, "ReSTIR DI requires motion vectors.");

    PipelineSet& pipelines = GetPipelines(
        inputs.FrameState.UseSoftShadowVariant,
        inputs.FrameState.EnvironmentProjectionVariant);
    EnsureResources(inputs.FrameState.Width, inputs.FrameState.Height);
    CommandContext commandContext(commandList);
    if (inputs.PrepareCommandContext)
    {
        inputs.PrepareCommandContext(commandContext);
    }

    ExecuteInitialSampling(commandContext, inputs, pipelines);
    InsertUavBarrier(commandList, m_Resources->InitialReservoir);
    InsertUavBarrier(commandList, m_Resources->InitialReservoirState);

    const bool temporalResamplingEnabled = inputs.FrameState.Constants.TemporalResamplingEnabled != 0u;
    const bool spatialResamplingEnabled = inputs.FrameState.Constants.SpatialResamplingEnabled != 0u;
    std::shared_ptr<Texture> finalReservoir = m_Resources->InitialReservoir;
    std::shared_ptr<Texture> finalReservoirState = m_Resources->InitialReservoirState;

    if (temporalResamplingEnabled)
    {
        ExecuteTemporalResampling(commandContext, inputs, pipelines);
        InsertUavBarrier(commandList, m_Resources->TemporalReservoir);
        InsertUavBarrier(commandList, m_Resources->TemporalReservoirState);
        finalReservoir = m_Resources->TemporalReservoir;
        finalReservoirState = m_Resources->TemporalReservoirState;
    }

    if (spatialResamplingEnabled)
    {
        ExecuteSpatialResampling(
            commandContext,
            inputs,
            pipelines,
            finalReservoir,
            finalReservoirState);
        InsertUavBarrier(commandList, m_Resources->SpatialReservoir);
        InsertUavBarrier(commandList, m_Resources->SpatialReservoirState);
        finalReservoir = m_Resources->SpatialReservoir;
        finalReservoirState = m_Resources->SpatialReservoirState;
    }

    ExecuteFinalShading(commandContext, inputs, pipelines, finalReservoir, finalReservoirState);
    InsertUavBarrier(commandList, inputs.DirectLighting);
}

void ReSTIRDIPass::EnsureResources(const uint32_t width, const uint32_t height)
{
    if (m_Resources != nullptr && m_ResourceWidth == width && m_ResourceHeight == height)
    {
        return;
    }

    m_Resources = std::make_unique<InternalResources>();
//Modify Begin:2026-08-12 by BestHui
    const auto createReservoir = [this, width, height](const wchar_t* name)
    {
        return RenderTexture::CreateUav2D(m_DeviceContext, RESERVOIR_FORMAT, width, height, name);
    };
    const auto createHistoryPosition = [this, width, height](const wchar_t* name)
    {
        return RenderTexture::CreateUav2D(m_DeviceContext, HISTORY_POSITION_FORMAT, width, height, name);
    };
    const auto createHistoryShading = [this, width, height](const wchar_t* name)
    {
        return RenderTexture::CreateUav2D(m_DeviceContext, HISTORY_SHADING_FORMAT, width, height, name);
    };
//Modify End

    m_Resources->ReservoirA = createReservoir(L"ReSTIR DI Reservoir A");
    m_Resources->ReservoirB = createReservoir(L"ReSTIR DI Reservoir B");
    m_Resources->ReservoirAState = createReservoir(L"ReSTIR DI Reservoir A State");
    m_Resources->ReservoirBState = createReservoir(L"ReSTIR DI Reservoir B State");
    m_Resources->HistoryPositionA = createHistoryPosition(L"ReSTIR DI History Position A");
    m_Resources->HistoryPositionB = createHistoryPosition(L"ReSTIR DI History Position B");
    m_Resources->HistoryNormalRoughnessA = createHistoryShading(L"ReSTIR DI History Normal Roughness A");
    m_Resources->HistoryNormalRoughnessB = createHistoryShading(L"ReSTIR DI History Normal Roughness B");
    m_Resources->HistoryDiffuseMetallicA = createHistoryShading(L"ReSTIR DI History Diffuse Metallic A");
    m_Resources->HistoryDiffuseMetallicB = createHistoryShading(L"ReSTIR DI History Diffuse Metallic B");
    m_Resources->HistorySpecularOcclusionA = createHistoryShading(L"ReSTIR DI History Specular Occlusion A");
    m_Resources->HistorySpecularOcclusionB = createHistoryShading(L"ReSTIR DI History Specular Occlusion B");
    m_Resources->InitialReservoir = createReservoir(L"ReSTIR DI RIS Reservoir");
    m_Resources->InitialReservoirState = createReservoir(L"ReSTIR DI RIS Reservoir State");
    m_Resources->TemporalReservoir = createReservoir(L"ReSTIR DI Temporal Reservoir");
    m_Resources->TemporalReservoirState = createReservoir(L"ReSTIR DI Temporal Reservoir State");
    m_Resources->SpatialReservoir = createReservoir(L"ReSTIR DI Spatial Reservoir");
    m_Resources->SpatialReservoirState = createReservoir(L"ReSTIR DI Spatial Reservoir State");
    m_ResourceWidth = width;
    m_ResourceHeight = height;
}

void ReSTIRDIPass::ExecuteInitialSampling(
    CommandContext& commandContext,
    const ReSTIRDIExecutionInputs& inputs,
    PipelineSet& pipelines)
{
    ComputeShader& shader = GetStageShader(
        pipelines,
        ReSTIRDIStage::RIS,
        inputs.FrameState.Constants,
        inputs.FrameState.ShadingModel);
    inputs.BindSceneInputs(commandContext, shader);
    commandContext.SetConstantBuffer(shader, "ReSTIRDIConstants", sizeof(inputs.FrameState.Constants), &inputs.FrameState.Constants);
    commandContext.SetUnorderedAccessView(shader, "ReSTIRDIRISReservoir", UnorderedAccessView(m_Resources->InitialReservoir));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRDIRISReservoirState", UnorderedAccessView(m_Resources->InitialReservoirState));
    commandContext.BindPipeline(shader);
    commandContext.BindDescriptorSet(shader.GetDescriptorSet());
    commandContext.Dispatch(Math::DivideByMultiple(inputs.FrameState.Width, 8u), Math::DivideByMultiple(inputs.FrameState.Height, 8u), 1u);
}

void ReSTIRDIPass::ExecuteTemporalResampling(
    CommandContext& commandContext,
    const ReSTIRDIExecutionInputs& inputs,
    PipelineSet& pipelines)
{
    ComputeShader& shader = GetStageShader(
        pipelines,
        ReSTIRDIStage::Temporal,
        inputs.FrameState.Constants,
        inputs.FrameState.ShadingModel);
    const bool writeReservoirA = (inputs.FrameState.FrameIndex & 1u) == 0u;
    inputs.BindSceneInputs(commandContext, shader);
    commandContext.SetConstantBuffer(shader, "ReSTIRDIConstants", sizeof(inputs.FrameState.Constants), &inputs.FrameState.Constants);
    commandContext.SetShaderResourceView(shader, "ReSTIRDIRISReservoir", ShaderResourceView(m_Resources->InitialReservoir));
    commandContext.SetShaderResourceView(shader, "ReSTIRDIRISReservoirState", ShaderResourceView(m_Resources->InitialReservoirState));
    commandContext.SetShaderResourceView(shader, "MotionVectorTexture", ShaderResourceView(inputs.MotionVector));
    commandContext.SetShaderResourceView(shader, "ReSTIRDIHistoryReservoir", ShaderResourceView(writeReservoirA ? m_Resources->ReservoirB : m_Resources->ReservoirA));
    commandContext.SetShaderResourceView(shader, "ReSTIRDIHistoryReservoirState", ShaderResourceView(writeReservoirA ? m_Resources->ReservoirBState : m_Resources->ReservoirAState));
    commandContext.SetShaderResourceView(shader, "ReSTIRDIHistoryPosition", ShaderResourceView(writeReservoirA ? m_Resources->HistoryPositionB : m_Resources->HistoryPositionA));
    commandContext.SetShaderResourceView(shader, "ReSTIRDIHistoryNormalRoughness", ShaderResourceView(writeReservoirA ? m_Resources->HistoryNormalRoughnessB : m_Resources->HistoryNormalRoughnessA));
    commandContext.SetShaderResourceView(shader, "ReSTIRDIHistoryDiffuseMetallic", ShaderResourceView(writeReservoirA ? m_Resources->HistoryDiffuseMetallicB : m_Resources->HistoryDiffuseMetallicA));
    commandContext.SetShaderResourceView(shader, "ReSTIRDIHistorySpecularOcclusion", ShaderResourceView(writeReservoirA ? m_Resources->HistorySpecularOcclusionB : m_Resources->HistorySpecularOcclusionA));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRDITemporalReservoir", UnorderedAccessView(m_Resources->TemporalReservoir));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRDITemporalReservoirState", UnorderedAccessView(m_Resources->TemporalReservoirState));
    commandContext.BindPipeline(shader);
    commandContext.BindDescriptorSet(shader.GetDescriptorSet());
    commandContext.Dispatch(Math::DivideByMultiple(inputs.FrameState.Width, 8u), Math::DivideByMultiple(inputs.FrameState.Height, 8u), 1u);
}

void ReSTIRDIPass::ExecuteSpatialResampling(
    CommandContext& commandContext,
    const ReSTIRDIExecutionInputs& inputs,
    PipelineSet& pipelines,
    const std::shared_ptr<Texture>& inputReservoir,
    const std::shared_ptr<Texture>& inputReservoirState)
{
    ComputeShader& shader = GetStageShader(
        pipelines,
        ReSTIRDIStage::Spatial,
        inputs.FrameState.Constants,
        inputs.FrameState.ShadingModel);
    inputs.BindSceneInputs(commandContext, shader);
    commandContext.SetConstantBuffer(shader, "ReSTIRDIConstants", sizeof(inputs.FrameState.Constants), &inputs.FrameState.Constants);
    commandContext.SetShaderResourceView(shader, "ReSTIRDITemporalReservoir", ShaderResourceView(inputReservoir));
    commandContext.SetShaderResourceView(shader, "ReSTIRDITemporalReservoirState", ShaderResourceView(inputReservoirState));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRDISpatialReservoir", UnorderedAccessView(m_Resources->SpatialReservoir));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRDISpatialReservoirState", UnorderedAccessView(m_Resources->SpatialReservoirState));
    commandContext.BindPipeline(shader);
    commandContext.BindDescriptorSet(shader.GetDescriptorSet());
    commandContext.Dispatch(Math::DivideByMultiple(inputs.FrameState.Width, 8u), Math::DivideByMultiple(inputs.FrameState.Height, 8u), 1u);
}

void ReSTIRDIPass::ExecuteFinalShading(
    CommandContext& commandContext,
    const ReSTIRDIExecutionInputs& inputs,
    PipelineSet& pipelines,
    const std::shared_ptr<Texture>& finalReservoir,
    const std::shared_ptr<Texture>& finalReservoirState)
{
    ComputeShader& shader = GetStageShader(
        pipelines,
        ReSTIRDIStage::Shade,
        inputs.FrameState.Constants,
        inputs.FrameState.ShadingModel);
    const bool writeReservoirA = (inputs.FrameState.FrameIndex & 1u) == 0u;
    inputs.BindSceneInputs(commandContext, shader);
    commandContext.SetConstantBuffer(shader, "ReSTIRDIConstants", sizeof(inputs.FrameState.Constants), &inputs.FrameState.Constants);
    commandContext.SetShaderResourceView(shader, "ReSTIRDIFinalReservoir", ShaderResourceView(finalReservoir));
    commandContext.SetShaderResourceView(shader, "ReSTIRDIFinalReservoirState", ShaderResourceView(finalReservoirState));
    commandContext.SetUnorderedAccessView(shader, "DirectLighting", UnorderedAccessView(inputs.DirectLighting));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRDICurrentReservoir", UnorderedAccessView(writeReservoirA ? m_Resources->ReservoirA : m_Resources->ReservoirB));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRDICurrentReservoirState", UnorderedAccessView(writeReservoirA ? m_Resources->ReservoirAState : m_Resources->ReservoirBState));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRDICurrentPosition", UnorderedAccessView(writeReservoirA ? m_Resources->HistoryPositionA : m_Resources->HistoryPositionB));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRDICurrentNormalRoughness", UnorderedAccessView(writeReservoirA ? m_Resources->HistoryNormalRoughnessA : m_Resources->HistoryNormalRoughnessB));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRDICurrentDiffuseMetallic", UnorderedAccessView(writeReservoirA ? m_Resources->HistoryDiffuseMetallicA : m_Resources->HistoryDiffuseMetallicB));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRDICurrentSpecularOcclusion", UnorderedAccessView(writeReservoirA ? m_Resources->HistorySpecularOcclusionA : m_Resources->HistorySpecularOcclusionB));
    commandContext.BindPipeline(shader);
    commandContext.BindDescriptorSet(shader.GetDescriptorSet());
    commandContext.Dispatch(Math::DivideByMultiple(inputs.FrameState.Width, 8u), Math::DivideByMultiple(inputs.FrameState.Height, 8u), 1u);
}

size_t ReSTIRDIPass::GetPipelineVariantIndex(
    const bool useSoftShadowVariant,
    const uint32_t environmentProjectionVariant)
{
//Modify Begin:2026-08-06 by BestHui
    Assert(
        environmentProjectionVariant < EnvironmentProjectionVariantCount,
        "Unsupported ReSTIR DI environment projection variant.");
    return static_cast<size_t>(environmentProjectionVariant * 2u + (useSoftShadowVariant ? 1u : 0u));
//Modify End
}

uint32_t ReSTIRDIPass::GetStageVariantKey(
    const ReSTIRDIStage stage,
    const ReSTIRDIFrameConstants& constants,
    const MaterialShadingModel shadingModel)
{
    uint32_t featureKey = 0u;
    switch (stage)
    {
    case ReSTIRDIStage::RIS:
        featureKey = constants.InitialVisibilityEnabled != 0u ? 1u : 0u;
        break;

    case ReSTIRDIStage::Temporal:
        if (constants.TemporalResamplingEnabled == 0u)
        {
            featureKey = (constants.BoilingFilterEnabled != 0u ? 1u : 0u) << 5u;
            break;
        }
        Assert(constants.TemporalBiasCorrectionMode <= 2u, "Unsupported ReSTIR DI temporal bias correction mode.");
        featureKey = 1u |
            ((constants.TemporalBiasCorrectionMode & 0x3u) << 1u) |
            ((constants.TemporalVisibilityShortcutEnabled != 0u ? 1u : 0u) << 3u) |
            ((constants.TemporalPermutationSamplingEnabled != 0u ? 1u : 0u) << 4u) |
            ((constants.BoilingFilterEnabled != 0u ? 1u : 0u) << 5u);
        break;

    case ReSTIRDIStage::Spatial:
        if (constants.SpatialResamplingEnabled == 0u)
        {
            break;
        }
        Assert(constants.SpatialBiasCorrectionMode <= 3u, "Unsupported ReSTIR DI spatial bias correction mode.");
        featureKey = 1u |
            ((constants.SpatialBiasCorrectionMode & 0x3u) << 1u) |
            ((constants.SpatialMaterialSimilarityTestEnabled != 0u ? 1u : 0u) << 3u) |
            ((constants.SpatialDiscountNaiveSamples != 0u ? 1u : 0u) << 4u);
        break;

    case ReSTIRDIStage::Shade:
        if (constants.FinalVisibilityEnabled == 0u)
        {
            break;
        }
        featureKey = 1u |
            ((constants.FinalVisibilityReuseEnabled != 0u ? 1u : 0u) << 1u) |
            ((constants.FinalVisibilityDiscardInvisibleSamples != 0u ? 1u : 0u) << 2u);
        break;
    }

    return featureKey | (static_cast<uint32_t>(shadingModel) << 8u);
}

std::vector<ShaderVariantDefine> ReSTIRDIPass::GetStageVariantDefines(
    const ReSTIRDIStage stage,
    const ReSTIRDIFrameConstants& constants,
    const MaterialShadingModel shadingModel)
{
    const auto booleanDefine = [](const char* name, const bool value)
    {
        return ShaderVariantDefine { name, value ? "1" : "0" };
    };

    std::vector<ShaderVariantDefine> defines;
    switch (stage)
    {
    case ReSTIRDIStage::RIS:
        defines = {
            booleanDefine("RESTIR_DI_USE_INITIAL_VISIBILITY", constants.InitialVisibilityEnabled != 0u),
        };
        break;

    case ReSTIRDIStage::Temporal:
        defines = {
            booleanDefine("RESTIR_DI_USE_TEMPORAL_REUSE", constants.TemporalResamplingEnabled != 0u),
            { "RESTIR_DI_TEMPORAL_BIAS_MODE", std::to_string(constants.TemporalBiasCorrectionMode) },
            booleanDefine("RESTIR_DI_USE_TEMPORAL_VISIBILITY_SHORTCUT", constants.TemporalVisibilityShortcutEnabled != 0u),
            booleanDefine("RESTIR_DI_USE_TEMPORAL_PERMUTATION_SAMPLING", constants.TemporalPermutationSamplingEnabled != 0u),
            booleanDefine("RESTIR_DI_USE_TEMPORAL_BOILING_FILTER", constants.BoilingFilterEnabled != 0u),
        };
        break;

    case ReSTIRDIStage::Spatial:
        defines = {
            booleanDefine("RESTIR_DI_USE_SPATIAL_REUSE", constants.SpatialResamplingEnabled != 0u),
            { "RESTIR_DI_SPATIAL_BIAS_MODE", std::to_string(constants.SpatialBiasCorrectionMode) },
            booleanDefine("RESTIR_DI_USE_SPATIAL_MATERIAL_SIMILARITY", constants.SpatialMaterialSimilarityTestEnabled != 0u),
            booleanDefine("RESTIR_DI_USE_SPATIAL_NAIVE_SAMPLE_DISCOUNT", constants.SpatialDiscountNaiveSamples != 0u),
        };
        break;

    case ReSTIRDIStage::Shade:
        defines = {
            booleanDefine("RESTIR_DI_USE_FINAL_VISIBILITY", constants.FinalVisibilityEnabled != 0u),
            booleanDefine("RESTIR_DI_USE_FINAL_VISIBILITY_REUSE", constants.FinalVisibilityReuseEnabled != 0u),
            booleanDefine("RESTIR_DI_DISCARD_INVISIBLE_FINAL_SAMPLES", constants.FinalVisibilityDiscardInvisibleSamples != 0u),
        };
        break;
    }

    defines.push_back({
        "FRAMEWORK_MATERIAL_SHADING_MODEL",
        std::to_string(static_cast<uint32_t>(shadingModel))
    });
    return defines;
}

ReSTIRDIPass::PipelineSet& ReSTIRDIPass::GetPipelines(
    const bool useSoftShadowVariant,
    const uint32_t environmentProjectionVariant)
{
    std::unique_ptr<PipelineSet>& pipelines = m_Pipelines[
        GetPipelineVariantIndex(useSoftShadowVariant, environmentProjectionVariant)];
    if (pipelines == nullptr)
    {
        pipelines = std::make_unique<PipelineSet>();
        pipelines->UseSoftShadowVariant = useSoftShadowVariant;
        pipelines->EnvironmentProjectionVariant = environmentProjectionVariant;
    }

    Assert(pipelines != nullptr, "ReSTIR DI pipeline creation failed.");
    return *pipelines;
}

ComputeShader& ReSTIRDIPass::GetStageShader(
    PipelineSet& pipelines,
    const ReSTIRDIStage stage,
    const ReSTIRDIFrameConstants& constants,
    const MaterialShadingModel shadingModel)
{
    std::unordered_map<uint32_t, std::unique_ptr<ComputeShader>>* stagePipelines = nullptr;
    const std::wstring* sourceFileName = nullptr;
    std::wstring compiledFileName;
    switch (stage)
    {
    case ReSTIRDIStage::RIS:
        stagePipelines = &pipelines.RISVariants;
        compiledFileName = L"ReSTIRDI.RIS.cs.cso";
        sourceFileName = &m_ShaderSources.RIS;
        break;
    case ReSTIRDIStage::Temporal:
        stagePipelines = &pipelines.TemporalVariants;
        compiledFileName = L"ReSTIRDI.Temporal.cs.cso";
        sourceFileName = &m_ShaderSources.Temporal;
        break;
    case ReSTIRDIStage::Spatial:
        stagePipelines = &pipelines.SpatialVariants;
        compiledFileName = L"ReSTIRDI.Spatial.cs.cso";
        sourceFileName = &m_ShaderSources.Spatial;
        break;
    case ReSTIRDIStage::Shade:
        stagePipelines = &pipelines.ShadeVariants;
        compiledFileName = L"ReSTIRDI.Shade.cs.cso";
        sourceFileName = &m_ShaderSources.Shade;
        break;
    }

    Assert(stagePipelines != nullptr && sourceFileName != nullptr, "Unsupported ReSTIR DI stage.");
    const uint32_t variantKey = GetStageVariantKey(stage, constants, shadingModel);
    auto [shaderIt, inserted] = stagePipelines->try_emplace(variantKey);
    if (inserted)
    {
        shaderIt->second = CreateComputeShader(
            compiledFileName + L".variant" + std::to_wstring(variantKey),
            *sourceFileName,
            pipelines.UseSoftShadowVariant,
            pipelines.EnvironmentProjectionVariant,
            GetStageVariantDefines(stage, constants, shadingModel));
    }

    Assert(shaderIt->second != nullptr, "ReSTIR DI stage shader creation failed.");
    return *shaderIt->second;
}

std::unique_ptr<ComputeShader> ReSTIRDIPass::CreateComputeShader(
    const std::wstring& compiledFileName,
    const std::wstring& sourceFileName,
    const bool useSoftShadowVariant,
    const uint32_t environmentProjectionVariant,
    std::vector<ShaderVariantDefine> featureDefines)
{
    ShaderVariantDesc shaderDesc;
    shaderDesc.CompiledFileName = useSoftShadowVariant
        ? compiledFileName + L".softshadow"
        : compiledFileName;
    shaderDesc.SourceFileName = sourceFileName;
    shaderDesc.TargetProfile = ShaderTargetProfile::Compute();
    shaderDesc.DebugName = "Framework ReSTIR DI";
    if (useSoftShadowVariant)
    {
        shaderDesc.Defines = m_ShaderSources.SoftShadowDefines;
    }
//Modify Begin:2026-08-06 by BestHui
    if (environmentProjectionVariant != 0u)
    {
        Assert(
            !m_ShaderSources.EnvironmentProjectionDefineName.empty(),
            "ReSTIR DI environment projection variants require a define name.");
        shaderDesc.CompiledFileName += L".environment" + std::to_wstring(environmentProjectionVariant);
        shaderDesc.Defines.push_back({
            m_ShaderSources.EnvironmentProjectionDefineName,
            std::to_string(environmentProjectionVariant)
        });
    }
//Modify End
    shaderDesc.Defines.insert(
        shaderDesc.Defines.end(),
        std::make_move_iterator(featureDefines.begin()),
        std::make_move_iterator(featureDefines.end()));

    const std::shared_ptr<ShaderBlob> shaderBlob = m_ShaderVariants.GetOrCompile(shaderDesc);
//Modify Begin:2026-08-12 by BestHui
    ComputePipelineDescBuilder pipelineDescBuilder =
        ComputePipelineDescBuilder::ReflectedDefault(*shaderBlob)
            .WithDirectlyIndexedResourceHeap();
    for (const PipelineStaticSamplerContract& contract : m_ShaderSources.StaticSamplerContracts)
    {
        pipelineDescBuilder.WithStaticSamplerContract(contract);
    }
    return std::make_unique<ComputeShader>(
        m_DeviceContext,
        *shaderBlob,
        pipelineDescBuilder.Build());
//Modify End
}
//Modify End
