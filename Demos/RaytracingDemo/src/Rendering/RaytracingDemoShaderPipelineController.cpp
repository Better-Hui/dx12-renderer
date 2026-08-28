//Modify Begin:2026-08-28 by Hui
#include <Rendering/RaytracingDemoShaderPipelineController.h>

#include <Framework/Core/FrameworkDeviceContext.h>
#include <Framework/Rendering/Pipeline/ComputePipelineStateBuilder.h>
#include <Framework/Rendering/Pipeline/PipelineLayout.h>
#include <Framework/Rendering/Pipeline/RasterPipelineStateBuilder.h>
#include <Framework/Rendering/Pipeline/ShaderTargetProfile.h>

#include <stdexcept>
#include <utility>

namespace
{
    template<typename CreateFunc>
    void CreatePipeline(const char* name, CreateFunc&& createFunc)
    {
        try
        {
            std::forward<CreateFunc>(createFunc)();
        }
        catch (const std::exception& exception)
        {
            throw std::runtime_error(
                std::string("Failed to create shader pipeline '") + name + "': " + exception.what());
        }
    }

    PipelineLayoutReflectionOptions CreateBindlessGraphicsLayout()
    {
        PipelineLayoutReflectionOptions options;
        options.MaxDescriptorCount = 4096u;
        options.ShaderStages = PipelineShaderStageFlags::AllGraphics;
        options.UsesBindlessResourceHeap = true;
        options.StaticSamplerContracts = {
            PipelineStaticSamplers::PointWrap(0u),
            PipelineStaticSamplers::LinearWrap(1u),
            PipelineStaticSamplers::PointClamp(2u),
            PipelineStaticSamplers::LinearClamp(3u),
            PipelineStaticSamplers::ShadowCompareClamp(4u),
        };
        return options;
    }
}

RaytracingDemoShaderPipelineController::RaytracingDemoShaderPipelineController(
    FrameworkDeviceContext& deviceContext)
    : m_DeviceContext(deviceContext)
{
}

std::shared_ptr<ShaderBlob> RaytracingDemoShaderPipelineController::LoadShaderVariant(
    std::wstring compiledFileName,
    std::wstring sourceFileName,
    std::string targetProfile,
    std::vector<ShaderVariantDefine> defines)
{
    ShaderVariantDesc desc;
    desc.CompiledFileName = std::move(compiledFileName);
    desc.SourceFileName = std::move(sourceFileName);
    desc.TargetProfile = std::move(targetProfile);
    desc.Defines = std::move(defines);
    desc.DebugName = "RaytracingDemo";
    return m_ShaderVariants.GetOrCompile(desc);
}

