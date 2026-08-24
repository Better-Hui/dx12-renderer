//Modify Begin:2026-08-24 by Hui
#pragma once

#include <Framework/Rendering/Lighting/ActivePixelList.h>
#include <Framework/Rendering/Lighting/ReSTIRGI.h>
#include <Framework/Rendering/Lighting/MaterialShadingModel.h>
#include <Framework/Rendering/Pipeline/PipelineLayout.h>
#include <Framework/Rendering/Pipeline/ShaderVariant.h>
#include <RenderGraph/RenderGraphBuilder.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class CommandList;
class CommandContext;
class ComputeShader;
class FrameworkDeviceContext;
class Texture;

struct ReSTIRGIFrameState
{
    bool Enabled = false;
    bool UseSoftShadowVariant = false;
    MaterialShadingModel ShadingModel = MaterialShadingModel::Pbr;
    uint32_t EnvironmentProjectionVariant = 0u;
    ReSTIRGIVariantConfig VariantConfig = {};
    ReSTIRGIFrameConstants Constants = {};
};

struct ReSTIRGIExecutionInputs
{
    ReSTIRGIFrameState FrameState;
    std::shared_ptr<Texture> IndirectLighting;
    std::shared_ptr<Texture> MotionVector;
    ActivePixelDispatch CompactedDispatch = {};
    std::function<void(CommandContext&)> PrepareCommandContext;
    std::function<void(CommandContext&, ComputeShader&)> BindSceneInputs;
    bool EnableStageTiming = false;
    std::function<void(CommandList&, const char*)> WriteTimestamp;
};

struct ReSTIRGIShaderSources
{
    std::wstring Initial;
    std::wstring Temporal;
    std::wstring Spatial;
    std::wstring Shade;
    std::vector<ShaderVariantDefine> SoftShadowDefines;
    std::string EnvironmentProjectionDefineName;
    std::vector<PipelineStaticSamplerContract> StaticSamplerContracts;
};

struct ReSTIRGIGraphInputs
{
    RenderGraph::ResourceId IndirectLighting = 0;
    RenderGraph::ResourceId InputToken = 0;
    RenderGraph::ResourceId OutputToken = 0;
    uint32_t Width = 1u;
    uint32_t Height = 1u;
    bool UseCompactedDispatch = false;
    std::function<uint32_t()> GetFrameIndex;
    std::function<ReSTIRGIVariantConfig()> ResolveVariantConfig;
    std::function<void(RenderGraph::RenderGraphPassBuilder&)> DeclareSharedResources;
    std::function<ReSTIRGIExecutionInputs(const RenderGraph::RenderContext&)> ResolveFrameInputs;
};

class ReSTIRGIPass final
{
public:
    ReSTIRGIPass(FrameworkDeviceContext& deviceContext, ReSTIRGIShaderSources shaderSources);
    ~ReSTIRGIPass();

    ReSTIRGIPass(const ReSTIRGIPass&) = delete;
    ReSTIRGIPass& operator=(const ReSTIRGIPass&) = delete;

    void EnsurePipelines(
        bool useSoftShadowVariant,
        uint32_t environmentProjectionVariant,
        const ReSTIRGIVariantConfig& variantConfig,
        MaterialShadingModel shadingModel,
        bool useCompactedDispatch);
    void AddPasses(RenderGraph::RenderGraphBuilder& builder, ReSTIRGIGraphInputs inputs);

private:
    enum class ReSTIRGIStage : uint8_t
    {
        Initial,
        Temporal,
        Spatial,
        Shade,
    };

    struct PipelineSet;
    struct InternalResources;

    static uint32_t GetPipelineVariantIndex(
        bool useSoftShadowVariant,
        uint32_t environmentProjectionVariant);
    uint32_t GetStageVariantKey(
        ReSTIRGIStage stage,
        const ReSTIRGIVariantConfig& variantConfig,
        MaterialShadingModel shadingModel,
        bool useCompactedDispatch) const;
    std::vector<ShaderVariantDefine> GetStageVariantDefines(
        ReSTIRGIStage stage,
        const ReSTIRGIVariantConfig& variantConfig,
        MaterialShadingModel shadingModel,
        bool useCompactedDispatch) const;
    PipelineSet& GetPipelines(
        bool useSoftShadowVariant,
        uint32_t environmentProjectionVariant);
    ComputeShader& GetStageShader(
        PipelineSet& pipelines,
        ReSTIRGIStage stage,
        const ReSTIRGIVariantConfig& variantConfig,
        MaterialShadingModel shadingModel,
        bool useCompactedDispatch);
    bool EnsureResources(uint32_t width, uint32_t height);
    void ExecuteInitialSampling(CommandContext& commandContext, const ReSTIRGIExecutionInputs& inputs, PipelineSet& pipelines);
    void ExecuteTemporalResampling(CommandContext& commandContext, const ReSTIRGIExecutionInputs& inputs, PipelineSet& pipelines);
    void ExecuteSpatialResampling(
        CommandContext& commandContext,
        const ReSTIRGIExecutionInputs& inputs,
        PipelineSet& pipelines,
        const std::shared_ptr<Texture>& sourceCreation,
        const std::shared_ptr<Texture>& sourceHit,
        const std::shared_ptr<Texture>& sourceLight);
    void ExecuteFinalShading(
        CommandContext& commandContext,
        const ReSTIRGIExecutionInputs& inputs,
        PipelineSet& pipelines,
        const std::shared_ptr<Texture>& reservoirCreation,
        const std::shared_ptr<Texture>& reservoirHit,
        const std::shared_ptr<Texture>& reservoirLight);
    std::unique_ptr<ComputeShader> CreateComputeShader(
        const std::wstring& compiledFileName,
        const std::wstring& sourceFileName,
        bool useSoftShadowVariant,
        uint32_t environmentProjectionVariant,
        std::vector<ShaderVariantDefine> featureDefines);

    FrameworkDeviceContext& m_DeviceContext;
    ReSTIRGIShaderSources m_ShaderSources;
    ShaderVariantManager m_ShaderVariants;
    std::unordered_map<uint32_t, std::unique_ptr<PipelineSet>> m_Pipelines;
    std::unique_ptr<InternalResources> m_Resources;
    uint32_t m_ResourceWidth = 0;
    uint32_t m_ResourceHeight = 0;
    bool m_HistoryValid = false;
};
//Modify End
