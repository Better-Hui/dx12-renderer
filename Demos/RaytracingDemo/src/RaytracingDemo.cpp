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
    , m_MaterialBuffer(L"Ray Tracing Materials")
    , m_GeometryBuffer(L"Ray Tracing Geometry Data")
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

    LoadDeferredLightingScene(*commandList);

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

    AddRaytracingInstances();

    EnsureRayTracingPipelines();

    RayTracingAccelerationStructureBuildSettings accelerationStructureSettings{};
    accelerationStructureSettings.AllowUpdate = true;
    m_RayTracingAccelerationStructure.Build(*commandList, accelerationStructureSettings);
    commandList->CopyStructuredBuffer(m_MaterialBuffer, m_Materials);
    commandList->CopyStructuredBuffer(m_GeometryBuffer, m_RayTracingAccelerationStructure.GetGeometryData());
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
    m_SceneObjects.clear();
    m_Materials.clear();
    m_Textures.clear();
}

RaytracingDemo::RayTracingSceneResourceLayout RaytracingDemo::BuildRayTracingSceneResourceLayout() const
{
    const std::vector<std::shared_ptr<Mesh>>& rayTracingMeshes = m_RayTracingAccelerationStructure.GetMeshes();

    RayTracingSceneResourceLayout layout;
    layout.TextureDescriptorCapacity = ComputeDescriptorArrayCapacity(m_Textures.size(), m_Textures.capacity());
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
    shader.SetBuffer("Materials", m_MaterialBuffer);
    shader.SetBuffer("Geometries", m_GeometryBuffer);
    m_Lights.BindRayTracingResources(shader);
    for (uint32_t textureIndex = 0; textureIndex < m_Textures.size(); ++textureIndex)
    {
        shader.SetTexture("Textures", textureIndex, ShaderResourceView(m_Textures[textureIndex]));
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

uint32_t RaytracingDemo::AddTexture(CommandList& commandList, const std::wstring& path, TextureUsageType usage)
{
    auto texture = std::make_shared<Texture>();
    commandList.LoadTextureFromFile(*texture, path, usage);
    m_Textures.push_back(texture);
    return static_cast<uint32_t>(m_Textures.size() - 1);
}

uint32_t RaytracingDemo::AddMaterial(const MaterialData& material)
{
    m_Materials.push_back(material);
    return static_cast<uint32_t>(m_Materials.size() - 1);
}

uint32_t RaytracingDemo::AddPbrMaterial(
    const XMFLOAT4& diffuse,
    const XMFLOAT4& tilingOffset,
    const uint32_t diffuseTextureIndex,
    const uint32_t normalTextureIndex,
    const uint32_t metallicTextureIndex,
    const uint32_t roughnessTextureIndex,
    const uint32_t ambientOcclusionTextureIndex,
    const float metallic,
    const float roughness,
    const bool hasDiffuseMap,
    const bool hasNormalMap,
    const bool hasMetallicMap,
    const bool hasRoughnessMap,
    const bool hasAmbientOcclusionMap)
{
    MaterialData material{};
    material.Diffuse = diffuse;
    material.Specular = { 0.04f, 0.04f, 0.04f, 1.0f };
    material.TilingOffset = tilingOffset;
    material.DiffuseTextureIndex = diffuseTextureIndex;
    material.NormalTextureIndex = normalTextureIndex;
    material.MetallicTextureIndex = metallicTextureIndex;
    material.RoughnessTextureIndex = roughnessTextureIndex;
    material.AmbientOcclusionTextureIndex = ambientOcclusionTextureIndex;
    material.HasDiffuseMap = hasDiffuseMap ? 1u : 0u;
    material.HasNormalMap = hasNormalMap ? 1u : 0u;
    material.HasMetallicMap = hasMetallicMap ? 1u : 0u;
    material.HasRoughnessMap = hasRoughnessMap ? 1u : 0u;
    material.HasAmbientOcclusionMap = hasAmbientOcclusionMap ? 1u : 0u;
    material.Metallic = metallic;
    material.Roughness = roughness;
    return AddMaterial(material);
}

uint32_t RaytracingDemo::AddDiffuseMaterial(
    const XMFLOAT4& diffuse,
    const XMFLOAT4& tilingOffset,
    const uint32_t diffuseTextureIndex,
    const float metallic,
    const float roughness)
{
    return AddPbrMaterial(
        diffuse,
        tilingOffset,
        diffuseTextureIndex,
        diffuseTextureIndex,
        diffuseTextureIndex,
        diffuseTextureIndex,
        diffuseTextureIndex,
        metallic,
        roughness,
        true);
}

void RaytracingDemo::LoadDeferredLightingScene(CommandList& commandList)
{
    ModelLoader modelLoader;

    const uint32_t whiteTexture = AddTexture(commandList, L"Assets/Textures/white.png");
    const uint32_t groundTexture = AddTexture(commandList, L"Assets/Textures/Ground047/Ground047_1K_Color.jpg");
    const uint32_t groundNormalTexture = AddTexture(commandList, L"Assets/Textures/Ground047/Ground047_1K_NormalDX.jpg", TextureUsageType::Normalmap);
    const uint32_t groundRoughnessTexture = AddTexture(commandList, L"Assets/Textures/Ground047/Ground047_1K_Roughness.jpg", TextureUsageType::Other);
    const uint32_t groundAoTexture = AddTexture(commandList, L"Assets/Textures/Ground047/Ground047_1K_AmbientOcclusion.jpg", TextureUsageType::Other);
    const uint32_t chestTexture = AddTexture(commandList, L"Assets/Models/old-wooden-chest/chest_01_BaseColor.png");
    const uint32_t chestNormalTexture = AddTexture(commandList, L"Assets/Models/old-wooden-chest/chest_01_Normal.png", TextureUsageType::Normalmap);
    const uint32_t chestMetallicTexture = AddTexture(commandList, L"Assets/Models/old-wooden-chest/chest_01_Metallic.png", TextureUsageType::Other);
    const uint32_t chestRoughnessTexture = AddTexture(commandList, L"Assets/Models/old-wooden-chest/chest_01_Roughness.png", TextureUsageType::Other);
    const uint32_t cerberusTexture = AddTexture(commandList, L"Assets/Models/cerberus/Cerberus_A.jpg");
    const uint32_t cerberusNormalTexture = AddTexture(commandList, L"Assets/Models/cerberus/Cerberus_N.jpg", TextureUsageType::Normalmap);
    const uint32_t cerberusMetallicTexture = AddTexture(commandList, L"Assets/Models/cerberus/Cerberus_M.jpg", TextureUsageType::Other);
    const uint32_t cerberusRoughnessTexture = AddTexture(commandList, L"Assets/Models/cerberus/Cerberus_R.jpg", TextureUsageType::Other);
    const uint32_t tvTexture = AddTexture(commandList, L"Assets/Models/tv/TV_Color.jpg");
    const uint32_t tvNormalTexture = AddTexture(commandList, L"Assets/Models/tv/TV_Normal.jpg", TextureUsageType::Normalmap);
    const uint32_t tvMetallicTexture = AddTexture(commandList, L"Assets/Models/tv/TV_Metallic.jpg", TextureUsageType::Other);
    const uint32_t tvRoughnessTexture = AddTexture(commandList, L"Assets/Models/tv/TV_Roughness.jpg", TextureUsageType::Other);
    const uint32_t tvAoTexture = AddTexture(commandList, L"Assets/Models/tv/TV_Occlusion.jpg", TextureUsageType::Other);

    const uint32_t groundMaterial = AddPbrMaterial({ 1, 1, 1, 1 }, { 6, 6, 0, 0 }, groundTexture, groundNormalTexture, whiteTexture, groundRoughnessTexture, groundAoTexture, 0.0f, 1.0f, true, true, false, true, true);
    const uint32_t chestMaterial = AddPbrMaterial({ 1, 1, 1, 1 }, { 1, 1, 0, 0 }, chestTexture, chestNormalTexture, chestMetallicTexture, chestRoughnessTexture, whiteTexture, 1.0f, 1.0f, true, true, true, true, false);
    const uint32_t mirrorMaterial = AddDiffuseMaterial({ 0.85f, 0.85f, 0.92f, 1 }, { 1, 1, 0, 0 }, whiteTexture, 1.0f, 0.08f);
    const uint32_t cubeMaterial = AddDiffuseMaterial({ 0.9f, 0.9f, 0.9f, 1 }, { 1, 1, 0, 0 }, whiteTexture, 0.0f, 0.35f);
    const uint32_t cerberusMaterial = AddPbrMaterial({ 1, 1, 1, 1 }, { 1, 1, 0, 0 }, cerberusTexture, cerberusNormalTexture, cerberusMetallicTexture, cerberusRoughnessTexture, whiteTexture, 1.0f, 1.0f, true, true, true, true, false);
    const uint32_t tvMaterial = AddPbrMaterial({ 1, 1, 1, 1 }, { 1, 1, 0, 0 }, tvTexture, tvNormalTexture, tvMetallicTexture, tvRoughnessTexture, tvAoTexture, 1.0f, 1.0f, true, true, true, true, true);

    {
        auto model = modelLoader.LoadExisting(Mesh::CreatePlane(commandList));
        XMMATRIX worldMatrix = XMMatrixScaling(200.0f, 200.0f, 200.0f);
        m_SceneObjects.push_back({ worldMatrix, model, groundMaterial });
    }

    {
        auto model = modelLoader.Load(commandList, "Assets/Models/old-wooden-chest/chest_01.fbx");

        XMMATRIX worldMatrix =
            XMMatrixScaling(0.01f, 0.01f, 0.01f) *
            XMMatrixRotationRollPitchYaw(XMConvertToRadians(90.0f), 0.0f, 0.0f) *
            XMMatrixTranslation(0.0f, 0.25f, 15.0f);
        m_SceneObjects.push_back({ worldMatrix, model, chestMaterial });

        worldMatrix =
            XMMatrixScaling(0.01f, 0.01f, 0.01f) *
            XMMatrixRotationRollPitchYaw(XMConvertToRadians(90.0f), 0.0f, 0.0f) *
            XMMatrixTranslation(-50.0f, 0.25f, 15.0f);
        m_SceneObjects.push_back({ worldMatrix, model, chestMaterial });
    }

    {
        auto model = modelLoader.LoadExisting(Mesh::CreatePlane(commandList));
        XMMATRIX worldMatrix = XMMatrixScaling(30.0f, 30.0f, 30.0f) * XMMatrixTranslation(-50.0f, 0.1f, 15.0f);
        m_SceneObjects.push_back({ worldMatrix, model, mirrorMaterial });
    }

    {
        auto model = modelLoader.LoadExisting(Mesh::CreateCube(commandList));
        XMMATRIX worldMatrix = XMMatrixScaling(5.0f, 5.0f, 5.0f) * XMMatrixTranslation(-54.0f, 2.5f, 7.0f);
        m_SceneObjects.push_back({ worldMatrix, model, cubeMaterial });
    }

    {
        auto model = modelLoader.Load(commandList, "Assets/Models/cerberus/Cerberus_LP.FBX");
        XMMATRIX worldMatrix =
            XMMatrixScaling(0.10f, 0.10f, 0.10f) *
            XMMatrixRotationRollPitchYaw(XMConvertToRadians(90.0f), XMConvertToRadians(135.0f), 0.0f) *
            XMMatrixTranslation(15.0f, 5.0f, 10.0f);
        m_SceneObjects.push_back({ worldMatrix, model, cerberusMaterial });
    }

    {
        auto model = modelLoader.Load(commandList, "Assets/Models/tv/TV.FBX");
        XMMATRIX worldMatrix =
            XMMatrixScaling(0.30f, 0.30f, 0.30f) *
            XMMatrixRotationRollPitchYaw(XMConvertToRadians(90.0f), XMConvertToRadians(-45.0f), 0.0f) *
            XMMatrixTranslation(-14.0f, 0.0f, 18.0f);
        m_SceneObjects.push_back({ worldMatrix, model, tvMaterial });
    }

    const int steps = 5;
    for (int x = 0; x < steps; ++x)
    {
        for (int y = 0; y < steps; ++y)
        {
            const float metallic = static_cast<float>(x) / static_cast<float>(steps - 1);
            const float roughness = static_cast<float>(y) / static_cast<float>(steps - 1);
            const XMFLOAT4 color = {
                0.25f + metallic * 0.75f,
                0.25f + roughness * 0.75f,
                0.9f - roughness * 0.45f,
                1.0f
            };

            const uint32_t material = AddDiffuseMaterial(color, { 1, 1, 0, 0 }, whiteTexture, metallic, roughness);
            auto model = modelLoader.LoadExisting(Mesh::CreateSphere(commandList));
            XMMATRIX worldMatrix = XMMatrixTranslation(x * 1.5f, 5.0f + y * 2.0f, 25.0f);
            m_SceneObjects.push_back({ worldMatrix, model, material });
        }
    }

    m_Lights.CreateDemoLights();
}

void RaytracingDemo::AddRaytracingInstances()
{
    m_RayTracingAccelerationStructure.ClearInstances();

    for (const SceneObject& object : m_SceneObjects)
    {
        for (const auto& mesh : object.Model->GetMeshes())
        {
            m_RayTracingAccelerationStructure.AddInstance({
                mesh,
                object.WorldMatrix,
                object.MaterialIndex
            });
        }
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
