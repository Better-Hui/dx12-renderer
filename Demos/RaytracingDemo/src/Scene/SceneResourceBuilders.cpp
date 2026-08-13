//Modify Begin:2026-07-30 by BestHui
#include <Scene/SceneResourceBuilders.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>

#include <Framework/Geometry/Mesh.h>
#include <Framework/Geometry/Model.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <system_error>
#include <utility>

SceneTextureMaterialResources::SceneTextureMaterialResources(Microsoft::WRL::ComPtr<ID3D12Device2> device)
    : m_MaterialBuffer(L"Ray Tracing Materials")
    , m_BindlessDescriptorHeap(*device.Get())
{
}

void SceneTextureMaterialResources::Clear()
{
    m_MaterialBuffer = StructuredBuffer(L"Ray Tracing Materials");
    m_BindlessDescriptorHeap.Reset();
    m_Materials.clear();
    m_Textures.clear();
//Modify Begin:2026-08-11 by BestHui
    m_TextureShaderResourceViews.clear();
//Modify End
    m_TextureIndices.clear();
}

uint32_t SceneTextureMaterialResources::AddTexture(
    CommandList& commandList,
    const std::wstring& path,
    const TextureUsageType usage)
{
    const std::wstring cacheKey = BuildTextureCacheKey(path, usage);
    if (const auto existing = m_TextureIndices.find(cacheKey); existing != m_TextureIndices.end())
    {
        return existing->second;
    }

//Modify Begin:2026-08-12 by BestHui
    auto texture = std::make_shared<Texture>(
        TextureUsageType::Other,
        L"",
        commandList.GetDeviceContext());
//Modify End
    commandList.LoadTextureFromFile(*texture, path, usage);
    const uint32_t textureIndex = m_BindlessDescriptorHeap.AddShaderResourceView(*texture);
    m_Textures.push_back(texture);
//Modify Begin:2026-08-11 by BestHui
    m_TextureShaderResourceViews.emplace_back(texture);
//Modify End
    m_TextureIndices.emplace(cacheKey, textureIndex);
    return textureIndex;
}

