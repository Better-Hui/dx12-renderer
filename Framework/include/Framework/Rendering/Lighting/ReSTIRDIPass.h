#pragma once

//Modify Begin:2026-07-30 by BestHui
#include <Framework/Rendering/Lighting/ReSTIRDI.h>
#include <Framework/Rendering/Pipeline/ShaderVariant.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class CommandList;
class ComputeShader;
class FrameworkDeviceContext;
class Texture;

struct ReSTIRDIFrameState
{
    bool Enabled = false;
    bool UseSoftShadowVariant = false;
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
    std::function<void(CommandList&, ComputeShader&)> BindSceneInputs;
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
};

class ReSTIRDIPass final
{
public:
    ReSTIRDIPass(FrameworkDeviceContext& deviceContext, ReSTIRDIShaderSources shaderSources);
    ~ReSTIRDIPass();

    ReSTIRDIPass(const ReSTIRDIPass&) = delete;
    ReSTIRDIPass& operator=(const ReSTIRDIPass&) = delete;

//Modify Begin:2026-08-06 by BestHui
    void EnsurePipelines(bool useSoftShadowVariant, uint32_t environmentProjectionVariant);
//Modify End
    void Execute(
        CommandList& commandList,
        const ReSTIRDIExecutionInputs& inputs);

private:
    struct PipelineSet;
    struct InternalResources;

//Modify Begin:2026-08-06 by BestHui
    static constexpr uint32_t EnvironmentProjectionVariantCount = 3u;
    static constexpr uint32_t PipelineVariantCount = EnvironmentProjectionVariantCount * 2u;

    static size_t GetPipelineVariantIndex(bool useSoftShadowVariant, uint32_t environmentProjectionVariant);
    PipelineSet& GetPipelines(bool useSoftShadowVariant, uint32_t environmentProjectionVariant);
//Modify End
    void EnsureResources(uint32_t width, uint32_t height);
    void ExecuteInitialSampling(CommandList& commandList, const ReSTIRDIExecutionInputs& inputs, PipelineSet& pipelines);
    void ExecuteTemporalResampling(CommandList& commandList, const ReSTIRDIExecutionInputs& inputs, PipelineSet& pipelines);
    void ExecuteSpatialResampling(CommandList& commandList, const ReSTIRDIExecutionInputs& inputs, PipelineSet& pipelines);
    void ExecuteFinalShading(CommandList& commandList, const ReSTIRDIExecutionInputs& inputs, PipelineSet& pipelines);
    std::unique_ptr<ComputeShader> CreateComputeShader(
        const std::wstring& compiledFileName,
        const std::wstring& sourceFileName,
        bool useSoftShadowVariant,
        uint32_t environmentProjectionVariant);

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
