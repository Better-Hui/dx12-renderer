#pragma once

//Modify Begin:2026-07-30 by BestHui
#include <Framework/Rendering/Lighting/ReSTIRDI.h>
#include <Framework/Rendering/Pipeline/ShaderVariant.h>

#include <RenderGraph/ResourceId.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class CommandList;
class CommandContext;
class ComputeShader;
class FrameworkDeviceContext;

namespace RenderGraph
{
    class RenderPass;
    struct RenderContext;
}

namespace FrameworkRenderFeatures
{
    struct ReSTIRDIFrameState
    {
        bool Enabled = false;
        bool UseSoftShadowVariant = false;
        uint32_t Width = 1;
        uint32_t Height = 1;
        uint32_t FrameIndex = 0;
        ReSTIRDIFrameConstants Constants = {};
    };

    struct ReSTIRDIGBufferResourceIds
    {
        RenderGraph::ResourceId AlbedoOcclusion = 0;
        RenderGraph::ResourceId SpecularSmoothness = 0;
        RenderGraph::ResourceId Normal = 0;
        RenderGraph::ResourceId EmissionMetallic = 0;
        RenderGraph::ResourceId Position = 0;
        RenderGraph::ResourceId MotionVector = 0;
        RenderGraph::ResourceId Depth = 0;
    };

    struct ReSTIRDIHistoryResourceIds
    {
        RenderGraph::ResourceId ReservoirA = 0;
        RenderGraph::ResourceId ReservoirB = 0;
        RenderGraph::ResourceId ReservoirAState = 0;
        RenderGraph::ResourceId ReservoirBState = 0;
        RenderGraph::ResourceId PositionA = 0;
        RenderGraph::ResourceId PositionB = 0;
        RenderGraph::ResourceId NormalRoughnessA = 0;
        RenderGraph::ResourceId NormalRoughnessB = 0;
        RenderGraph::ResourceId DiffuseMetallicA = 0;
        RenderGraph::ResourceId DiffuseMetallicB = 0;
        RenderGraph::ResourceId SpecularOcclusionA = 0;
        RenderGraph::ResourceId SpecularOcclusionB = 0;
    };

    struct ReSTIRDIIntermediateResourceIds
    {
        RenderGraph::ResourceId RISReservoir = 0;
        RenderGraph::ResourceId RISReservoirState = 0;
        RenderGraph::ResourceId TemporalReservoir = 0;
        RenderGraph::ResourceId TemporalReservoirState = 0;
        RenderGraph::ResourceId SpatialReservoir = 0;
        RenderGraph::ResourceId SpatialReservoirState = 0;
        RenderGraph::ResourceId RISFinishedToken = 0;
        RenderGraph::ResourceId TemporalFinishedToken = 0;
        RenderGraph::ResourceId SpatialFinishedToken = 0;
        RenderGraph::ResourceId ShadeFinishedToken = 0;
    };

    struct ReSTIRDIPassResources
    {
        RenderGraph::ResourceId PrerequisiteToken = 0;
        ReSTIRDIGBufferResourceIds GBuffer;
        ReSTIRDIHistoryResourceIds History;
        ReSTIRDIIntermediateResourceIds Intermediate;
        RenderGraph::ResourceId DirectLighting = 0;
    };

    struct ReSTIRDISceneAdapter
    {
        std::function<ReSTIRDIFrameState(const RenderGraph::RenderContext&)> GetFrameState;
        std::function<void(const RenderGraph::RenderContext&, CommandList&, ComputeShader&)> BindInputs;
    };

    struct ReSTIRDIPassInputs
    {
        ReSTIRDIPassResources Resources;
        std::shared_ptr<const ReSTIRDISceneAdapter> SceneAdapter;
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
        void AddPasses(
            std::vector<std::unique_ptr<RenderGraph::RenderPass>>& renderPasses,
            const ReSTIRDIPassInputs& inputs);

    private:
        enum class Stage : uint32_t
        {
            RIS,
            Temporal,
            Spatial,
            Shade,
        };

        struct PipelineSet;

        PipelineSet& GetPipelines(bool useSoftShadowVariant);
        std::unique_ptr<ComputeShader> CreateComputeShader(
            const std::wstring& compiledFileName,
            const std::wstring& sourceFileName,
            bool useSoftShadowVariant);
        void ExecuteStage(
            Stage stage,
            const ReSTIRDIPassInputs& inputs,
            const RenderGraph::RenderContext& context,
            CommandList& commandList,
            const std::function<void(CommandContext&, ComputeShader&, const ReSTIRDIFrameState&)>& bindStageResources);

        FrameworkDeviceContext& m_DeviceContext;
        ReSTIRDIShaderSources m_ShaderSources;
        ShaderVariantManager m_ShaderVariants;
        std::unique_ptr<PipelineSet> m_HardShadowPipelines;
        std::unique_ptr<PipelineSet> m_SoftShadowPipelines;
    };
}
//Modify End