std::wstring SceneTextureMaterialResources::BuildTextureCacheKey(
    const std::wstring& path,
    const TextureUsageType usage) const
{
    std::error_code errorCode;
    std::filesystem::path normalizedPath = std::filesystem::weakly_canonical(std::filesystem::path(path), errorCode);
    if (errorCode)
    {
        normalizedPath = std::filesystem::path(path).lexically_normal();
    }

    std::wstring key = normalizedPath.native();
    std::transform(
        key.begin(),
        key.end(),
        key.begin(),
        [](const wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
    return std::to_wstring(static_cast<uint32_t>(usage)) + L":" + key;
}

uint32_t SceneTextureMaterialResources::AddMaterial(const RaytracingDemoMaterialData& material)
{
    m_Materials.push_back(material);
    return static_cast<uint32_t>(m_Materials.size() - 1);
}

uint32_t SceneTextureMaterialResources::AddPbrMaterial(
    const DirectX::XMFLOAT4& diffuse,
    const DirectX::XMFLOAT4& tilingOffset,
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
    const bool hasAmbientOcclusionMap,
    const DirectX::XMFLOAT4& emission,
    const uint32_t emissionTextureIndex,
    const bool hasEmissionMap)
{
    RaytracingDemoMaterialData material{};
    material.Diffuse = diffuse;
    material.Specular = { 0.04f, 0.04f, 0.04f, 1.0f };
    material.TilingOffset = tilingOffset;
    material.DiffuseTextureIndex = diffuseTextureIndex;
    material.NormalTextureIndex = normalTextureIndex;
    material.MetallicTextureIndex = metallicTextureIndex;
    material.RoughnessTextureIndex = roughnessTextureIndex;
    material.AmbientOcclusionTextureIndex = ambientOcclusionTextureIndex;
    material.Emission = emission;
    material.EmissionTextureIndex = emissionTextureIndex;
    material.HasDiffuseMap = hasDiffuseMap ? 1u : 0u;
    material.HasNormalMap = hasNormalMap ? 1u : 0u;
    material.HasMetallicMap = hasMetallicMap ? 1u : 0u;
    material.HasRoughnessMap = hasRoughnessMap ? 1u : 0u;
    material.HasAmbientOcclusionMap = hasAmbientOcclusionMap ? 1u : 0u;
    material.HasEmissionMap = hasEmissionMap ? 1u : 0u;
    material.Metallic = metallic;
    material.Roughness = roughness;
    return AddMaterial(material);
}

uint32_t SceneTextureMaterialResources::AddDiffuseMaterial(
    const DirectX::XMFLOAT4& diffuse,
    const DirectX::XMFLOAT4& tilingOffset,
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
        metallic, roughness, true, false, false, false, false,
        { 0.0f, 0.0f, 0.0f, 1.0f }, diffuseTextureIndex, false);
}

void SceneTextureMaterialResources::UploadMaterialBuffer(CommandList& commandList)
{
    commandList.CopyStructuredBuffer(m_MaterialBuffer, m_Materials);
}

void SceneTextureMaterialResources::TransitionTextures(
    CommandList& commandList,
    const D3D12_RESOURCE_STATES stateAfter) const
{
    for (const std::shared_ptr<Texture>& texture : m_Textures)
    {
        commandList.TransitionBarrier(*texture, stateAfter);
    }
}

//Modify Begin:2026-08-11 by BestHui
const std::vector<ShaderResourceView>& SceneTextureMaterialResources::GetTextureShaderResourceViews() const
{
    return m_TextureShaderResourceViews;
}
//Modify End

void SceneGeometryResources::Clear()
{
    m_Geometries.clear();
    m_Objects.clear();
}

uint32_t SceneGeometryResources::AddGeometry(
    const std::shared_ptr<Model>& model,
    std::vector<MeshPrototype> prototypes)
{
    Assert(model != nullptr, "Scene geometry model must not be null.");
    const uint32_t geometryIndex = static_cast<uint32_t>(m_Geometries.size());
    m_Geometries.push_back({ model, std::move(prototypes) });
    return geometryIndex;
}

void SceneGeometryResources::AddObject(
    const DirectX::XMMATRIX& worldMatrix,
    const uint32_t geometryIndex,
    const uint32_t materialIndex)
{
    Assert(geometryIndex < m_Geometries.size(), "Scene object geometry index is invalid.");
    m_Objects.push_back({ worldMatrix, geometryIndex, materialIndex });
}

void SceneGeometryResources::AppendObjects(const std::span<const RaytracingDemoSceneObject> objects)
{
    m_Objects.insert(m_Objects.end(), objects.begin(), objects.end());
}

void SceneGeometryResources::ResizeObjects(const size_t count)
{
    Assert(count <= m_Objects.size(), "Scene object resize cannot grow without object data.");
    m_Objects.resize(count);
}

void SceneMeshletResources::Clear()
{
    m_Resources.Clear();
    m_StressInstanceHandles.clear();
}

void SceneMeshletResources::Initialize(
    const std::vector<RaytracingDemoSceneGeometry>& geometries,
    const std::vector<RaytracingDemoSceneObject>& objects,
    const size_t stressObjectStart,
    const size_t stressObjectCount,
    const bool stressObjectsEnabled)
{
    std::vector<MeshletSceneGeometrySource> meshletGeometries;
    meshletGeometries.reserve(geometries.size());
    for (const RaytracingDemoSceneGeometry& geometry : geometries)
    {
        meshletGeometries.push_back({ &geometry.MeshPrototypes });
    }

    m_Resources.Clear();
    m_Resources.InitializeGeometries(meshletGeometries);
    m_StressInstanceHandles.clear();
    for (size_t objectIndex = 0; objectIndex < objects.size(); ++objectIndex)
    {
        const RaytracingDemoSceneObject& object = objects[objectIndex];
        const MeshletSceneInstanceHandle handle = m_Resources.AddInstance(
            { object.WorldMatrix, object.GeometryIndex, object.MaterialIndex });
        if (stressObjectsEnabled && stressObjectCount > 0 && objectIndex >= stressObjectStart)
        {
            m_StressInstanceHandles.push_back(handle);
        }
    }
}

void SceneMeshletResources::AddStressInstances(const std::span<const RaytracingDemoSceneObject> objects)
{
    for (const RaytracingDemoSceneObject& object : objects)
    {
        m_StressInstanceHandles.push_back(m_Resources.AddInstance(
            { object.WorldMatrix, object.GeometryIndex, object.MaterialIndex }));
    }
}

void SceneMeshletResources::RemoveStressInstances()
{
    m_Resources.RemoveInstances(m_StressInstanceHandles);
    m_StressInstanceHandles.clear();
}

void SceneMeshletResources::Upload(CommandList& commandList)
{
    m_Resources.Upload(commandList);
}

//Modify Begin:2026-07-30 by BestHui
SceneRayTracingResources::SceneRayTracingResources(std::shared_ptr<D3D12DeviceContext> deviceContext)
    : m_GeometryBuffer(L"Ray Tracing Geometry Data")
    , m_AccelerationStructure(std::move(deviceContext))
{
}
//Modify End

void SceneRayTracingResources::Clear()
{
    m_GeometryBuffer = StructuredBuffer(L"Ray Tracing Geometry Data");
    m_AccelerationStructure.ClearInstances();
    m_StressInstanceHandles.clear();
}

void SceneRayTracingResources::Build(
    CommandList& commandList,
    const std::vector<RaytracingDemoSceneGeometry>& geometries,
    const std::vector<RaytracingDemoSceneObject>& objects,
    const size_t stressObjectStart,
    const size_t stressObjectCount,
    const bool stressObjectsEnabled,
    BindlessDescriptorHeap& bindlessDescriptorHeap,
    const RayTracingAccelerationStructureBuildSettings settings)
{
    m_AccelerationStructure.ClearInstances();
    m_StressInstanceHandles.clear();
    for (size_t objectIndex = 0; objectIndex < objects.size(); ++objectIndex)
    {
        std::vector<RayTracingInstanceHandle>* instanceHandles =
            stressObjectsEnabled && stressObjectCount > 0 && objectIndex >= stressObjectStart
            ? &m_StressInstanceHandles
            : nullptr;
        AddObjectInstances(geometries, objects[objectIndex], instanceHandles);
    }

    m_AccelerationStructure.Build(commandList, settings);
    UploadGeometryBuffer(commandList, bindlessDescriptorHeap);
}

void SceneRayTracingResources::AddStressInstances(
    const std::vector<RaytracingDemoSceneGeometry>& geometries,
    const std::span<const RaytracingDemoSceneObject> objects)
{
    for (const RaytracingDemoSceneObject& object : objects)
    {
        AddObjectInstances(geometries, object, &m_StressInstanceHandles);
    }
}

void SceneRayTracingResources::RemoveStressInstances()
{
    m_AccelerationStructure.RemoveInstances(m_StressInstanceHandles);
    m_StressInstanceHandles.clear();
}

void SceneRayTracingResources::Update(
    CommandList& commandList,
    BindlessDescriptorHeap& bindlessDescriptorHeap)
{
    m_AccelerationStructure.Update(commandList);
    UploadGeometryBuffer(commandList, bindlessDescriptorHeap);
}

void SceneRayTracingResources::TransitionShaderResources(
    CommandList& commandList,
    const D3D12_RESOURCE_STATES stateAfter) const
{
    for (const std::shared_ptr<Mesh>& mesh : m_AccelerationStructure.GetMeshes())
    {
        commandList.TransitionBarrier(mesh->GetVertexBuffer(), stateAfter);
        commandList.TransitionBarrier(mesh->GetIndexBuffer(), stateAfter);
    }
}

void SceneRayTracingResources::AddObjectInstances(
    const std::vector<RaytracingDemoSceneGeometry>& geometries,
    const RaytracingDemoSceneObject& object,
    std::vector<RayTracingInstanceHandle>* instanceHandles)
{
    Assert(object.GeometryIndex < geometries.size(), "Scene object geometry index is invalid.");
    const RaytracingDemoSceneGeometry& geometry = geometries[object.GeometryIndex];
    for (const std::shared_ptr<Mesh>& mesh : geometry.Model->GetMeshes())
    {
        const RayTracingInstanceHandle handle = m_AccelerationStructure.AddInstance({
            mesh,
            object.WorldMatrix,
            object.MaterialIndex
        });
        if (instanceHandles != nullptr)
        {
            instanceHandles->push_back(handle);
        }
    }
}

void SceneRayTracingResources::UploadGeometryBuffer(
    CommandList& commandList,
    BindlessDescriptorHeap& bindlessDescriptorHeap)
{
    std::vector<RayTracingGeometryData> geometryData = m_AccelerationStructure.GetGeometryData();
    const std::vector<std::shared_ptr<Mesh>>& meshes = m_AccelerationStructure.GetMeshes();
    std::vector<uint32_t> vertexBufferDescriptorIndices(meshes.size());
    std::vector<uint32_t> indexBufferDescriptorIndices(meshes.size());

    for (uint32_t meshIndex = 0; meshIndex < meshes.size(); ++meshIndex)
    {
        const Mesh& mesh = *meshes[meshIndex];
        vertexBufferDescriptorIndices[meshIndex] = bindlessDescriptorHeap.AddShaderResourceView(mesh.GetVertexBuffer());
        indexBufferDescriptorIndices[meshIndex] = bindlessDescriptorHeap.AddShaderResourceView(mesh.GetIndexBuffer());
    }

    for (RayTracingGeometryData& geometry : geometryData)
    {
        Assert(geometry.VertexBufferIndex < vertexBufferDescriptorIndices.size(), "Ray tracing vertex buffer descriptor index is invalid.");
        Assert(geometry.IndexBufferIndex < indexBufferDescriptorIndices.size(), "Ray tracing index buffer descriptor index is invalid.");
        geometry.VertexBufferIndex = vertexBufferDescriptorIndices[geometry.VertexBufferIndex];
        geometry.IndexBufferIndex = indexBufferDescriptorIndices[geometry.IndexBufferIndex];
    }

    commandList.CopyStructuredBuffer(m_GeometryBuffer, geometryData);
}
//Modify End
