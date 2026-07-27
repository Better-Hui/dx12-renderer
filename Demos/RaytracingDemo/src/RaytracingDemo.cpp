#include <RaytracingDemo.h>

#include <DX12Library/Application.h>
#include <DX12Library/CommandList.h>
#include <DX12Library/CommandQueue.h>
#include <DX12Library/Events.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/Window.h>

#include <Framework/GraphicsSettings.h>
#include <Framework/Mesh.h>
#include <Framework/ModelLoader.h>
#include <Framework/RasterPipelineStateBuilder.h>
#include <Framework/ShaderBlob.h>

#include <RenderGraph/RenderMetadata.h>

#include <DirectXMath.h>
#include <d3dx12.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

#include <RenderGraph/RaytracingDemoRenderGraphBuilder.h>

#if defined(min)
#undef min
#endif

#if defined(max)
#undef max
#endif

using namespace DirectX;

namespace
{
    uint32_t ComputeDescriptorArrayCapacity(const size_t resourceCount, const size_t resourceCapacity)
    {
        return static_cast<uint32_t>(std::max<size_t>(
            RaytracingDemo::MinRayTracingDescriptorArrayCapacity,
            std::max(resourceCount, resourceCapacity)));
    }

}

RaytracingDemo::RaytracingDemo(const std::wstring& name, const int width, const int height, GraphicsSettings graphicsSettings)
    : Base(name, width, height, false)
    , m_Width(width)
    , m_Height(height)
{
    (void)graphicsSettings;

    const XMVECTOR cameraPos = XMVectorSet(0, 8, -35, 1);
    const XMVECTOR cameraTarget = XMVectorSet(0, 5, 18, 1);
    const XMVECTOR cameraUp = XMVectorSet(0, 1, 0, 0);
    m_Camera.SetLookAt(cameraPos, cameraTarget, cameraUp);
    m_Camera.SetProjection(m_CameraFov, static_cast<float>(m_Width) / static_cast<float>(m_Height), 0.1f, 1000.0f);

    char* mode = nullptr;
    size_t modeLength = 0;
    _dupenv_s(&mode, &modeLength, "RAYTRACING_DEMO_MODE");
    if (mode != nullptr && std::strcmp(mode, "shader-table") == 0)
    {
        m_PathTracingBackend = PathTracingBackend::ShaderTableDxr;
    }
    std::free(mode);

    char* nrdMode = nullptr;
    size_t nrdModeLength = 0;
    _dupenv_s(&nrdMode, &nrdModeLength, "RAYTRACING_DEMO_NRD");
    if (nrdMode != nullptr)
    {
        m_Denoisers.SetAlgorithm(std::strcmp(nrdMode, "0") != 0 ? DenoiserController::Algorithm::NRD : DenoiserController::Algorithm::Off);
    }
    std::free(nrdMode);

    char* denoiserMode = nullptr;
    size_t denoiserModeLength = 0;
    _dupenv_s(&denoiserMode, &denoiserModeLength, "RAYTRACING_DEMO_DENOISER");
    if (denoiserMode != nullptr)
    {
        m_Denoisers.SetAlgorithmFromName(denoiserMode);
    }
    std::free(denoiserMode);

}

