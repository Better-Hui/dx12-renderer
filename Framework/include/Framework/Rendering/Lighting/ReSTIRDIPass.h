#pragma once

//Modify Begin:2026-07-30 by BestHui
#include <Framework/Rendering/Lighting/ReSTIRDI.h>
#include <Framework/Rendering/Pipeline/ShaderVariant.h>

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
};

class ReSTIRDIPass final
{
public:
    ReSTIRDIPass(FrameworkDeviceContext& deviceContext, ReSTIRDIShaderSources shaderSources);
    ~ReSTIRDIPass();

    ReSTIRDIPass(const ReSTIRDIPass&) = delete;
    ReSTIRDIPass& operator=(const ReSTIRDIPass&) = delete;

    void EnsurePipelines(bool useSoftShadowVariant);
    void Execute(
        CommandList& commandList,
        const ReSTIRDIExecutionInputs& inputs);

private:
    struct PipelineSet;
    struct InternalResources;

    PipelineSet& GetPipelines(bool useSoftShadowVariant);
    void EnsureResources(uint32_t width, uint32_t height);
    void ExecuteInitialSampling(CommandList& commandList, const ReSTIRDIExecutionInputs& inputs, PipelineSet& pipelines);
    void ExecuteTemporalResampling(CommandList& commandList, const ReSTIRDIExecutionInputs& inputs, PipelineSet& pipelines);
    void ExecuteSpatialResampling(CommandList& commandList, const ReSTIRDIExecutionInputs& inputs, PipelineSet& pipelines);
    void ExecuteFinalShading(CommandList& commandList, const ReSTIRDIExecutionInputs& inputs, PipelineSet& pipelines);
    std::unique_ptr<ComputeShader> CreateComputeShader(
        const std::wstring& compiledFileName,
        const std::wstring& sourceFileName,
        bool useSoftShadowVariant);

    FrameworkDeviceContext& m_DeviceContext;
    ReSTIRDIShaderSources m_ShaderSources;
    ShaderVariantManager m_ShaderVariants;
    std::unique_ptr<PipelineSet> m_HardShadowPipelines;
    std::unique_ptr<PipelineSet> m_SoftShadowPipelines;
    std::unique_ptr<InternalResources> m_Resources;
    uint32_t m_ResourceWidth = 0;
    uint32_t m_ResourceHeight = 0;
};
//Modify End
