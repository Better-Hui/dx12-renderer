#pragma once

//Modify Begin:2026-08-24 by Hui
#include <Framework/Rendering/Lighting/ActivePixelList.h>
#include <Framework/Rendering/Lighting/ReSTIRDI.h>
#include <Framework/Rendering/Lighting/MaterialShadingModel.h>
#include <Framework/Rendering/Pipeline/PipelineLayout.h>
#include <Framework/Rendering/Pipeline/ShaderVariant.h>
#include <RenderGraph/RenderGraphBuilder.h>

#include <array>
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

struct ReSTIRDIFrameState
{
    bool Enabled = false;
    bool UseSoftShadowVariant = false;
    MaterialShadingModel ShadingModel = MaterialShadingModel::Pbr;
    uint32_t EnvironmentProjectionVariant = 0u;
    uint32_t Width = 1;
    uint32_t Height = 1;
    uint32_t FrameIndex = 0;
    ReSTIRDIFrameConstants Constants = {};
};

struct ReSTIRDIExecutionInputs
{
    ReSTIRDIFrameState FrameState;
    std::shared_ptr<Texture> DirectLighting;
    std::shared_ptr<Texture> MotionVector;
    ActivePixelDispatch CompactedDispatch = {};
    std::function<void(CommandContext&)> PrepareCommandContext;
    std::function<void(CommandContext&, ComputeShader&)> BindSceneInputs;
};

struct ReSTIRDIShaderSources
{
    std::wstring RIS;
    std::wstring Temporal;
    std::wstring Spatial;
    std::wstring Shade;
    std::vector<ShaderVariantDefine> SoftShadowDefines;
    std::string EnvironmentProjectionDefineName;
    std::vector<PipelineStaticSamplerContract> StaticSamplerContracts;
};

struct ReSTIRDIGraphInputs
{
    RenderGraph::ResourceId DirectLighting = 0;
    RenderGraph::ResourceId InputToken = 0;
    RenderGraph::ResourceId OutputToken = 0;
    uint32_t Width = 1u;
    uint32_t Height = 1u;
    bool UseCompactedDispatch = false;
    bool EnableTemporalResampling = false;
    bool EnableBoilingFilter = false;
    bool EnableSpatialResampling = false;
    std::function<uint32_t()> GetFrameIndex;
    std::function<void(RenderGraph::RenderGraphPassBuilder&)> DeclareSharedResources;
    std::function<ReSTIRDIExecutionInputs(const RenderGraph::RenderContext&)> ResolveFrameInputs;
};

class ReSTIRDIPass final
{
public:
    ReSTIRDIPass(FrameworkDeviceContext& deviceContext, ReSTIRDIShaderSources shaderSources);
    ~ReSTIRDIPass();

    ReSTIRDIPass(const ReSTIRDIPass&) = delete;
    ReSTIRDIPass& operator=(const ReSTIRDIPass&) = delete;

    void EnsurePipelines(
        bool useSoftShadowVariant,
        uint32_t environmentProjectionVariant,
        const ReSTIRDIFrameConstants& constants,
        MaterialShadingModel shadingModel,
        bool useCompactedDispatch);
    void AddPasses(
        RenderGraph::RenderGraphBuilder& builder,
        ReSTIRDIGraphInputs inputs);

private:
    enum class ReSTIRDIStage : uint8_t
    {
        RIS,
        Temporal,
        BoilingFilter,
        Spatial,
        Shade,
    };

    struct PipelineSet;
    struct InternalResources;

    static constexpr uint32_t EnvironmentProjectionVariantCount = 3u;
    static constexpr uint32_t PipelineVariantCount = EnvironmentProjectionVariantCount * 2u;

    static size_t GetPipelineVariantIndex(bool useSoftShadowVariant, uint32_t environmentProjectionVariant);
    static uint32_t GetStageVariantKey(
        ReSTIRDIStage stage,
        const ReSTIRDIFrameConstants& constants,
        MaterialShadingModel shadingModel,
        bool useCompactedDispatch);
    static std::vector<ShaderVariantDefine> GetStageVariantDefines(
        ReSTIRDIStage stage,
        const ReSTIRDIFrameConstants& constants,
        MaterialShadingModel shadingModel,
        bool useCompactedDispatch);
    PipelineSet& GetPipelines(bool useSoftShadowVariant, uint32_t environmentProjectionVariant);
    ComputeShader& GetStageShader(
        PipelineSet& pipelines,
        ReSTIRDIStage stage,
        const ReSTIRDIFrameConstants& constants,
        MaterialShadingModel shadingModel,
        bool useCompactedDispatch);
    void EnsureResources(uint32_t width, uint32_t height);
    void ExecuteInitialSampling(CommandContext& commandContext, const ReSTIRDIExecutionInputs& inputs, PipelineSet& pipelines);
    void ExecuteTemporalResampling(CommandContext& commandContext, const ReSTIRDIExecutionInputs& inputs, PipelineSet& pipelines);
    void ExecuteBoilingFilter(CommandContext& commandContext, const ReSTIRDIExecutionInputs& inputs, PipelineSet& pipelines);
    void ExecuteSpatialResampling(
        CommandContext& commandContext,
        const ReSTIRDIExecutionInputs& inputs,
        PipelineSet& pipelines,
        const std::shared_ptr<Texture>& inputReservoir,
        const std::shared_ptr<Texture>& inputReservoirState);
    void ExecuteFinalShading(
        CommandContext& commandContext,
        const ReSTIRDIExecutionInputs& inputs,
        PipelineSet& pipelines,
        const std::shared_ptr<Texture>& finalReservoir,
        const std::shared_ptr<Texture>& finalReservoirState);
    std::unique_ptr<ComputeShader> CreateComputeShader(
        const std::wstring& compiledFileName,
        const std::wstring& sourceFileName,
        bool useSoftShadowVariant,
        uint32_t environmentProjectionVariant,
        std::vector<ShaderVariantDefine> featureDefines);

    FrameworkDeviceContext& m_DeviceContext;
    ReSTIRDIShaderSources m_ShaderSources;
    ShaderVariantManager m_ShaderVariants;
    std::array<std::unique_ptr<PipelineSet>, PipelineVariantCount> m_Pipelines;
    std::unique_ptr<InternalResources> m_Resources;
    uint32_t m_ResourceWidth = 0;
    uint32_t m_ResourceHeight = 0;
};
//Modify End