void RaytracingDemoShaderPipelineController::CreateGeometryPipelines()
{
    if (m_GBufferShader != nullptr)
    {
        return;
    }

    CreatePipeline("GBuffer", [this]()
    {
        const auto vertexShader = LoadShaderVariant(
            L"GBuffer.vs.cso",
            L"Demos/RaytracingDemo/shaders/GBuffer/GBuffer.vs.hlsl",
            ShaderTargetProfile::Vertex());
        const auto pixelShader = LoadShaderVariant(
            L"GBuffer.ps.cso",
            L"Demos/RaytracingDemo/shaders/GBuffer/GBuffer.ps.hlsl",
            ShaderTargetProfile::Pixel());
        m_GBufferShader = std::make_shared<Shader>(
            m_DeviceContext,
            *vertexShader,
            *pixelShader,
            CreateBindlessGraphicsLayout(),
            [](RasterPipelineStateBuilder& builder)
            {
                builder.WithNoCull();
            });
    });

    CreatePipeline("GBufferTaskMeshShader", [this]()
    {
        const auto amplificationShader = LoadShaderVariant(
            L"GBuffer.task.as.cso",
            L"Demos/RaytracingDemo/shaders/GBuffer/GBuffer.task.as.hlsl",
            ShaderTargetProfile::Amplification());
        const auto meshShader = LoadShaderVariant(
            L"GBuffer.task.ms.cso",
            L"Demos/RaytracingDemo/shaders/GBuffer/GBuffer.task.ms.hlsl",
            ShaderTargetProfile::Mesh());
        const auto pixelShader = LoadShaderVariant(
            L"GBuffer.meshletindirect.ps.cso",
            L"Demos/RaytracingDemo/shaders/GBuffer/GBuffer.meshletindirect.ps.hlsl",
            ShaderTargetProfile::Pixel());
        m_GBufferTaskMeshShader = std::make_shared<MeshShader>(
            m_DeviceContext,
            *amplificationShader,
            *meshShader,
            *pixelShader,
            CreateBindlessGraphicsLayout(),
            [](RasterPipelineStateBuilder& builder)
            {
                builder.WithNoCull();
            });
    });

    CreatePipeline("GBufferMeshletIndirect", [this]()
    {
        const auto vertexShader = LoadShaderVariant(
            L"GBuffer.meshletindirect.vs.cso",
            L"Demos/RaytracingDemo/shaders/GBuffer/GBuffer.meshletindirect.vs.hlsl",
            ShaderTargetProfile::Vertex());
        const auto pixelShader = LoadShaderVariant(
            L"GBuffer.meshletindirect.ps.cso",
            L"Demos/RaytracingDemo/shaders/GBuffer/GBuffer.meshletindirect.ps.hlsl",
            ShaderTargetProfile::Pixel());
        PipelineLayoutReflectionOptions layoutOptions = CreateBindlessGraphicsLayout();
        layoutOptions.RootConstantBufferNames.push_back("MeshletDrawCBuffer");
        m_GBufferMeshletIndirectShader = std::make_shared<Shader>(
            m_DeviceContext,
            *vertexShader,
            *pixelShader,
            std::move(layoutOptions),
            [](RasterPipelineStateBuilder& builder)
            {
                builder.WithNoCull();
            });
    });

    CreatePipeline("MeshletCull", [this]()
    {
        const auto shaderBlob = LoadShaderVariant(
            L"MeshletCull.cs.cso",
            L"Demos/RaytracingDemo/shaders/GBuffer/MeshletCull.cs.hlsl",
            ShaderTargetProfile::Compute());
        m_MeshletCullShader = std::make_shared<ComputeShader>(
            m_DeviceContext,
            *shaderBlob,
            ComputePipelineDescBuilder::ReflectedDefault(*shaderBlob).Build());
        m_MeshletDrawCommandSignature = m_GBufferMeshletIndirectShader->CreateIndirectDrawCommandSignature(
            "MeshletDrawCBuffer",
            sizeof(MeshletIndirectCommand));
    });
}

