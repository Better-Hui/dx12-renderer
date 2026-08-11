#pragma once

#include <Framework/Rendering/Lighting/ReSTIRGI.h>
#include <Framework/Rendering/Pipeline/ShaderVariant.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
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
    std::function<void(CommandContext&, ComputeShader&)> BindSceneInputs;
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

    void EnsurePipelines(bool useSoftShadowVariant, uint32_t environmentProjectionVariant);
    void Execute(CommandList& commandList, const ReSTIRGIExecutionInputs& inputs);

private:
    struct PipelineSet;
    struct InternalResources;

    static constexpr uint32_t EnvironmentProjectionVariantCount = 3u;
    static constexpr uint32_t PipelineVariantCount = EnvironmentProjectionVariantCount * 2u;

    static size_t GetPipelineVariantIndex(bool useSoftShadowVariant, uint32_t environmentProjectionVariant);
    PipelineSet& GetPipelines(bool useSoftShadowVariant, uint32_t environmentProjectionVariant);
//Modify Begin:2026-07-30 by BestHui
    bool EnsureResources(uint32_t width, uint32_t height);
//Modify End
    void ExecuteInitialSampling(CommandList& commandList, const ReSTIRGIExecutionInputs& inputs, PipelineSet& pipelines);
    void ExecuteTemporalResampling(CommandList& commandList, const ReSTIRGIExecutionInputs& inputs, PipelineSet& pipelines);
    void ExecuteSpatialResampling(CommandList& commandList, const ReSTIRGIExecutionInputs& inputs, PipelineSet& pipelines);
    void ExecuteFinalShading(
        CommandList& commandList,
        const ReSTIRGIExecutionInputs& inputs,
        PipelineSet& pipelines,
        const std::shared_ptr<Texture>& reservoirCreation,
        const std::shared_ptr<Texture>& reservoirHit,
        const std::shared_ptr<Texture>& reservoirLight);
    std::unique_ptr<ComputeShader> CreateComputeShader(
        const std::wstring& compiledFileName,
        const std::wstring& sourceFileName,
        bool useSoftShadowVariant,
        uint32_t environmentProjectionVariant);

    FrameworkDeviceContext& m_DeviceContext;
    ReSTIRGIShaderSources m_ShaderSources;
    ShaderVariantManager m_ShaderVariants;
    std::array<std::unique_ptr<PipelineSet>, PipelineVariantCount> m_Pipelines;
    std::unique_ptr<InternalResources> m_Resources;
    uint32_t m_ResourceWidth = 0;
    uint32_t m_ResourceHeight = 0;
    uint32_t m_HistoryReadIndex = 0;
//Modify Begin:2026-07-30 by BestHui
    bool m_HistoryValid = false;
//Modify End
};
//Modify End
