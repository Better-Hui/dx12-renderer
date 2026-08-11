#include <Framework/Rendering/Lighting/ReSTIRGIPass.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/Texture.h>
#include <Framework/Core/FrameworkDeviceContext.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/Pipeline/ComputePipelineStateBuilder.h>
#include <Framework/Rendering/Pipeline/ComputeShader.h>
#include <Framework/Rendering/Pipeline/ShaderTargetProfile.h>
#include <Framework/Rendering/Texture/RenderTexture.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>
#include <Framework/Rendering/Texture/UnorderedAccessView.h>

#include <algorithm>
#include <utility>

//Modify Begin:2026-08-10 by BestHui
struct ReSTIRGIPass::PipelineSet
{
    bool UseSoftShadowVariant = false;
    uint32_t EnvironmentProjectionVariant = 0u;
    std::unordered_map<uint32_t, std::unique_ptr<ComputeShader>> InitialVariants;
    std::unordered_map<uint32_t, std::unique_ptr<ComputeShader>> TemporalVariants;
    std::unordered_map<uint32_t, std::unique_ptr<ComputeShader>> SpatialVariants;
    std::unordered_map<uint32_t, std::unique_ptr<ComputeShader>> ShadeVariants;
};

struct ReSTIRGIPass::InternalResources
{
    struct ReservoirSet
    {
        std::shared_ptr<Texture> Creation;
        std::shared_ptr<Texture> Hit;
        std::shared_ptr<Texture> Light;
    };

//Modify Begin:2026-07-30 by BestHui
    struct HistorySet
    {
        ReservoirSet Temporal;
        ReservoirSet Spatial;
    };
//Modify End

    ReservoirSet Initial;
//Modify Begin:2026-07-30 by BestHui
    std::array<HistorySet, 2> History;
//Modify End
};

namespace
{
    constexpr DXGI_FORMAT ReservoirFormat = DXGI_FORMAT_R32G32B32A32_UINT;

//Modify Begin:2026-08-11 by BestHui
    void WriteStageTimestamp(
        CommandList& commandList,
        const ReSTIRGIExecutionInputs& inputs,
        const char* markerName)
    {
        if (inputs.EnableStageTiming && inputs.WriteTimestamp)
        {
            inputs.WriteTimestamp(commandList, markerName);
        }
    }
//Modify End

    void InsertUavBarrier(
        CommandList& commandList,
        const std::shared_ptr<Texture>& creation,
        const std::shared_ptr<Texture>& hit,
        const std::shared_ptr<Texture>& light)
    {
        commandList.UavBarrier(*creation);
        commandList.UavBarrier(*hit);
        commandList.UavBarrier(*light);
    }
}

ReSTIRGIPass::ReSTIRGIPass(
    FrameworkDeviceContext& deviceContext,
    ReSTIRGIShaderSources shaderSources)
    : m_DeviceContext(deviceContext)
    , m_ShaderSources(std::move(shaderSources))
{
}

ReSTIRGIPass::~ReSTIRGIPass() = default;

void ReSTIRGIPass::EnsurePipelines(
    const bool useSoftShadowVariant,
    const uint32_t environmentProjectionVariant,
    const ReSTIRGIFrameConstants& constants,
    const uint32_t maxPathBounces)
{
//Modify Begin:2026-08-11 by BestHui
    m_MaxPathBounces = std::clamp(maxPathBounces, 1u, 5u);
//Modify End
    PipelineSet& pipelines = GetPipelines(useSoftShadowVariant, environmentProjectionVariant);
    static_cast<void>(GetStageShader(pipelines, ReSTIRGIStage::Initial, constants));
    static_cast<void>(GetStageShader(pipelines, ReSTIRGIStage::Temporal, constants));
    static_cast<void>(GetStageShader(pipelines, ReSTIRGIStage::Spatial, constants));
    static_cast<void>(GetStageShader(pipelines, ReSTIRGIStage::Shade, constants));
}