void RaytracingDemoShaderPipelineController::CreatePostProcessPipelines()
{
    if (m_DisplayCompositeShader != nullptr)
    {
        return;
    }

    CreatePipeline("DisplayComposite", [this]()
    {
        const auto vertexShader = LoadShaderVariant(
            L"DisplayComposite.vs.cso",
            L"Demos/RaytracingDemo/shaders/PostProcessing/DisplayComposite.vs.hlsl",
            ShaderTargetProfile::Vertex());
        const auto pixelShader = LoadShaderVariant(
            L"DisplayComposite.ps.cso",
            L"Demos/RaytracingDemo/shaders/PostProcessing/DisplayComposite.ps.hlsl",
            ShaderTargetProfile::Pixel());
        PipelineLayoutReflectionOptions layoutOptions;
        layoutOptions.MaxDescriptorCount = 4096u;
        layoutOptions.ShaderStages = PipelineShaderStageFlags::AllGraphics;
        layoutOptions.StaticSamplerContracts = { PipelineStaticSamplers::LinearClamp(3u) };
        m_DisplayCompositeShader = std::make_shared<Shader>(
            m_DeviceContext,
            *vertexShader,
            *pixelShader,
            std::move(layoutOptions),
            [](RasterPipelineStateBuilder&) {});
    });

    CreatePipeline("Hdr10Presentation", [this]()
    {
        const auto vertexShader = LoadShaderVariant(
            L"Hdr10Presentation.vs.cso",
            L"Demos/RaytracingDemo/shaders/PostProcessing/DisplayComposite.vs.hlsl",
            ShaderTargetProfile::Vertex());
        const auto pixelShader = LoadShaderVariant(
            L"Hdr10Presentation.ps.cso",
            L"Demos/RaytracingDemo/shaders/PostProcessing/Hdr10Presentation.ps.hlsl",
            ShaderTargetProfile::Pixel());
        PipelineLayoutReflectionOptions layoutOptions;
        layoutOptions.MaxDescriptorCount = 4096u;
        layoutOptions.ShaderStages = PipelineShaderStageFlags::AllGraphics;
        layoutOptions.StaticSamplerContracts = { PipelineStaticSamplers::LinearClamp(3u) };
        m_Hdr10PresentationShader = std::make_shared<Shader>(
            m_DeviceContext,
            *vertexShader,
            *pixelShader,
            std::move(layoutOptions),
            [](RasterPipelineStateBuilder&) {});
    });

    CreatePipeline("SkyboxCompute", [this]()
    {
        const auto skyboxShader = LoadShaderVariant(
            L"Skybox.cs.cso",
            L"Demos/RaytracingDemo/shaders/Skybox/Skybox.cs.hlsl",
            ShaderTargetProfile::Compute());
        m_SkyboxComputeShader = std::make_shared<ComputeShader>(
            m_DeviceContext,
            *skyboxShader,
            ComputePipelineDescBuilder::ReflectedDefault(*skyboxShader)
                .WithCommonRootSignatureStaticSamplers()
                .Build());

        const auto equirectangularShader = LoadShaderVariant(
            L"SkyboxEquirectangular.cs.cso",
            L"Demos/RaytracingDemo/shaders/Skybox/SkyboxEquirectangular.cs.hlsl",
            ShaderTargetProfile::Compute());
        m_SkyboxEquirectangularComputeShader = std::make_shared<ComputeShader>(
            m_DeviceContext,
            *equirectangularShader,
            ComputePipelineDescBuilder::ReflectedDefault(*equirectangularShader)
                .WithCommonRootSignatureStaticSamplers()
                .Build());

        const auto cubemapStripShader = LoadShaderVariant(
            L"SkyboxCubemapStrip.cs.cso",
            L"Demos/RaytracingDemo/shaders/Skybox/SkyboxCubemapStrip.cs.hlsl",
            ShaderTargetProfile::Compute());
        m_SkyboxCubemapStripComputeShader = std::make_shared<ComputeShader>(
            m_DeviceContext,
            *cubemapStripShader,
            ComputePipelineDescBuilder::ReflectedDefault(*cubemapStripShader)
                .WithCommonRootSignatureStaticSamplers()
                .Build());
    });

    CreatePipeline("DLSSRayReconstructionPrepare", [this]()
    {
        const auto shaderBlob = LoadShaderVariant(
            L"DLSSRayReconstructionPrepare.cs.cso",
            L"Demos/RaytracingDemo/shaders/Upscaling/DLSSRayReconstructionPrepare.cs.hlsl",
            ShaderTargetProfile::Compute());
        m_DLSSRayReconstructionPrepareShader = std::make_shared<ComputeShader>(
            m_DeviceContext,
            *shaderBlob,
            ComputePipelineDescBuilder::ReflectedDefault(*shaderBlob).Build());
    });

    CreatePipeline("CopyQueueValidation", [this]()
    {
        const auto shaderBlob = LoadShaderVariant(
            L"CopyQueueValidation.cs.cso",
            L"Demos/RaytracingDemo/shaders/Diagnostics/CopyQueueValidation.cs.hlsl",
            ShaderTargetProfile::Compute());
        m_CopyQueueValidationShader = std::make_shared<ComputeShader>(
            m_DeviceContext,
            *shaderBlob,
            ComputePipelineDescBuilder::ReflectedDefault(*shaderBlob).Build());
    });
}

void RaytracingDemoShaderPipelineController::CreateLightingPipelines()
{
    if (m_LightBillboardShader != nullptr)
    {
        return;
    }

    CreatePipeline("LightBillboard", [this]()
    {
        const auto vertexShader = LoadShaderVariant(
            L"LightBillboard.vs.cso",
            L"Demos/RaytracingDemo/shaders/LightBillboard/LightBillboard.vs.hlsl",
            ShaderTargetProfile::Vertex());
        const auto pixelShader = LoadShaderVariant(
            L"LightBillboard.ps.cso",
            L"Demos/RaytracingDemo/shaders/LightBillboard/LightBillboard.ps.hlsl",
            ShaderTargetProfile::Pixel());
        m_LightBillboardShader = std::make_shared<Shader>(
            m_DeviceContext,
            *vertexShader,
            *pixelShader,
            [](RasterPipelineStateBuilder& builder)
            {
                builder.WithAlphaBlend().WithDepthTestNoWrite().WithNoCull();
            });
    });
}

void RaytracingDemoShaderPipelineController::Reset()
{
    m_LightBillboardShader.reset();
    m_CopyQueueValidationShader.reset();
    m_DLSSRayReconstructionPrepareShader.reset();
    m_SkyboxCubemapStripComputeShader.reset();
    m_SkyboxEquirectangularComputeShader.reset();
    m_SkyboxComputeShader.reset();
    m_Hdr10PresentationShader.reset();
    m_DisplayCompositeShader.reset();
    m_GBufferTaskMeshShader.reset();
    m_MeshletDrawCommandSignature.reset();
    m_MeshletCullShader.reset();
    m_GBufferMeshletIndirectShader.reset();
    m_GBufferShader.reset();
    m_ShaderVariants.Clear();
}
//Modify End
