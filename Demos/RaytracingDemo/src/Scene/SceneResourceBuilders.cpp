//Modify Begin:2026-08-21 by Hui
#include <Scene/SceneResourceBuilders.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/ResourceUploader.h>

#include <Framework/Geometry/Mesh.h>
#include <Framework/Geometry/Model.h>
#include <Framework/Rendering/Texture/TextureLoader.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace
{
    std::wstring ToWidePath(const std::filesystem::path& path)
    {
        return path.wstring();
    }

    std::wstring ToWideUtf8(const std::string& value)
    {
        const std::u8string utf8(
            reinterpret_cast<const char8_t*>(value.data()),
            reinterpret_cast<const char8_t*>(value.data() + value.size()));
        return std::filesystem::path(utf8).wstring();
    }
}

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
    m_TextureShaderResourceViews.clear();
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

    auto texture = std::make_shared<Texture>(
        TextureUsageType::Other,
        L"",
        commandList.GetDeviceContext());
    TextureLoader(commandList.GetDeviceContext()).Load(commandList, *texture, path, usage);
    const uint32_t textureIndex = m_BindlessDescriptorHeap.AddShaderResourceView(*texture);
    m_Textures.push_back(texture);
    m_TextureShaderResourceViews.emplace_back(texture);
    m_TextureIndices.emplace(cacheKey, textureIndex);
    return textureIndex;
}

uint32_t SceneTextureMaterialResources::AddTexture(
    CommandList& commandList,
    const SceneTextureBinding& binding,
    const TextureUsageType usage)
{
    if (binding.EmbeddedTexture == nullptr || !binding.EmbeddedTexture->IsValid())
    {
        if (binding.AssetPath.empty())
        {
            throw std::invalid_argument("Scene texture binding has neither an external path nor embedded data.");
        }
        return AddTexture(commandList, ToWidePath(binding.AssetPath), usage);
    }

    const std::wstring cacheKey = BuildTextureCacheKey(
        ToWideUtf8(binding.EmbeddedTexture->CacheKey),
        usage);
    if (const auto existing = m_TextureIndices.find(cacheKey); existing != m_TextureIndices.end())
    {
        return existing->second;
    }

    auto texture = std::make_shared<Texture>(
        TextureUsageType::Other,
        L"",
        commandList.GetDeviceContext());
    const TextureMemorySource source{
        binding.EmbeddedTexture->Data,
        ToWideUtf8(binding.EmbeddedTexture->CacheKey),
        binding.EmbeddedTexture->FormatHint,
        binding.EmbeddedTexture->Width,
        binding.EmbeddedTexture->Height,
        binding.EmbeddedTexture->Encoding == SceneEmbeddedTextureEncoding::Bgra8
    };
    TextureLoader(commandList.GetDeviceContext()).Load(commandList, *texture, source, usage);
    const uint32_t textureIndex = m_BindlessDescriptorHeap.AddShaderResourceView(*texture);
    m_Textures.push_back(texture);
    m_TextureShaderResourceViews.emplace_back(texture);
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
    ResourceUploader(commandList.GetDeviceContext()).UploadStructuredBuffer(
        commandList, m_MaterialBuffer, m_Materials);
}

void SceneTextureMaterialResources::ForEachBindlessTexture(
    const std::function<void(const Resource&)>& action) const
{
    for (const std::shared_ptr<Texture>& texture : m_Textures)
    {
        action(*texture);
    }
}

void SceneTextureMaterialResources::ForEachShaderResource(
    const std::function<void(const Resource&)>& action) const
{
    ForEachBindlessTexture(action);
    action(m_MaterialBuffer);
}

const std::vector<ShaderResourceView>& SceneTextureMaterialResources::GetTextureShaderResourceViews() const
{
    return m_TextureShaderResourceViews;
}

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