bool RaytracingDemo::LoadContent()
{
    Assert(RayTracingShader::IsSupported(), "DirectX Raytracing is not supported by the selected adapter.");

    const auto commandQueue = Application::Get().GetCommandQueue();
    const auto commandList = commandQueue->GetCommandList();

    m_SceneResources.LoadDeferredLightingScene(*commandList);
    m_Lights.CreateDemoLights();

    m_SkyboxTexture = std::make_shared<Texture>();
    commandList->LoadTextureFromFile(*m_SkyboxTexture, L"Assets/Textures/skybox/skybox.dds", TextureUsageType::Albedo);

    m_ImGui = std::make_unique<ImGuiImpl>(*commandList, *PWindow);

    m_SkyboxMesh = Mesh::CreateCube(*commandList);
    m_LightBillboardMesh = Mesh::CreateVerticalQuad(*commandList);

    m_GBufferShader = std::make_shared<Shader>(
        ShaderBlob(L"GBuffer.vs.cso"),
        ShaderBlob(L"GBuffer.ps.cso"),
        [](RasterPipelineStateBuilder&) {});

    m_SkyboxShader = std::make_shared<Shader>(
        ShaderBlob(L"Skybox.vs.cso"),
        ShaderBlob(L"Skybox.ps.cso"),
        [](RasterPipelineStateBuilder& builder)
        {
            builder.WithFrontFaceCull().WithDepthTestNoWrite();
        });

    m_LightBillboardShader = std::make_shared<Shader>(
        ShaderBlob(L"LightBillboard.vs.cso"),
        ShaderBlob(L"LightBillboard.ps.cso"),
        [](RasterPipelineStateBuilder& builder)
        {
            builder.WithAlphaBlend().WithDepthTestNoWrite().WithNoCull();
        });

    m_Denoisers.Initialize();
    if (IsDenoiserEnabled())
    {
        m_AccumulationEnabled = false;
    }

    m_SceneResources.AddRayTracingInstances(m_RayTracingAccelerationStructure);

    EnsureRayTracingPipelines();

    RayTracingAccelerationStructureBuildSettings accelerationStructureSettings{};
    accelerationStructureSettings.AllowUpdate = true;
    m_RayTracingAccelerationStructure.Build(*commandList, accelerationStructureSettings);
    m_SceneResources.UploadRayTracingBuffers(*commandList, m_RayTracingAccelerationStructure);
    m_Lights.InitializeGpuBuffers(*commandList);
    if (m_DirectRayTracingBindingSet != nullptr || m_IndirectRayTracingBindingSet != nullptr)
    {
        BindRayTracingShaderResources();
    }

    m_RenderGraph = RaytracingDemoRenderGraphBuilder::Create(*this, *commandList);

    const uint64_t fenceValue = commandQueue->ExecuteCommandList(commandList);
    commandQueue->WaitForFenceValue(fenceValue);
    return true;
}

void RaytracingDemo::UnloadContent()
{
    m_RenderGraph.reset();
    m_LightBillboardShader.reset();
    m_SkyboxShader.reset();
    m_GBufferShader.reset();
    m_LightBillboardMesh.reset();
    m_SkyboxMesh.reset();
    m_ImGui.reset();
    m_Denoisers.Shutdown();
    m_SkyboxTexture.reset();
    m_LightingCompositeShader.reset();
    m_InlineIndirectLightingShader.reset();
    m_InlineDirectLightingShader.reset();
    m_IndirectRayTracingBindingSet.reset();
    m_DirectRayTracingBindingSet.reset();
    m_RayTracingShader.reset();
    m_SceneResources.Clear();
}

RaytracingDemo::RayTracingSceneResourceLayout RaytracingDemo::BuildRayTracingSceneResourceLayout() const
{
    const std::vector<std::shared_ptr<Mesh>>& rayTracingMeshes = m_RayTracingAccelerationStructure.GetMeshes();

    RayTracingSceneResourceLayout layout;
    layout.TextureDescriptorCapacity = ComputeDescriptorArrayCapacity(m_SceneResources.GetTextureCount(), m_SceneResources.GetTextureCapacity());
    layout.GeometryDescriptorCapacity = ComputeDescriptorArrayCapacity(rayTracingMeshes.size(), rayTracingMeshes.capacity());
    return layout;
}