void ReSTIRGIPass::Execute(
    CommandList& commandList,
    const ReSTIRGIExecutionInputs& inputs)
{
    if (!inputs.FrameState.Enabled)
    {
        return;
    }

    Assert(static_cast<bool>(inputs.BindSceneInputs), "ReSTIR GI requires scene input bindings.");
    Assert(inputs.IndirectLighting != nullptr, "ReSTIR GI requires an indirect lighting output.");
    Assert(inputs.MotionVector != nullptr, "ReSTIR GI requires motion vectors.");

    PipelineSet& pipelines = GetPipelines(
        inputs.FrameState.UseSoftShadowVariant,
        inputs.FrameState.EnvironmentProjectionVariant);
//Modify Begin:2026-07-30 by BestHui
    const bool resourcesRecreated = EnsureResources(
        inputs.FrameState.Constants.Width,
        inputs.FrameState.Constants.Height);
    ReSTIRGIExecutionInputs executionInputs = inputs;
    executionInputs.FrameState.Constants.HistoryValid =
        inputs.FrameState.Constants.HistoryValid != 0u && m_HistoryValid && !resourcesRecreated ? 1u : 0u;
//Modify End

//Modify Begin:2026-07-30 by BestHui
    const uint32_t historyWriteIndex = 1u - m_HistoryReadIndex;
    const bool temporalResamplingEnabled = executionInputs.FrameState.Constants.TemporalResamplingEnabled != 0u;
    const bool spatialResamplingEnabled = executionInputs.FrameState.Constants.SpatialResamplingEnabled != 0u;
    const bool requiresTemporalOutput = temporalResamplingEnabled || spatialResamplingEnabled;
//Modify End

//Modify Begin:2026-08-11 by BestHui
    CommandContext commandContext(commandList);
    if (executionInputs.PrepareCommandContext)
    {
        executionInputs.PrepareCommandContext(commandContext);
    }
//Modify End
    WriteStageTimestamp(commandList, executionInputs, "ReSTIR GI.Begin");
    ExecuteInitialSampling(commandContext, executionInputs, pipelines);
    InsertUavBarrier(
        commandList,
        m_Resources->Initial.Creation,
        m_Resources->Initial.Hit,
        m_Resources->Initial.Light);
    WriteStageTimestamp(commandList, executionInputs, "ReSTIR GI.Initial");

//Modify Begin:2026-08-11 by BestHui
    const InternalResources::ReservoirSet* finalReservoir = &m_Resources->Initial;
    if (requiresTemporalOutput)
    {
        ExecuteTemporalResampling(commandContext, executionInputs, pipelines);
        InsertUavBarrier(
            commandList,
            m_Resources->History[historyWriteIndex].Temporal.Creation,
            m_Resources->History[historyWriteIndex].Temporal.Hit,
            m_Resources->History[historyWriteIndex].Temporal.Light);
        WriteStageTimestamp(commandList, executionInputs, "ReSTIR GI.Temporal");
        finalReservoir = &m_Resources->History[historyWriteIndex].Temporal;
    }

    if (spatialResamplingEnabled)
    {
        ExecuteSpatialResampling(commandContext, executionInputs, pipelines);
        InsertUavBarrier(
            commandList,
            m_Resources->History[historyWriteIndex].Spatial.Creation,
            m_Resources->History[historyWriteIndex].Spatial.Hit,
            m_Resources->History[historyWriteIndex].Spatial.Light);
        WriteStageTimestamp(commandList, executionInputs, "ReSTIR GI.Spatial");
        finalReservoir = &m_Resources->History[historyWriteIndex].Spatial;
    }

    ExecuteFinalShading(
        commandContext,
        executionInputs,
        pipelines,
        finalReservoir->Creation,
        finalReservoir->Hit,
        finalReservoir->Light);
    WriteStageTimestamp(commandList, executionInputs, "ReSTIR GI.Shade");
//Modify End
    commandList.UavBarrier(*executionInputs.IndirectLighting);
//Modify Begin:2026-08-11 by BestHui
    if (requiresTemporalOutput)
    {
        m_HistoryReadIndex = historyWriteIndex;
    }
    m_HistoryValid = temporalResamplingEnabled;
//Modify End
}

