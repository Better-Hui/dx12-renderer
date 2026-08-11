#pragma once

#include <Framework/Rendering/Lighting/ReSTIRGI.h>
#include <Framework/Rendering/Pipeline/ShaderVariant.h>

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

//Modify Begin:2026-08-10 by BestHui
struct ReSTIRGIFrameState
{
    bool Enabled = false;
    bool UseSoftShadowVariant = false;
    uint32_t EnvironmentProjectionVariant = 0u;
    ReSTIRGIFrameConstants Constants = {};
};

struct ReSTIRGIExecutionInputs
{
    ReSTIRGIFrameState FrameState;
    std::shared_ptr<Texture> IndirectLighting;
    std::shared_ptr<Texture> MotionVector;
//Modify Begin:2026-08-11 by BestHui
    std::function<void(CommandContext&)> PrepareCommandContext;
//Modify End
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
        const ReSTIRGIFrameConstants& constants,
        uint32_t maxPathBounces);
    void Execute(CommandList& commandList, const ReSTIRGIExecutionInputs& inputs);

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
        const ReSTIRGIFrameConstants& constants) const;
    std::vector<ShaderVariantDefine> GetStageVariantDefines(
        ReSTIRGIStage stage,
        const ReSTIRGIFrameConstants& constants) const;
    PipelineSet& GetPipelines(
        bool useSoftShadowVariant,
        uint32_t environmentProjectionVariant);
    ComputeShader& GetStageShader(
        PipelineSet& pipelines,
        ReSTIRGIStage stage,
        const ReSTIRGIFrameConstants& constants);
//Modify Begin:2026-07-30 by BestHui
    bool EnsureResources(uint32_t width, uint32_t height);
//Modify End
//Modify Begin:2026-08-11 by BestHui
    void ExecuteInitialSampling(CommandContext& commandContext, const ReSTIRGIExecutionInputs& inputs, PipelineSet& pipelines);
    void ExecuteTemporalResampling(CommandContext& commandContext, const ReSTIRGIExecutionInputs& inputs, PipelineSet& pipelines);
    void ExecuteSpatialResampling(CommandContext& commandContext, const ReSTIRGIExecutionInputs& inputs, PipelineSet& pipelines);
//Modify End
    void ExecuteFinalShading(
//Modify Begin:2026-08-11 by BestHui
        CommandContext& commandContext,
//Modify End
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
    uint32_t m_HistoryReadIndex = 0;
    uint32_t m_MaxPathBounces = 3;
//Modify Begin:2026-07-30 by BestHui
    bool m_HistoryValid = false;
//Modify End
};
//Modify End