void RaytracingDemo::EnsureRayTracingPipelines()
{
    const RayTracingSceneResourceLayout layout = BuildRayTracingSceneResourceLayout();
    const bool needsDxrPipeline = m_PathTracingBackend == PathTracingBackend::ShaderTableDxr;
    if ((!needsDxrPipeline || (m_RayTracingShader != nullptr &&
            m_DirectRayTracingBindingSet != nullptr &&
            m_IndirectRayTracingBindingSet != nullptr)) &&
        m_InlineDirectLightingShader != nullptr &&
        m_InlineIndirectLightingShader != nullptr &&
        m_LightingCompositeShader != nullptr &&
        !(m_RayTracingSceneResourceLayout != layout))
    {
        return;
    }

    m_RayTracingSceneResourceLayout = layout;

    if (needsDxrPipeline)
    {
        const ShaderBlob pathTracingShader(L"PathTracing.rt.cso");
        const RayTracingPipelineDesc rayTracingDesc = RayTracingPipelineDescBuilder::ReflectedDefault(pathTracingShader)
            .WithExport(L"DirectLightingRayGen")
            .WithExport(L"IndirectLightingRayGen")
            .WithRayGenerationPass("DirectLightingRayGen", L"DirectLightingRayGen", { L"Miss" }, { L"HitGroup" })
            .WithRayGenerationPass("IndirectLightingRayGen", L"IndirectLightingRayGen", { L"Miss" }, { L"HitGroup" })
            .WithTextureArray("Textures", 0, 3, layout.TextureDescriptorCapacity)
            .WithVertexBufferArray("VertexBuffers", 0, 1, layout.GeometryDescriptorCapacity)
            .WithIndexBufferArray("IndexBuffers", 0, 2, layout.GeometryDescriptorCapacity)
            .WithPayloadSize(64)
            .Build();
        m_RayTracingShader = std::make_unique<RayTracingShader>(pathTracingShader, rayTracingDesc);
        m_DirectRayTracingBindingSet = std::make_unique<RayTracingBindingSet>(m_RayTracingShader->CreateBindingSet());
        m_IndirectRayTracingBindingSet = std::make_unique<RayTracingBindingSet>(m_RayTracingShader->CreateBindingSet());
    }
    else
    {
        m_IndirectRayTracingBindingSet.reset();
        m_DirectRayTracingBindingSet.reset();
        m_RayTracingShader.reset();
    }

    const ShaderBlob inlineDirectLightingShader(L"DirectLighting.cs.cso");
    const ComputePipelineDesc inlineDirectLightingDesc = ComputePipelineDescBuilder::ReflectedDefault(inlineDirectLightingShader)
        .WithDescriptorArrayCount("Textures", layout.TextureDescriptorCapacity)
        .WithDescriptorArrayCount("VertexBuffers", layout.GeometryDescriptorCapacity)
        .WithDescriptorArrayCount("IndexBuffers", layout.GeometryDescriptorCapacity)
        .Build();
    m_InlineDirectLightingShader = std::make_unique<ComputeShader>(inlineDirectLightingShader, inlineDirectLightingDesc);

    const ShaderBlob inlineIndirectLightingShader(L"IndirectLighting.cs.cso");
    const ComputePipelineDesc inlineIndirectLightingDesc = ComputePipelineDescBuilder::ReflectedDefault(inlineIndirectLightingShader)
        .WithDescriptorArrayCount("Textures", layout.TextureDescriptorCapacity)
        .WithDescriptorArrayCount("VertexBuffers", layout.GeometryDescriptorCapacity)
        .WithDescriptorArrayCount("IndexBuffers", layout.GeometryDescriptorCapacity)
        .Build();
    m_InlineIndirectLightingShader = std::make_unique<ComputeShader>(inlineIndirectLightingShader, inlineIndirectLightingDesc);

    const ShaderBlob lightingCompositeShader(L"LightingComposite.cs.cso");
    m_LightingCompositeShader = std::make_unique<ComputeShader>(
        lightingCompositeShader,
        ComputePipelineDescBuilder::ReflectedDefault(lightingCompositeShader).Build());

    if ((m_DirectRayTracingBindingSet != nullptr || m_IndirectRayTracingBindingSet != nullptr) &&
        m_RayTracingAccelerationStructure.GetInstanceCount() > 0)
    {
        BindRayTracingShaderResources();
    }
}

void RaytracingDemo::BindRayTracingShaderResources()
{
    if (m_DirectRayTracingBindingSet != nullptr)
    {
        BindRayTracingShaderResources(*m_DirectRayTracingBindingSet);
    }
    if (m_IndirectRayTracingBindingSet != nullptr)
    {
        BindRayTracingShaderResources(*m_IndirectRayTracingBindingSet);
    }
}

void RaytracingDemo::BindRayTracingShaderResources(RayTracingBindingSet& shader)
{
    shader.SetAccelerationStructure("Scene", m_RayTracingAccelerationStructure);
    shader.SetBuffer("Materials", m_SceneResources.GetMaterialBuffer());
    shader.SetBuffer("Geometries", m_SceneResources.GetGeometryBuffer());
    m_Lights.BindRayTracingResources(shader);
    const std::vector<std::shared_ptr<Texture>>& textures = m_SceneResources.GetTextures();
    for (uint32_t textureIndex = 0; textureIndex < textures.size(); ++textureIndex)
    {
        shader.SetTexture("Textures", textureIndex, ShaderResourceView(textures[textureIndex]));
    }
    shader.SetTexture("Skybox", ShaderResourceView::TextureCube(m_SkyboxTexture));
}