//Modify Begin:2026-07-30 by BestHui
bool ReSTIRGIPass::EnsureResources(const uint32_t width, const uint32_t height)
//Modify End
{
    if (m_Resources != nullptr && m_ResourceWidth == width && m_ResourceHeight == height)
    {
        return false;
    }

    const auto createReservoirSet = [width, height](const wchar_t* prefix)
    {
        InternalResources::ReservoirSet reservoir;
        const std::wstring prefixString(prefix);
        reservoir.Creation = RenderTexture::CreateUav2D(
            ReservoirFormat,
            width,
            height,
            prefixString + L" Creation");
        reservoir.Hit = RenderTexture::CreateUav2D(
            ReservoirFormat,
            width,
            height,
            prefixString + L" Hit");
        reservoir.Light = RenderTexture::CreateUav2D(
            ReservoirFormat,
            width,
            height,
            prefixString + L" Light");
        return reservoir;
    };

    m_Resources = std::make_unique<InternalResources>();
    m_Resources->Initial = createReservoirSet(L"ReSTIR GI Initial");
//Modify Begin:2026-07-30 by BestHui
    m_Resources->History[0].Temporal = createReservoirSet(L"ReSTIR GI History 0 Temporal");
    m_Resources->History[0].Spatial = createReservoirSet(L"ReSTIR GI History 0 Spatial");
    m_Resources->History[1].Temporal = createReservoirSet(L"ReSTIR GI History 1 Temporal");
    m_Resources->History[1].Spatial = createReservoirSet(L"ReSTIR GI History 1 Spatial");
//Modify End
    m_ResourceWidth = width;
    m_ResourceHeight = height;
    m_HistoryReadIndex = 0;
//Modify Begin:2026-07-30 by BestHui
    m_HistoryValid = false;
    return true;
//Modify End
}

void ReSTIRGIPass::ExecuteInitialSampling(
//Modify Begin:2026-08-11 by BestHui
    CommandContext& commandContext,
//Modify End
    const ReSTIRGIExecutionInputs& inputs,
    PipelineSet& pipelines)
{
    ComputeShader& shader = GetStageShader(
        pipelines,
        ReSTIRGIStage::Initial,
        inputs.FrameState.Constants);
//Modify Begin:2026-07-30 by BestHui
    inputs.BindSceneInputs(commandContext, shader);
//Modify End
    commandContext.SetConstantBuffer(shader, "ReSTIRGIConstants", sizeof(inputs.FrameState.Constants), &inputs.FrameState.Constants);
    commandContext.SetUnorderedAccessView(shader, "ReSTIRGIInitialCreation", UnorderedAccessView(m_Resources->Initial.Creation));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRGIInitialHit", UnorderedAccessView(m_Resources->Initial.Hit));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRGIInitialLight", UnorderedAccessView(m_Resources->Initial.Light));
    commandContext.BindPipeline(shader);
    commandContext.BindDescriptorSet(shader.GetDescriptorSet());
    commandContext.Dispatch(
        Math::DivideByMultiple(inputs.FrameState.Constants.Width, 8u),
        Math::DivideByMultiple(inputs.FrameState.Constants.Height, 8u),
        1u);
}