bool SceneGeometryResources::UpdateObjectWorldMatrix(
    const size_t objectIndex,
    const DirectX::XMMATRIX& worldMatrix)
{
    if (objectIndex >= m_Objects.size())
    {
        return false;
    }

    m_Objects[objectIndex].WorldMatrix = worldMatrix;
    return true;
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

SceneRayTracingResources::SceneRayTracingResources(std::shared_ptr<D3D12DeviceContext> deviceContext)
    : m_GeometryBuffer(L"Ray Tracing Geometry Data")
    , m_AccelerationStructure(std::move(deviceContext))
{
}

void SceneRayTracingResources::Clear()
{
    m_GeometryBuffer = StructuredBuffer(L"Ray Tracing Geometry Data");
    m_AccelerationStructure.ClearInstances();
    m_StressInstanceHandles.clear();
    m_SceneInstanceHandles.clear();
    m_SceneInstanceDescs.clear();
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
    m_SceneInstanceHandles.clear();
    m_SceneInstanceDescs.clear();
    m_SceneInstanceHandles.reserve(objects.size());
    m_SceneInstanceDescs.reserve(objects.size());
    for (size_t objectIndex = 0; objectIndex < objects.size(); ++objectIndex)
    {
        std::vector<RayTracingInstanceHandle> sceneInstanceHandles;
        std::vector<RayTracingInstanceDesc> sceneInstanceDescs;
        AddObjectInstances(
            geometries,
            objects[objectIndex],
            &sceneInstanceHandles,
            &sceneInstanceDescs);
        if (stressObjectsEnabled && stressObjectCount > 0 && objectIndex >= stressObjectStart)
        {
            m_StressInstanceHandles.insert(
                m_StressInstanceHandles.end(),
                sceneInstanceHandles.begin(),
                sceneInstanceHandles.end());
        }
        m_SceneInstanceHandles.push_back(std::move(sceneInstanceHandles));
        m_SceneInstanceDescs.push_back(std::move(sceneInstanceDescs));
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

bool SceneRayTracingResources::UpdateSceneObjectTransform(
    const size_t objectIndex,
    const DirectX::XMMATRIX& worldMatrix)
{
    if (objectIndex >= m_SceneInstanceHandles.size())
    {
        return false;
    }

    std::vector<RayTracingInstanceHandle>& handles = m_SceneInstanceHandles[objectIndex];
    std::vector<RayTracingInstanceDesc>& instanceDescs = m_SceneInstanceDescs[objectIndex];
    Assert(handles.size() == instanceDescs.size(), "Ray tracing scene instance handle/description counts must match.");
    for (size_t instanceIndex = 0; instanceIndex < handles.size(); ++instanceIndex)
    {
        instanceDescs[instanceIndex].Transform = worldMatrix;
        Assert(
            m_AccelerationStructure.UpdateInstance(handles[instanceIndex], instanceDescs[instanceIndex]),
            "Ray tracing scene instance transform update uses an invalid handle.");
    }
    return !handles.empty();
}

void SceneRayTracingResources::RefitDirtyGeometry(CommandList& commandList)
{
    m_AccelerationStructure.Update(commandList);
}

void SceneRayTracingResources::ForEachShaderResource(
    const std::function<void(const Resource&)>& action) const
{
    action(m_GeometryBuffer);
    for (const std::shared_ptr<Mesh>& mesh : m_AccelerationStructure.GetMeshes())
    {
        action(mesh->GetVertexBuffer());
        action(mesh->GetIndexBuffer());
    }
}

void SceneRayTracingResources::AddObjectInstances(
    const std::vector<RaytracingDemoSceneGeometry>& geometries,
    const RaytracingDemoSceneObject& object,
    std::vector<RayTracingInstanceHandle>* instanceHandles,
    std::vector<RayTracingInstanceDesc>* instanceDescs)
{
    Assert(object.GeometryIndex < geometries.size(), "Scene object geometry index is invalid.");
    const RaytracingDemoSceneGeometry& geometry = geometries[object.GeometryIndex];
    for (const std::shared_ptr<Mesh>& mesh : geometry.Model->GetMeshes())
    {
        const RayTracingInstanceDesc instanceDesc = {
            mesh,
            object.WorldMatrix,
            object.MaterialIndex
        };
        const RayTracingInstanceHandle handle = m_AccelerationStructure.AddInstance(instanceDesc);
        if (instanceHandles != nullptr)
        {
            instanceHandles->push_back(handle);
        }
        if (instanceDescs != nullptr)
        {
            instanceDescs->push_back(instanceDesc);
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

    ResourceUploader(commandList.GetDeviceContext()).UploadStructuredBuffer(
        commandList, m_GeometryBuffer, geometryData);
}
//Modify End
