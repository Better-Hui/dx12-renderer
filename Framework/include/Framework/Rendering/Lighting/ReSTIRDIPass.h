#pragma once

//Modify Begin:2026-07-30 by BestHui
#include <Framework/Rendering/Lighting/ReSTIRDI.h>
#include <Framework/Rendering/Lighting/MaterialShadingModel.h>
#include <Framework/Rendering/Pipeline/PipelineLayout.h>
#include <Framework/Rendering/Pipeline/ShaderVariant.h>

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
//Modify Begin:2026-07-30 by BestHui
    MaterialShadingModel ShadingModel = MaterialShadingModel::Pbr;
//Modify End
//Modify Begin:2026-08-06 by BestHui
    uint32_t EnvironmentProjectionVariant = 0u;
//Modify End
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
//Modify Begin:2026-08-06 by BestHui
    std::string EnvironmentProjectionDefineName;
//Modify End
//Modify Begin:2026-08-12 by BestHui
    std::vector<PipelineStaticSamplerContract> StaticSamplerContracts;
//Modify End
};

class ReSTIRDIPass final
{
public:
    ReSTIRDIPass(FrameworkDeviceContext& deviceContext, ReSTIRDIShaderSources shaderSources);
    ~ReSTIRDIPass();

    ReSTIRDIPass(const ReSTIRDIPass&) = delete;
    ReSTIRDIPass& operator=(const ReSTIRDIPass&) = delete;

//Modify Begin:2026-08-06 by BestHui
    void EnsurePipelines(
        bool useSoftShadowVariant,
        uint32_t environmentProjectionVariant,
        const ReSTIRDIFrameConstants& constants,
        MaterialShadingModel shadingModel);
//Modify End
    void Execute(
        CommandList& commandList,
        const ReSTIRDIExecutionInputs& inputs);

private:
    enum class ReSTIRDIStage : uint8_t
    {
        RIS,
        Temporal,
        Spatial,
        Shade,
    };

    struct PipelineSet;
    struct InternalResources;

//Modify Begin:2026-08-06 by BestHui
    static constexpr uint32_t EnvironmentProjectionVariantCount = 3u;
    static constexpr uint32_t PipelineVariantCount = EnvironmentProjectionVariantCount * 2u;

    static size_t GetPipelineVariantIndex(bool useSoftShadowVariant, uint32_t environmentProjectionVariant);
    static uint32_t GetStageVariantKey(
        ReSTIRDIStage stage,
        const ReSTIRDIFrameConstants& constants,
        MaterialShadingModel shadingModel);
    static std::vector<ShaderVariantDefine> GetStageVariantDefines(
        ReSTIRDIStage stage,
        const ReSTIRDIFrameConstants& constants,
        MaterialShadingModel shadingModel);
    PipelineSet& GetPipelines(bool useSoftShadowVariant, uint32_t environmentProjectionVariant);
    ComputeShader& GetStageShader(
        PipelineSet& pipelines,
        ReSTIRDIStage stage,
        const ReSTIRDIFrameConstants& constants,
        MaterialShadingModel shadingModel);
//Modify End
    void EnsureResources(uint32_t width, uint32_t height);
    void ExecuteInitialSampling(CommandContext& commandContext, const ReSTIRDIExecutionInputs& inputs, PipelineSet& pipelines);
    void ExecuteTemporalResampling(CommandContext& commandContext, const ReSTIRDIExecutionInputs& inputs, PipelineSet& pipelines);
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
//Modify Begin:2026-08-06 by BestHui
    std::array<std::unique_ptr<PipelineSet>, PipelineVariantCount> m_Pipelines;
//Modify End
    std::unique_ptr<InternalResources> m_Resources;
    uint32_t m_ResourceWidth = 0;
    uint32_t m_ResourceHeight = 0;
};
//Modify End