void ReSTIRGIPass::ExecuteTemporalResampling(
//Modify Begin:2026-08-11 by BestHui
    CommandContext& commandContext,
//Modify End
    const ReSTIRGIExecutionInputs& inputs,
    PipelineSet& pipelines)
{
    ComputeShader& shader = GetStageShader(
        pipelines,
        ReSTIRGIStage::Temporal,
        inputs.FrameState.Constants);
//Modify Begin:2026-07-30 by BestHui
    inputs.BindSceneInputs(commandContext, shader);
//Modify End
    commandContext.SetConstantBuffer(shader, "ReSTIRGIConstants", sizeof(inputs.FrameState.Constants), &inputs.FrameState.Constants);
    commandContext.SetShaderResourceView(shader, "ReSTIRGIInitialCreation", ShaderResourceView(m_Resources->Initial.Creation));
    commandContext.SetShaderResourceView(shader, "ReSTIRGIInitialHit", ShaderResourceView(m_Resources->Initial.Hit));
    commandContext.SetShaderResourceView(shader, "ReSTIRGIInitialLight", ShaderResourceView(m_Resources->Initial.Light));
    commandContext.SetShaderResourceView(shader, "MotionVectorTexture", ShaderResourceView(inputs.MotionVector));
//Modify Begin:2026-07-30 by BestHui
//Modify Begin:2026-08-11 by BestHui
    const InternalResources::ReservoirSet& historyRead =
        inputs.FrameState.Constants.SpatialResamplingEnabled != 0u
        ? m_Resources->History[m_HistoryReadIndex].Spatial
        : m_Resources->History[m_HistoryReadIndex].Temporal;
//Modify End
    const InternalResources::ReservoirSet& historyWrite = m_Resources->History[1u - m_HistoryReadIndex].Temporal;
    commandContext.SetShaderResourceView(shader, "ReSTIRGIHistoryCreation", ShaderResourceView(historyRead.Creation));
    commandContext.SetShaderResourceView(shader, "ReSTIRGIHistoryHit", ShaderResourceView(historyRead.Hit));
    commandContext.SetShaderResourceView(shader, "ReSTIRGIHistoryLight", ShaderResourceView(historyRead.Light));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRGITemporalCreation", UnorderedAccessView(historyWrite.Creation));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRGITemporalHit", UnorderedAccessView(historyWrite.Hit));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRGITemporalLight", UnorderedAccessView(historyWrite.Light));
//Modify End
    commandContext.BindPipeline(shader);
    commandContext.BindDescriptorSet(shader.GetDescriptorSet());
    commandContext.Dispatch(
        Math::DivideByMultiple(inputs.FrameState.Constants.Width, 8u),
        Math::DivideByMultiple(inputs.FrameState.Constants.Height, 8u),
        1u);
}

void ReSTIRGIPass::ExecuteSpatialResampling(
//Modify Begin:2026-08-11 by BestHui
    CommandContext& commandContext,
//Modify End
    const ReSTIRGIExecutionInputs& inputs,
    PipelineSet& pipelines)
{
    ComputeShader& shader = GetStageShader(
        pipelines,
        ReSTIRGIStage::Spatial,
        inputs.FrameState.Constants);
//Modify Begin:2026-07-30 by BestHui
    inputs.BindSceneInputs(commandContext, shader);
//Modify End
    commandContext.SetConstantBuffer(shader, "ReSTIRGIConstants", sizeof(inputs.FrameState.Constants), &inputs.FrameState.Constants);
//Modify Begin:2026-07-30 by BestHui
    const InternalResources::ReservoirSet& currentTemporal = m_Resources->History[1u - m_HistoryReadIndex].Temporal;
    const InternalResources::ReservoirSet& spatialOutput = m_Resources->History[1u - m_HistoryReadIndex].Spatial;
    commandContext.SetShaderResourceView(shader, "ReSTIRGITemporalCreation", ShaderResourceView(currentTemporal.Creation));
    commandContext.SetShaderResourceView(shader, "ReSTIRGITemporalHit", ShaderResourceView(currentTemporal.Hit));
    commandContext.SetShaderResourceView(shader, "ReSTIRGITemporalLight", ShaderResourceView(currentTemporal.Light));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRGIHistoryCreation", UnorderedAccessView(spatialOutput.Creation));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRGIHistoryHit", UnorderedAccessView(spatialOutput.Hit));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRGIHistoryLight", UnorderedAccessView(spatialOutput.Light));
//Modify End
    commandContext.BindPipeline(shader);
    commandContext.BindDescriptorSet(shader.GetDescriptorSet());
    commandContext.Dispatch(
        Math::DivideByMultiple(inputs.FrameState.Constants.Width, 8u),
        Math::DivideByMultiple(inputs.FrameState.Constants.Height, 8u),
        1u);
}