RaytracingDemo::CameraConstants RaytracingDemo::BuildCameraConstants() const
{
    CameraConstants camera{};
    camera.InverseView = XMMatrixInverse(nullptr, m_Camera.GetViewMatrix());
    camera.InverseProjection = XMMatrixInverse(nullptr, m_Camera.GetProjectionMatrix());
    XMStoreFloat4(&camera.CameraPosition, m_Camera.GetTranslation());
    camera.Width = static_cast<uint32_t>(m_Width);
    camera.Height = static_cast<uint32_t>(m_Height);
    camera.MaxBounces = static_cast<uint32_t>(std::clamp(m_MaxBounces, 0, 5));
    camera.SamplesPerPixel = 1;
    m_Lights.FillCameraConstants(camera.DirectionalLightCount, camera.PointLightCount, camera.AreaLightCount, camera.SkyLight);
    camera.FrameIndex = m_FrameIndex;
    const bool pathAccumulationEnabled = m_AccumulationEnabled && !IsDenoiserEnabled();
    camera.AccumulationFrameIndex = pathAccumulationEnabled ? m_AccumulationFrameIndex : 0u;
    camera.AccumulationEnabled = pathAccumulationEnabled ? 1u : 0u;
    m_Denoisers.FillCameraConstants(camera.NRDDenoiserMode, camera.NRDReblurHitDistanceParameters);
    camera.DirectLightingEnabled = m_DirectLightingEnabled ? 1u : 0u;
    camera.IndirectLightingEnabled = m_IndirectLightingEnabled ? 1u : 0u;
    return camera;
}

RaytracingDemo::PipelineConstants RaytracingDemo::BuildPipelineConstants() const
{
    PipelineConstants pipeline{};
    pipeline.View = m_Camera.GetViewMatrix();
    pipeline.Projection = m_Camera.GetProjectionMatrix();
    pipeline.ViewProjection = pipeline.View * pipeline.Projection;
    XMStoreFloat4(&pipeline.CameraPosition, m_Camera.GetTranslation());
    pipeline.InverseView = XMMatrixInverse(nullptr, pipeline.View);
    pipeline.InverseProjection = XMMatrixInverse(nullptr, pipeline.Projection);
    pipeline.ScreenResolution = { static_cast<float>(m_Width), static_cast<float>(m_Height) };
    pipeline.ScreenTexelSize = { 1.0f / pipeline.ScreenResolution.x, 1.0f / pipeline.ScreenResolution.y };
    return pipeline;
}

void RaytracingDemo::ResetAccumulation(bool resetDenoiserHistory)
{
    m_AccumulationFrameIndex = 0;
    if (resetDenoiserHistory)
    {
        m_Denoisers.ResetHistory();
    }
}

void RaytracingDemo::OnRender(RenderEventArgs& e)
{
    Base::OnRender(e);

    if (m_ImGui != nullptr)
    {
        m_ImGui->BeginFrame();
        OnImGui();
        m_ImGui->Render();
    }

    RenderGraph::RenderMetadata metadata;
    metadata.m_ScreenWidth = static_cast<uint32_t>(m_Width);
    metadata.m_ScreenHeight = static_cast<uint32_t>(m_Height);
    metadata.m_FrameIndex = m_FrameIndex;
    metadata.m_Time = e.TotalTime;

    const auto commandQueue = Application::Get().GetCommandQueue();
    const auto commandList = commandQueue->GetCommandList();
    m_Lights.Upload(*commandList);
    commandQueue->ExecuteCommandList(commandList);

    m_RenderGraph->Execute(metadata);
    m_RenderGraph->Present(PWindow);

    ++m_FrameIndex;
    if (m_AccumulationEnabled && !IsDenoiserEnabled())
    {
        ++m_AccumulationFrameIndex;
    }
    else
    {
        m_AccumulationFrameIndex = 0;
    }

    m_PreviousViewProjection = m_Camera.GetViewMatrix() * m_Camera.GetProjectionMatrix();
    m_HasPreviousViewProjection = true;
}