void ReSTIRGIPass::ExecuteFinalShading(
//Modify Begin:2026-08-11 by BestHui
    CommandContext& commandContext,
//Modify End
    const ReSTIRGIExecutionInputs& inputs,
    PipelineSet& pipelines,
    const std::shared_ptr<Texture>& reservoirCreation,
    const std::shared_ptr<Texture>& reservoirHit,
    const std::shared_ptr<Texture>& reservoirLight)
{
    ComputeShader& shader = GetStageShader(
        pipelines,
        ReSTIRGIStage::Shade,
        inputs.FrameState.Constants);
//Modify Begin:2026-07-30 by BestHui
    inputs.BindSceneInputs(commandContext, shader);
//Modify End
    commandContext.SetConstantBuffer(shader, "ReSTIRGIConstants", sizeof(inputs.FrameState.Constants), &inputs.FrameState.Constants);
//Modify Begin:2026-08-11 by BestHui
    commandContext.SetShaderResourceView(shader, "ReSTIRGIHistoryCreation", ShaderResourceView(reservoirCreation));
    commandContext.SetShaderResourceView(shader, "ReSTIRGIHistoryHit", ShaderResourceView(reservoirHit));
    commandContext.SetShaderResourceView(shader, "ReSTIRGIHistoryLight", ShaderResourceView(reservoirLight));
//Modify End
    commandContext.SetUnorderedAccessView(shader, "IndirectLighting", UnorderedAccessView(inputs.IndirectLighting));
    commandContext.BindPipeline(shader);
    commandContext.BindDescriptorSet(shader.GetDescriptorSet());
    commandContext.Dispatch(
        Math::DivideByMultiple(inputs.FrameState.Constants.Width, 8u),
        Math::DivideByMultiple(inputs.FrameState.Constants.Height, 8u),
        1u);
}

uint32_t ReSTIRGIPass::GetPipelineVariantIndex(
    const bool useSoftShadowVariant,
    const uint32_t environmentProjectionVariant)
{
    Assert(
        environmentProjectionVariant < 3u,
        "Unsupported ReSTIR GI environment projection variant.");
    return environmentProjectionVariant |
        (useSoftShadowVariant ? 1u : 0u) << 2u;
}

ReSTIRGIPass::PipelineSet& ReSTIRGIPass::GetPipelines(
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

    return *pipelines;
}

uint32_t ReSTIRGIPass::GetStageVariantKey(
    const ReSTIRGIStage stage,
    const ReSTIRGIFrameConstants& constants) const
{
    switch (stage)
    {
    case ReSTIRGIStage::Initial:
        return m_MaxPathBounces;

    case ReSTIRGIStage::Shade:
        return 0u;

    case ReSTIRGIStage::Temporal:
        if (constants.TemporalResamplingEnabled == 0u)
        {
            return 0u;
        }
        return 1u | ((constants.TemporalJacobianEnabled != 0u ? 1u : 0u) << 1u);

    case ReSTIRGIStage::Spatial:
        return constants.SpatialUnbiasedResamplingEnabled != 0u ? 1u : 0u;
    }

    Assert(false, "Unsupported ReSTIR GI stage.");
    return 0u;
}

std::vector<ShaderVariantDefine> ReSTIRGIPass::GetStageVariantDefines(
    const ReSTIRGIStage stage,
    const ReSTIRGIFrameConstants& constants) const
{
    const auto booleanDefine = [](const char* name, const bool value)
    {
        return ShaderVariantDefine { name, value ? "1" : "0" };
    };

    switch (stage)
    {
    case ReSTIRGIStage::Initial:
        return {
            { "RESTIR_GI_MAX_PATH_BOUNCES", std::to_string(m_MaxPathBounces) },
        };

    case ReSTIRGIStage::Shade:
        return {};

    case ReSTIRGIStage::Temporal:
        return {
            booleanDefine("RESTIR_GI_USE_TEMPORAL_REUSE", constants.TemporalResamplingEnabled != 0u),
            booleanDefine(
                "RESTIR_GI_USE_TEMPORAL_JACOBIAN",
                constants.TemporalResamplingEnabled != 0u && constants.TemporalJacobianEnabled != 0u),
        };

    case ReSTIRGIStage::Spatial:
        return {
            booleanDefine("RESTIR_GI_USE_UNBIASED_SPATIAL_REUSE", constants.SpatialUnbiasedResamplingEnabled != 0u),
        };
    }

    Assert(false, "Unsupported ReSTIR GI stage.");
    return {};
}

ComputeShader& ReSTIRGIPass::GetStageShader(
    PipelineSet& pipelines,
    const ReSTIRGIStage stage,
    const ReSTIRGIFrameConstants& constants)
{
    std::unordered_map<uint32_t, std::unique_ptr<ComputeShader>>* stageVariants = nullptr;
    const std::wstring* sourceFileName = nullptr;
    std::wstring compiledFileName;
    switch (stage)
    {
    case ReSTIRGIStage::Initial:
        stageVariants = &pipelines.InitialVariants;
        sourceFileName = &m_ShaderSources.Initial;
        compiledFileName = L"ReSTIRGI.Initial.cs.cso";
        break;
    case ReSTIRGIStage::Temporal:
        stageVariants = &pipelines.TemporalVariants;
        sourceFileName = &m_ShaderSources.Temporal;
        compiledFileName = L"ReSTIRGI.Temporal.cs.cso";
        break;
    case ReSTIRGIStage::Spatial:
        stageVariants = &pipelines.SpatialVariants;
        sourceFileName = &m_ShaderSources.Spatial;
        compiledFileName = L"ReSTIRGI.Spatial.cs.cso";
        break;
    case ReSTIRGIStage::Shade:
        stageVariants = &pipelines.ShadeVariants;
        sourceFileName = &m_ShaderSources.Shade;
        compiledFileName = L"ReSTIRGI.Shade.cs.cso";
        break;
    }

    Assert(stageVariants != nullptr && sourceFileName != nullptr, "Unsupported ReSTIR GI stage.");
    const uint32_t variantKey = GetStageVariantKey(stage, constants);
    auto [shaderIt, inserted] = stageVariants->try_emplace(variantKey);
    if (inserted)
    {
        shaderIt->second = CreateComputeShader(
            compiledFileName + L".variant" + std::to_wstring(variantKey),
            *sourceFileName,
            pipelines.UseSoftShadowVariant,
            pipelines.EnvironmentProjectionVariant,
            GetStageVariantDefines(stage, constants));
    }

    Assert(shaderIt->second != nullptr, "ReSTIR GI stage shader creation failed.");
    return *shaderIt->second;
}

std::unique_ptr<ComputeShader> ReSTIRGIPass::CreateComputeShader(
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
    shaderDesc.DebugName = "Framework ReSTIR GI";
    if (useSoftShadowVariant)
    {
        shaderDesc.Defines = m_ShaderSources.SoftShadowDefines;
    }
    if (environmentProjectionVariant != 0u)
    {
        Assert(
            !m_ShaderSources.EnvironmentProjectionDefineName.empty(),
            "ReSTIR GI environment projection variants require a define name.");
        shaderDesc.CompiledFileName += L".environment" + std::to_wstring(environmentProjectionVariant);
        shaderDesc.Defines.push_back({
            m_ShaderSources.EnvironmentProjectionDefineName,
            std::to_string(environmentProjectionVariant)
        });
    }
    shaderDesc.Defines.insert(
        shaderDesc.Defines.end(),
        std::make_move_iterator(featureDefines.begin()),
        std::make_move_iterator(featureDefines.end()));

    const std::shared_ptr<ShaderBlob> shaderBlob = m_ShaderVariants.GetOrCompile(shaderDesc);
    return std::make_unique<ComputeShader>(
        m_DeviceContext,
        *shaderBlob,
        ComputePipelineDescBuilder::ReflectedDefault(*shaderBlob)
            .WithDirectlyIndexedResourceHeap()
            .Build());
}
//Modify End
