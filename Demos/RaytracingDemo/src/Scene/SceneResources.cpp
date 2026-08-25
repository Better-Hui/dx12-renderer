//Modify Begin:2026-08-21 by Hui
#include <Scene/SceneResources.h>
#include <Scene/SceneLightManager.h>
#include <Scene/SceneStressTestFactory.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/D3D12DeviceContext.h>
#include <DX12Library/Helpers.h>
#include <Framework/Geometry/Mesh.h>
#include <Framework/Geometry/Meshlet.h>
#include <Framework/Geometry/Model.h>
#include <Framework/Geometry/ModelLoader.h>
#include <Framework/Rendering/RayTracing/RayTracingAccelerationStructure.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <ranges>
#include <stdexcept>
#include <unordered_map>
#include <utility>

using namespace DirectX;

namespace
{
    std::string ToLower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
        return value;
    }

    std::wstring ToWidePath(const std::filesystem::path& path)
    {
        return path.wstring();
    }

    std::string ToUtf8Path(const std::filesystem::path& path)
    {
        const std::u8string value = path.u8string();
        return { reinterpret_cast<const char*>(value.data()), value.size() };
    }

    RaytracingDemoMaterialData MakeSceneMaterial(
        const SceneMaterial& material,
        const uint32_t diffuseTextureIndex,
        const uint32_t normalTextureIndex,
        const uint32_t metallicTextureIndex,
        const uint32_t roughnessTextureIndex,
        const uint32_t ambientOcclusionTextureIndex,
        const uint32_t emissionTextureIndex)
    {
        RaytracingDemoMaterialData output{};
        output.Diffuse = material.BaseColor;
        output.Specular = material.SpecColor;
        output.Emission = material.EmissionColor;
        output.TilingOffset = material.BaseMap.ScaleOffset;
        output.DiffuseTextureIndex = diffuseTextureIndex;
        output.NormalTextureIndex = normalTextureIndex;
        output.MetallicTextureIndex = metallicTextureIndex;
        output.RoughnessTextureIndex = roughnessTextureIndex;
        output.AmbientOcclusionTextureIndex = ambientOcclusionTextureIndex;
        output.EmissionTextureIndex = emissionTextureIndex;
        output.HasDiffuseMap = material.BaseMap.IsValid() ? 1u : 0u;
        output.HasNormalMap = material.NormalMap.IsValid() ? 1u : 0u;
        output.HasMetallicMap = (material.MetallicMap.IsValid() || material.MetallicGlossMap.IsValid()) ? 1u : 0u;
        output.HasRoughnessMap = (material.RoughnessMap.IsValid() || material.MetallicGlossMap.IsValid()) ? 1u : 0u;
        output.HasAmbientOcclusionMap = material.OcclusionMap.IsValid() ? 1u : 0u;
        output.HasEmissionMap = material.EmissionMap.IsValid() ? 1u : 0u;
        output.Metallic = material.Metallic;
        output.Roughness = std::clamp(material.Roughness, 0.02f, 1.0f);
        return output;
    }

    MeshPrototype CreateBuiltinPlanePrototype(const float width, const float height)
    {
        return Mesh::CreatePlanePrototype(width, height);
    }

    MeshPrototype CreateBuiltinCubePrototype(const float size)
    {
        return Mesh::CreateCubePrototype(size);
    }

    bool IsDynamicSceneAutomationEmitterEnabled()
    {
        char* automationMode = nullptr;
        size_t automationModeLength = 0;
        _dupenv_s(&automationMode, &automationModeLength, "RAYTRACING_DEMO_AUTOTEST");
        const bool enabled = automationMode != nullptr && std::strcmp(automationMode, "dynamic-scene") == 0;
        std::free(automationMode);
        return enabled;
    }

}

RaytracingDemoSceneResources::RaytracingDemoSceneResources(std::shared_ptr<D3D12DeviceContext> deviceContext)
    : m_TextureMaterialResources(deviceContext->GetDevice())
    , m_RayTracingResources(std::move(deviceContext))
{
}

void RaytracingDemoSceneResources::Clear()
{
    m_TextureMaterialResources.Clear();
    m_GeometryResources.Clear();
    m_MeshletResources.Clear();
    m_RayTracingResources.Clear();
    m_DynamicRayTracingUpdatesEnabled = false;
    m_DynamicRayTracingRestorePending = false;
    m_DynamicRayTracingUpdatePending = false;
    m_DynamicRayTracingObjectIndex = (std::numeric_limits<size_t>::max)();
    m_DynamicRayTracingGeometryIndex = (std::numeric_limits<uint32_t>::max)();
    m_DynamicRayTracingPrototypeIndex = (std::numeric_limits<uint32_t>::max)();
    m_DynamicRayTracingMesh.reset();
    m_DynamicRayTracingEmitterActive = false;
    m_DynamicRayTracingBaseVertices.clear();
    m_DynamicRayTracingVertices.clear();
    m_DynamicRayTracingUpdateStatistics = {};
    m_StressTestSphereObjects.clear();
    m_StressTestSphereObjectStart = 0;
    m_StressTestSphereMaterialIndex = (std::numeric_limits<uint32_t>::max)();
    m_StressTestSpheresEnabled = false;
}

uint32_t RaytracingDemoSceneResources::AddTexture(CommandList& commandList, const std::wstring& path, TextureUsageType usage)
{
    return m_TextureMaterialResources.AddTexture(commandList, path, usage);
}

uint32_t RaytracingDemoSceneResources::AddMaterial(const RaytracingDemoMaterialData& material)
{
    return m_TextureMaterialResources.AddMaterial(material);
}

uint32_t RaytracingDemoSceneResources::AddPbrMaterial(
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
    const bool hasAmbientOcclusionMap,
    const XMFLOAT4& emission,
    const uint32_t emissionTextureIndex,
    const bool hasEmissionMap)
{
    return m_TextureMaterialResources.AddPbrMaterial(
        diffuse,
        tilingOffset,
        diffuseTextureIndex,
        normalTextureIndex,
        metallicTextureIndex,
        roughnessTextureIndex,
        ambientOcclusionTextureIndex,
        metallic,
        roughness,
        hasDiffuseMap,
        hasNormalMap,
        hasMetallicMap,
        hasRoughnessMap,
        hasAmbientOcclusionMap,
        emission,
        emissionTextureIndex,
        hasEmissionMap);
}

uint32_t RaytracingDemoSceneResources::AddDiffuseMaterial(
    const XMFLOAT4& diffuse,
    const XMFLOAT4& tilingOffset,
    const uint32_t diffuseTextureIndex,
    const float metallic,
    const float roughness)
{
    return m_TextureMaterialResources.AddDiffuseMaterial(
        diffuse,
        tilingOffset,
        diffuseTextureIndex,
        metallic,
        roughness);
}

void RaytracingDemoSceneResources::ForEachGBufferShaderResource(
    const std::function<void(const Resource&)>& action) const
{
    m_TextureMaterialResources.ForEachShaderResource(action);
}

void RaytracingDemoSceneResources::ForEachRayTracingShaderResource(
    const std::function<void(const Resource&)>& action) const
{
    m_TextureMaterialResources.ForEachShaderResource(action);
    m_RayTracingResources.ForEachShaderResource(action);
}

SurfaceEmitterSceneData RaytracingDemoSceneResources::CollectEmissiveMeshSurfaceEmitters() const
{
    constexpr float emissionThreshold = 1.0e-4f;
    constexpr float triangleAreaThreshold = 1.0e-8f;

    const std::vector<RaytracingDemoMaterialData>& materials = m_TextureMaterialResources.GetMaterials();
    const std::vector<RaytracingDemoSceneGeometry>& geometries = m_GeometryResources.GetGeometries();
    const std::vector<RaytracingDemoSceneObject>& objects = m_GeometryResources.GetObjects();
    SurfaceEmitterSceneData emitterData;
    std::unordered_map<uint32_t, uint32_t> geometryIndices;

    for (const RaytracingDemoSceneObject& object : objects)
    {
        if (object.MaterialIndex >= materials.size() || object.GeometryIndex >= geometries.size())
        {
            continue;
        }

        const RaytracingDemoMaterialData& material = materials[object.MaterialIndex];
        const float emissionLuminance = std::fmax(
            material.Emission.x,
            std::fmax(material.Emission.y, material.Emission.z));
        if (emissionLuminance <= emissionThreshold && material.HasEmissionMap == 0u)
        {
            continue;
        }

        uint32_t emitterGeometryIndex = 0;
        const auto geometryIt = geometryIndices.find(object.GeometryIndex);
        if (geometryIt == geometryIndices.end())
        {
            SurfaceEmitterGeometryData emitterGeometry{};
            emitterGeometry.TriangleOffset = static_cast<uint32_t>(emitterData.Triangles.size());
            emitterGeometry.TriangleCdfOffset = static_cast<uint32_t>(emitterData.TriangleCdf.size());
            float cumulativeArea = 0.0f;

            const RaytracingDemoSceneGeometry& geometry = geometries[object.GeometryIndex];
            for (const MeshPrototype& prototype : geometry.MeshPrototypes)
            {
                for (size_t index = 0; index + 2 < prototype.m_Indices.size(); index += 3)
                {
                    const uint32_t vertexIndex0 = prototype.m_Indices[index + 0];
                    const uint32_t vertexIndex1 = prototype.m_Indices[index + 1];
                    const uint32_t vertexIndex2 = prototype.m_Indices[index + 2];
                    if (vertexIndex0 >= prototype.m_Vertices.size() ||
                        vertexIndex1 >= prototype.m_Vertices.size() ||
                        vertexIndex2 >= prototype.m_Vertices.size())
                    {
                        continue;
                    }

                    const VertexAttributes& vertex0 = prototype.m_Vertices[vertexIndex0];
                    const VertexAttributes& vertex1 = prototype.m_Vertices[vertexIndex1];
                    const VertexAttributes& vertex2 = prototype.m_Vertices[vertexIndex2];
                    const XMVECTOR position0 = XMVectorSet(vertex0.Position.x, vertex0.Position.y, vertex0.Position.z, 0.0f);
                    const XMVECTOR position1 = XMVectorSet(vertex1.Position.x, vertex1.Position.y, vertex1.Position.z, 0.0f);
                    const XMVECTOR position2 = XMVectorSet(vertex2.Position.x, vertex2.Position.y, vertex2.Position.z, 0.0f);
                    const float triangleArea = 0.5f * XMVectorGetX(XMVector3Length(XMVector3Cross(
                        XMVectorSubtract(position1, position0),
                        XMVectorSubtract(position2, position0))));
                    if (triangleArea <= triangleAreaThreshold)
                    {
                        continue;
                    }

                    SurfaceEmitterTriangleData triangle{};
                    triangle.Position0 = { vertex0.Position.x, vertex0.Position.y, vertex0.Position.z, 0.0f };
                    triangle.Position1 = { vertex1.Position.x, vertex1.Position.y, vertex1.Position.z, 0.0f };
                    triangle.Position2 = { vertex2.Position.x, vertex2.Position.y, vertex2.Position.z, 0.0f };
                    triangle.Uv0Uv1 = { vertex0.Uv.x, vertex0.Uv.y, vertex1.Uv.x, vertex1.Uv.y };
                    triangle.Uv2AndPadding = { vertex2.Uv.x, vertex2.Uv.y, 0.0f, 0.0f };
                    emitterData.Triangles.push_back(triangle);
                    cumulativeArea += triangleArea;
                    emitterData.TriangleCdf.push_back(cumulativeArea);
                }
            }

            emitterGeometry.TriangleCount = static_cast<uint32_t>(emitterData.Triangles.size()) - emitterGeometry.TriangleOffset;
            if (emitterGeometry.TriangleCount == 0u)
            {
                continue;
            }

            for (uint32_t triangleIndex = 0; triangleIndex < emitterGeometry.TriangleCount; ++triangleIndex)
            {
                const uint32_t cdfIndex = emitterGeometry.TriangleCdfOffset + triangleIndex;
                emitterData.TriangleCdf[cdfIndex] /= cumulativeArea;
            }

            emitterGeometryIndex = static_cast<uint32_t>(emitterData.Geometries.size());
            emitterData.Geometries.push_back(emitterGeometry);
            geometryIndices.emplace(object.GeometryIndex, emitterGeometryIndex);
        }
        else
        {
            emitterGeometryIndex = geometryIt->second;
        }

        SurfaceEmitterInstanceData instance{};
        XMFLOAT3 origin{};
        XMFLOAT3 axisX{};
        XMFLOAT3 axisY{};
        XMFLOAT3 axisZ{};
        XMStoreFloat3(&origin, XMVector3TransformCoord(XMVectorZero(), object.WorldMatrix));
        XMStoreFloat3(&axisX, XMVector3TransformNormal(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), object.WorldMatrix));
        XMStoreFloat3(&axisY, XMVector3TransformNormal(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), object.WorldMatrix));
        XMStoreFloat3(&axisZ, XMVector3TransformNormal(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), object.WorldMatrix));
        instance.OriginAndRange = { origin.x, origin.y, origin.z, 10000.0f };
        instance.AxisX = { axisX.x, axisX.y, axisX.z, 0.0f };
        instance.AxisY = { axisY.x, axisY.y, axisY.z, 0.0f };
        instance.AxisZ = { axisZ.x, axisZ.y, axisZ.z, 0.0f };
        instance.EmissionAndIntensity = emissionLuminance > emissionThreshold
            ? material.Emission
            : XMFLOAT4{ 1.0f, 1.0f, 1.0f, 1.0f };
        instance.GeometryIndex = emitterGeometryIndex;
        instance.MaterialIndex = object.MaterialIndex;
        instance.Flags = SurfaceEmitterInstanceFlagUseMaterialEmission;
        emitterData.Instances.push_back(instance);
    }

    return emitterData;
}

void RaytracingDemoSceneResources::LoadDeferredLightingScene(CommandList& commandList)
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
        const XMMATRIX worldMatrix = XMMatrixScaling(200.0f, 200.0f, 200.0f);
        const uint32_t geometryIndex = AddSceneGeometry(model, std::vector<MeshPrototype>{ CreateBuiltinPlanePrototype(1.0f, 1.0f) });
        AddSceneObject(worldMatrix, geometryIndex, groundMaterial);
    }

    {
        const std::vector<MeshPrototype> chestPrototypes = modelLoader.LoadAsMeshPrototypes("Assets/Models/old-wooden-chest/chest_01.fbx");
        auto model = modelLoader.Load(commandList, chestPrototypes);
        const uint32_t geometryIndex = AddSceneGeometry(model, chestPrototypes);

        XMMATRIX worldMatrix =
            XMMatrixScaling(0.01f, 0.01f, 0.01f) *
            XMMatrixRotationRollPitchYaw(XMConvertToRadians(90.0f), 0.0f, 0.0f) *
            XMMatrixTranslation(0.0f, 0.25f, 15.0f);
        AddSceneObject(worldMatrix, geometryIndex, chestMaterial);

        worldMatrix =
            XMMatrixScaling(0.01f, 0.01f, 0.01f) *
            XMMatrixRotationRollPitchYaw(XMConvertToRadians(90.0f), 0.0f, 0.0f) *
            XMMatrixTranslation(-50.0f, 0.25f, 15.0f);
        AddSceneObject(worldMatrix, geometryIndex, chestMaterial);
    }

    {
        auto model = modelLoader.LoadExisting(Mesh::CreatePlane(commandList));
        const XMMATRIX worldMatrix = XMMatrixScaling(30.0f, 30.0f, 30.0f) * XMMatrixTranslation(-50.0f, 0.1f, 15.0f);
        const uint32_t geometryIndex = AddSceneGeometry(model, std::vector<MeshPrototype>{ CreateBuiltinPlanePrototype(1.0f, 1.0f) });
        AddSceneObject(worldMatrix, geometryIndex, mirrorMaterial);
    }

    {
        auto model = modelLoader.LoadExisting(Mesh::CreateCube(commandList));
        const XMMATRIX worldMatrix = XMMatrixScaling(5.0f, 5.0f, 5.0f) * XMMatrixTranslation(-54.0f, 2.5f, 7.0f);
        const uint32_t geometryIndex = AddSceneGeometry(model, std::vector<MeshPrototype>{ CreateBuiltinCubePrototype(1.0f) });
        AddSceneObject(worldMatrix, geometryIndex, cubeMaterial);
    }

    {
        const std::vector<MeshPrototype> cerberusPrototypes = modelLoader.LoadAsMeshPrototypes("Assets/Models/cerberus/Cerberus_LP.FBX");
        auto model = modelLoader.Load(commandList, cerberusPrototypes);
        const uint32_t geometryIndex = AddSceneGeometry(model, cerberusPrototypes);
        const XMMATRIX worldMatrix =
            XMMatrixScaling(0.10f, 0.10f, 0.10f) *
            XMMatrixRotationRollPitchYaw(XMConvertToRadians(90.0f), XMConvertToRadians(135.0f), 0.0f) *
            XMMatrixTranslation(15.0f, 5.0f, 10.0f);
        AddSceneObject(worldMatrix, geometryIndex, cerberusMaterial);
    }

    {
        const std::vector<MeshPrototype> tvPrototypes = modelLoader.LoadAsMeshPrototypes("Assets/Models/tv/TV.FBX");
        auto model = modelLoader.Load(commandList, tvPrototypes);
        const uint32_t geometryIndex = AddSceneGeometry(model, tvPrototypes);
        const XMMATRIX worldMatrix =
            XMMatrixScaling(0.30f, 0.30f, 0.30f) *
            XMMatrixRotationRollPitchYaw(XMConvertToRadians(90.0f), XMConvertToRadians(-45.0f), 0.0f) *
            XMMatrixTranslation(-14.0f, 0.0f, 18.0f);
        AddSceneObject(worldMatrix, geometryIndex, tvMaterial);
    }

    constexpr int MaterialSteps = 5;
    std::vector<uint32_t> sphereMaterials;
    sphereMaterials.reserve(MaterialSteps * MaterialSteps);
    for (int x = 0; x < MaterialSteps; ++x)
    {
        for (int y = 0; y < MaterialSteps; ++y)
        {
            const float metallic = static_cast<float>(x) / static_cast<float>(MaterialSteps - 1);
            const float roughness = static_cast<float>(y) / static_cast<float>(MaterialSteps - 1);
            const XMFLOAT4 color = {
                0.25f + metallic * 0.75f,
                0.25f + roughness * 0.75f,
                0.9f - roughness * 0.45f,
                1.0f
            };
            sphereMaterials.push_back(AddDiffuseMaterial(color, { 1, 1, 0, 0 }, whiteTexture, metallic, roughness));
        }
    }

    const MeshPrototype spherePrototype = SceneStressTestFactory::CreateSpherePrototype(1.0f, 12);
    auto sphereModel = modelLoader.Load(commandList, std::vector<MeshPrototype>{ spherePrototype });
    const uint32_t sphereGeometryIndex = AddSceneGeometry(sphereModel, std::vector<MeshPrototype>{ spherePrototype });

    constexpr int SphereColumns = 64;
    constexpr int SphereRows = 16;
    constexpr int SphereDepthLayers = 8;
    constexpr float SphereSpacingX = 0.80f;
    constexpr float SphereSpacingY = 0.82f;
    constexpr float SphereSpacingZ = 1.55f;
    constexpr float SphereRadius = 0.38f;
    constexpr float CenterX = 0.0f;
    constexpr float CenterY = 6.5f;
    constexpr float CenterZ = 27.0f;
    const float startX = -static_cast<float>(SphereColumns - 1) * SphereSpacingX * 0.5f;
    const float startY = -static_cast<float>(SphereRows - 1) * SphereSpacingY * 0.5f;
    const float startZ = -static_cast<float>(SphereDepthLayers - 1) * SphereSpacingZ * 0.5f;

    for (int z = 0; z < SphereDepthLayers; ++z)
    {
        for (int y = 0; y < SphereRows; ++y)
        {
            for (int x = 0; x < SphereColumns; ++x)
            {
                const size_t materialIndex = static_cast<size_t>((x % MaterialSteps) * MaterialSteps + (y % MaterialSteps));
                const float wave = std::sin(static_cast<float>(x) * 0.29f + static_cast<float>(y) * 0.47f + static_cast<float>(z) * 0.71f);
                const float stagger = ((y + z) & 1) != 0 ? SphereSpacingX * 0.35f : 0.0f;
                const XMMATRIX worldMatrix =
                    XMMatrixScaling(SphereRadius, SphereRadius, SphereRadius) *
                    XMMatrixTranslation(
                        CenterX + startX + static_cast<float>(x) * SphereSpacingX + stagger,
                        CenterY + startY + static_cast<float>(y) * SphereSpacingY + wave * 0.15f,
                        CenterZ + startZ + static_cast<float>(z) * SphereSpacingZ);
                const uint32_t material = sphereMaterials[materialIndex];

                AddSceneObject(worldMatrix, sphereGeometryIndex, material);
            }
        }
    }

    AddDynamicSceneAutomationEmitter(commandList, whiteTexture);
    InitializeMeshletSceneResources();
    UploadMeshletBuffers(commandList);
}

bool RaytracingDemoSceneResources::LoadScene(
    CommandList& commandList,
    const Scene& scene,
    const bool enableStressTestSpheres)
{
    if (scene.GetObjects().empty())
    {
        return false;
    }

    const uint32_t whiteTexture = AddTexture(commandList, L"Assets/Textures/white.png");
    const std::vector<uint32_t> materialIndexMap = LoadSceneMaterials(commandList, scene, whiteTexture);
    const uint32_t defaultMaterial = AddDiffuseMaterial({ 0.85f, 0.85f, 0.85f, 1.0f }, { 1, 1, 0, 0 }, whiteTexture, 0.0f, 0.45f);
    LoadSceneObjects(commandList, scene, materialIndexMap, defaultMaterial);
    AddDynamicSceneAutomationEmitter(commandList, whiteTexture);
    m_StressTestSpheresEnabled = enableStressTestSpheres;
    AddStressTestSpheres(commandList, whiteTexture);
    InitializeMeshletSceneResources();
    UploadMeshletBuffers(commandList);

    return !m_GeometryResources.GetObjects().empty();
}

std::vector<uint32_t> RaytracingDemoSceneResources::LoadSceneMaterials(
    CommandList& commandList,
    const Scene& scene,
    const uint32_t whiteTexture)
{
    struct LoadedTexture
    {
        uint32_t DescriptorIndex = 0u;
        bool Loaded = false;
    };

    const auto addTextureOrFallback = [this, &commandList, whiteTexture](
        const SceneTextureBinding& binding,
        const TextureUsageType usage) -> LoadedTexture
        {
            if (binding.EmbeddedTexture != nullptr && binding.EmbeddedTexture->IsValid())
            {
                return { m_TextureMaterialResources.AddTexture(commandList, binding, usage), true };
            }
            if (!binding.AssetPath.empty() &&
                std::filesystem::exists(binding.AssetPath) &&
                ToLower(binding.AssetPath.extension().string()) != ".exr")
            {
                return { AddTexture(commandList, ToWidePath(binding.AssetPath), usage), true };
            }
            return { whiteTexture, false };
        };

    std::vector<uint32_t> materialIndexMap;
    materialIndexMap.reserve(scene.GetMaterials().size());
    for (const SceneMaterial& sceneMaterial : scene.GetMaterials())
    {
        if (!sceneMaterial.IsPbrMaterial)
        {
            materialIndexMap.push_back(AddDiffuseMaterial({ 0.8f, 0.8f, 0.8f, 1.0f }, { 1, 1, 0, 0 }, whiteTexture, 0.0f, 0.5f));
            continue;
        }

        const LoadedTexture diffuseTexture = addTextureOrFallback(sceneMaterial.BaseMap, TextureUsageType::Albedo);
        const LoadedTexture normalTexture = addTextureOrFallback(sceneMaterial.NormalMap, TextureUsageType::Normalmap);
        const SceneTextureBinding& metallicBinding = sceneMaterial.MetallicMap.IsValid()
            ? sceneMaterial.MetallicMap
            : sceneMaterial.MetallicGlossMap;
        const SceneTextureBinding& roughnessBinding = sceneMaterial.RoughnessMap.IsValid()
            ? sceneMaterial.RoughnessMap
            : sceneMaterial.MetallicGlossMap;
        const LoadedTexture metallicTexture = addTextureOrFallback(metallicBinding, TextureUsageType::Other);
        const LoadedTexture roughnessTexture = addTextureOrFallback(roughnessBinding, TextureUsageType::Other);
        const LoadedTexture occlusionTexture = addTextureOrFallback(sceneMaterial.OcclusionMap, TextureUsageType::Other);
        const LoadedTexture emissionTexture = addTextureOrFallback(sceneMaterial.EmissionMap, TextureUsageType::Albedo);

        RaytracingDemoMaterialData material = MakeSceneMaterial(
            sceneMaterial,
            diffuseTexture.DescriptorIndex,
            normalTexture.DescriptorIndex,
            metallicTexture.DescriptorIndex,
            roughnessTexture.DescriptorIndex,
            occlusionTexture.DescriptorIndex,
            emissionTexture.DescriptorIndex);
        material.HasDiffuseMap = diffuseTexture.Loaded ? 1u : 0u;
        material.HasNormalMap = normalTexture.Loaded ? 1u : 0u;
        material.HasMetallicMap = metallicTexture.Loaded ? 1u : 0u;
        material.HasRoughnessMap = roughnessTexture.Loaded ? 1u : 0u;
        material.HasAmbientOcclusionMap = occlusionTexture.Loaded ? 1u : 0u;
        material.HasEmissionMap = emissionTexture.Loaded ? 1u : 0u;
        materialIndexMap.push_back(AddMaterial(material));
    }

    return materialIndexMap;
}

void RaytracingDemoSceneResources::LoadSceneObjects(
    CommandList& commandList,
    const Scene& scene,
    const std::vector<uint32_t>& materialIndexMap,
    const uint32_t defaultMaterial)
{
    ModelLoader modelLoader;
    std::unordered_map<std::string, std::vector<MeshPrototype>> importedMeshPrototypeCache;

    for (const SceneObject& object : scene.GetObjects())
    {
        uint32_t materialIndex = defaultMaterial;
        if (object.MaterialIndex < materialIndexMap.size())
        {
            materialIndex = materialIndexMap[object.MaterialIndex];
        }

        if (object.Mesh.Kind == SceneMeshKind::BuiltinPlane)
        {
            auto model = modelLoader.LoadExisting(Mesh::CreatePlane(commandList));
            const uint32_t geometryIndex = AddSceneGeometry(model, std::vector<MeshPrototype>{ CreateBuiltinPlanePrototype(1.0f, 1.0f) });
            AddSceneObject(object.WorldMatrix, geometryIndex, materialIndex);
            continue;
        }

        if (object.Mesh.Kind == SceneMeshKind::BuiltinCube)
        {
            auto model = modelLoader.LoadExisting(Mesh::CreateCube(commandList));
            const uint32_t geometryIndex = AddSceneGeometry(model, std::vector<MeshPrototype>{ CreateBuiltinCubePrototype(1.0f) });
            AddSceneObject(object.WorldMatrix, geometryIndex, materialIndex);
            continue;
        }

        if (object.Mesh.Kind != SceneMeshKind::ExternalMesh || object.Mesh.AssetPath.empty())
        {
            continue;
        }

        const std::filesystem::path meshPath = object.Mesh.AssetPath;
        const std::string meshKey = meshPath.string();
        auto prototypeIterator = importedMeshPrototypeCache.find(meshKey);
        if (prototypeIterator == importedMeshPrototypeCache.end())
        {
            prototypeIterator = importedMeshPrototypeCache.emplace(meshKey, modelLoader.LoadAsMeshPrototypes(ToUtf8Path(meshPath))).first;
        }

        if (prototypeIterator->second.empty())
        {
            throw std::runtime_error("Imported mesh does not contain any renderable prototypes.");
        }
        const MeshPrototype* prototype = nullptr;
        if (object.Mesh.SubmeshIndex != SceneMeshReference::InvalidSubmeshIndex)
        {
            const auto sourceMesh = std::ranges::find_if(
                prototypeIterator->second,
                [&object](const MeshPrototype& candidate)
                {
                    return candidate.m_SourceMeshIndex == object.Mesh.SubmeshIndex;
                });
            if (sourceMesh != prototypeIterator->second.end())
            {
                prototype = &*sourceMesh;
            }
        }
        if (prototype == nullptr && !object.Mesh.SubmeshName.empty())
        {
            const auto namedMesh = std::ranges::find_if(
                prototypeIterator->second,
                [&object](const MeshPrototype& candidate)
                {
                    return ToLower(candidate.m_Name) == ToLower(object.Mesh.SubmeshName);
                });
            if (namedMesh != prototypeIterator->second.end())
            {
                prototype = &*namedMesh;
            }
        }
        if (prototype == nullptr && object.Mesh.SubmeshIndex == SceneMeshReference::InvalidSubmeshIndex && object.Mesh.SubmeshName.empty())
        {
            prototype = &prototypeIterator->second.front();
        }
        if (prototype == nullptr)
        {
            throw std::runtime_error(
                "Imported mesh submesh was not found: path='" + meshPath.string() +
                "', index=" + std::to_string(object.Mesh.SubmeshIndex) +
                ", name='" + object.Mesh.SubmeshName + "'.");
        }
        auto model = modelLoader.Load(commandList, std::vector<MeshPrototype>{ *prototype });
        const uint32_t geometryIndex = AddSceneGeometry(model, std::vector<MeshPrototype>{ *prototype });
        AddSceneObject(object.WorldMatrix, geometryIndex, materialIndex);
    }
}

uint32_t RaytracingDemoSceneResources::AddSceneGeometry(
    const std::shared_ptr<Model>& model,
    std::vector<MeshPrototype> prototypes)
{
    Assert(model != nullptr, "Scene geometry model must not be null.");
    return m_GeometryResources.AddGeometry(model, std::move(prototypes));
}

void RaytracingDemoSceneResources::AddSceneObject(
    const XMMATRIX& worldMatrix,
    const uint32_t geometryIndex,
    const uint32_t materialIndex)
{
    m_GeometryResources.AddObject(worldMatrix, geometryIndex, materialIndex);
}

void RaytracingDemoSceneResources::AddDynamicSceneAutomationEmitter(
    CommandList& commandList,
    const uint32_t whiteTextureIndex)
{
    if (!IsDynamicSceneAutomationEmitterEnabled())
    {
        return;
    }

    ModelLoader modelLoader;
    const uint32_t materialIndex = AddPbrMaterial(
        { 1.0f, 0.35f, 0.08f, 1.0f },
        { 1.0f, 1.0f, 0.0f, 0.0f },
        whiteTextureIndex,
        whiteTextureIndex,
        whiteTextureIndex,
        whiteTextureIndex,
        whiteTextureIndex,
        0.0f,
        0.45f,
        true,
        false,
        false,
        false,
        false,
        { 5.0f, 1.75f, 0.4f, 1.0f },
        whiteTextureIndex,
        false);
    auto model = modelLoader.LoadExisting(Mesh::CreateCube(commandList));
    const uint32_t geometryIndex = AddSceneGeometry(
        model,
        std::vector<MeshPrototype>{ CreateBuiltinCubePrototype(1.0f) });
    AddSceneObject(
        XMMatrixScaling(0.5f, 0.5f, 0.5f) * XMMatrixTranslation(0.0f, 3.0f, 18.0f),
        geometryIndex,
        materialIndex);
}

void RaytracingDemoSceneResources::InitializeMeshletSceneResources()
{
    m_MeshletResources.Initialize(
        m_GeometryResources.GetGeometries(),
        m_GeometryResources.GetObjects(),
        m_StressTestSphereObjectStart,
        m_StressTestSphereObjects.size(),
        m_StressTestSpheresEnabled);
}

void RaytracingDemoSceneResources::AddStressTestSpheres(CommandList& commandList, const uint32_t whiteTextureIndex)
{
    m_StressTestSphereObjects.clear();
    m_StressTestSphereObjectStart = m_GeometryResources.GetObjects().size();
    StressTestSceneData stressTestScene = SceneStressTestFactory::Create(
        commandList,
        m_TextureMaterialResources,
        m_GeometryResources,
        whiteTextureIndex);
    m_StressTestSphereMaterialIndex = stressTestScene.MaterialIndex;
    m_StressTestSphereObjects = std::move(stressTestScene.Objects);

    if (m_StressTestSpheresEnabled)
    {
        m_GeometryResources.AppendObjects(m_StressTestSphereObjects);
    }
}

bool RaytracingDemoSceneResources::SetStressTestSpheresEnabled(CommandList& commandList, const bool enabled)
{
    if (m_StressTestSpheresEnabled == enabled)
    {
        return false;
    }

    Assert(!m_StressTestSphereObjects.empty(), "Stress test sphere objects have not been initialized.");
    if (enabled)
    {
        m_StressTestSphereObjectStart = m_GeometryResources.GetObjects().size();
        m_GeometryResources.AppendObjects(m_StressTestSphereObjects);
        m_MeshletResources.AddStressInstances(m_StressTestSphereObjects);
        m_RayTracingResources.AddStressInstances(
            m_GeometryResources.GetGeometries(),
            m_StressTestSphereObjects);
    }
    else
    {
        m_MeshletResources.RemoveStressInstances();
        m_RayTracingResources.RemoveStressInstances();

        const size_t stressTestSphereObjectEnd = m_StressTestSphereObjectStart + m_StressTestSphereObjects.size();
        Assert(stressTestSphereObjectEnd == m_GeometryResources.GetObjects().size(), "Stress test sphere objects must remain at the end of the scene.");
        m_GeometryResources.ResizeObjects(m_StressTestSphereObjectStart);
    }

    m_StressTestSpheresEnabled = enabled;
    UploadMeshletBuffers(commandList);
    m_RayTracingResources.Update(commandList, m_TextureMaterialResources.GetBindlessDescriptorHeap());
    return true;
}

void RaytracingDemoSceneResources::UploadMeshletBuffers(CommandList& commandList)
{
    m_MeshletResources.Upload(commandList);
}

void RaytracingDemoSceneResources::BuildRayTracingAccelerationStructure(
    CommandList& commandList,
    const RayTracingAccelerationStructureBuildSettings settings)
{
    m_TextureMaterialResources.UploadMaterialBuffer(commandList);
    m_RayTracingResources.Build(
        commandList,
        m_GeometryResources.GetGeometries(),
        m_GeometryResources.GetObjects(),
        m_StressTestSphereObjectStart,
        m_StressTestSphereObjects.size(),
        m_StressTestSpheresEnabled,
        m_TextureMaterialResources.GetBindlessDescriptorHeap(),
        settings);
    InitializeDynamicRayTracingUpdateTarget();
}

void RaytracingDemoSceneResources::SetDynamicRayTracingUpdatesEnabled(const bool enabled)
{
    if (m_DynamicRayTracingUpdatesEnabled == enabled)
    {
        return;
    }

    m_DynamicRayTracingUpdatesEnabled = enabled;
    if (enabled)
    {
        m_DynamicRayTracingRestorePending = false;
    }
    else if (m_DynamicRayTracingMesh != nullptr)
    {
        m_DynamicRayTracingRestorePending = true;
    }
}

bool RaytracingDemoSceneResources::RequiresDynamicRayTracingUpdatePass() const
{
    return m_DynamicRayTracingMesh != nullptr &&
        (m_DynamicRayTracingUpdatesEnabled || m_DynamicRayTracingRestorePending);
}

const VertexBuffer& RaytracingDemoSceneResources::GetDynamicRayTracingVertexBuffer() const
{
    Assert(
        RequiresDynamicRayTracingUpdatePass(),
        "Dynamic ray tracing update pass requires an initialized scene mesh.");
    return m_DynamicRayTracingMesh->GetVertexBuffer();
}

const IndexBuffer& RaytracingDemoSceneResources::GetDynamicRayTracingIndexBuffer() const
{
    Assert(
        RequiresDynamicRayTracingUpdatePass(),
        "Dynamic ray tracing update pass requires an initialized scene mesh.");
    return m_DynamicRayTracingMesh->GetIndexBuffer();
}

bool RaytracingDemoSceneResources::BeginDynamicRayTracingGeometryUpdate(
    CommandList& commandList,
    const float timeSeconds)
{
    if (!RequiresDynamicRayTracingUpdatePass())
    {
        return false;
    }

    Assert(!m_DynamicRayTracingUpdatePending, "Dynamic ray tracing update cannot begin twice in one frame.");
    const bool restoring = !m_DynamicRayTracingUpdatesEnabled;
    m_DynamicRayTracingVertices = m_DynamicRayTracingBaseVertices;
    XMMATRIX worldMatrix = m_DynamicRayTracingBaseWorldMatrix;
    if (!restoring)
    {
        const float phase = timeSeconds * 3.0f;
        worldMatrix = XMMatrixTranslation(
            std::sin(phase) * 0.15f,
            0.0f,
            std::cos(phase * 0.7f) * 0.15f) *
            m_DynamicRayTracingBaseWorldMatrix;
        for (size_t vertexIndex = 0; vertexIndex < m_DynamicRayTracingVertices.size(); ++vertexIndex)
        {
            VertexAttributes& vertex = m_DynamicRayTracingVertices[vertexIndex];
            vertex.Position.y += std::sin(
                phase + static_cast<float>(vertexIndex % 29u) * 0.37f) * 0.015f;
        }
    }

    Assert(
        m_GeometryResources.UpdateObjectWorldMatrix(m_DynamicRayTracingObjectIndex, worldMatrix),
        "Dynamic ray tracing object index is invalid.");
    Assert(
        m_RayTracingResources.UpdateSceneObjectTransform(m_DynamicRayTracingObjectIndex, worldMatrix),
        "Dynamic ray tracing acceleration-structure instance update failed.");
    Assert(
        m_MeshletResources.UpdateSceneObjectTransform(m_DynamicRayTracingObjectIndex, worldMatrix),
        "Dynamic meshlet scene instance update failed.");
    Assert(
        m_GeometryResources.UpdateGeometryPrototypeVertices(
            m_DynamicRayTracingGeometryIndex,
            m_DynamicRayTracingPrototypeIndex,
            m_DynamicRayTracingVertices),
        "Dynamic ray tracing mesh prototype update failed.");
    Assert(
        m_MeshletResources.UpdateSceneGeometryVertices(
            m_DynamicRayTracingGeometryIndex,
            m_DynamicRayTracingPrototypeIndex,
            m_DynamicRayTracingVertices),
        "Dynamic meshlet geometry update failed.");
    m_DynamicRayTracingMesh->CopyVertexAttributes(commandList, m_DynamicRayTracingVertices);
    m_MeshletResources.Upload(commandList);
    m_DynamicRayTracingUpdatePending = true;
    m_DynamicRayTracingUpdateStatistics.LastUpdateRestored = restoring;
    ++m_DynamicRayTracingUpdateStatistics.GeometryUploadCount;
    ++m_DynamicRayTracingUpdateStatistics.MeshletTransformUpdateCount;
    ++m_DynamicRayTracingUpdateStatistics.MeshletGeometryUpdateCount;
    return true;
}

bool RaytracingDemoSceneResources::FinishDynamicRayTracingUpdate(CommandList& commandList)
{
    if (!m_DynamicRayTracingUpdatePending)
    {
        return false;
    }

    m_RayTracingResources.GetAccelerationStructure().MarkBottomLevelGeometryDirty(
        std::span<const std::shared_ptr<Mesh>>(&m_DynamicRayTracingMesh, 1u));
    m_RayTracingResources.RefitDirtyGeometry(commandList);
    m_DynamicRayTracingUpdatePending = false;
    ++m_DynamicRayTracingUpdateStatistics.RefitCount;
    if (m_DynamicRayTracingUpdateStatistics.LastUpdateRestored)
    {
        m_DynamicRayTracingRestorePending = false;
        ++m_DynamicRayTracingUpdateStatistics.RestoreCount;
    }
    return true;
}

bool RaytracingDemoSceneResources::RefreshDynamicEmissiveMeshSurfaceEmitters(SceneLightManager& lights)
{
    if (!m_DynamicRayTracingEmitterActive)
    {
        return false;
    }

    lights.SetEmissiveMeshSurfaceEmitters(CollectEmissiveMeshSurfaceEmitters());
    ++m_DynamicRayTracingUpdateStatistics.EmissiveMeshRefreshCount;
    return true;
}

void RaytracingDemoSceneResources::InitializeDynamicRayTracingUpdateTarget()
{
    m_DynamicRayTracingRestorePending = false;
    m_DynamicRayTracingUpdatePending = false;
    m_DynamicRayTracingObjectIndex = (std::numeric_limits<size_t>::max)();
    m_DynamicRayTracingGeometryIndex = (std::numeric_limits<uint32_t>::max)();
    m_DynamicRayTracingPrototypeIndex = (std::numeric_limits<uint32_t>::max)();
    m_DynamicRayTracingMesh.reset();
    m_DynamicRayTracingEmitterActive = false;
    m_DynamicRayTracingBaseVertices.clear();
    m_DynamicRayTracingVertices.clear();

    const std::vector<RaytracingDemoSceneObject>& objects = m_GeometryResources.GetObjects();
    const std::vector<RaytracingDemoSceneGeometry>& geometries = m_GeometryResources.GetGeometries();
    const size_t nonStressObjectCount = m_StressTestSphereObjects.empty()
        ? objects.size()
        : (std::min)(m_StressTestSphereObjectStart, objects.size());
    const auto isEmissiveObject = [this](const RaytracingDemoSceneObject& object)
    {
        if (object.MaterialIndex >= m_TextureMaterialResources.GetMaterials().size())
        {
            return false;
        }

        const RaytracingDemoMaterialData& material =
            m_TextureMaterialResources.GetMaterials()[object.MaterialIndex];
        return material.HasEmissionMap != 0u ||
            std::fmax(material.Emission.x, std::fmax(material.Emission.y, material.Emission.z)) > 1.0e-4f;
    };
    const auto selectTarget = [this, &geometries, &objects, nonStressObjectCount, &isEmissiveObject](const bool emissiveOnly)
    {
        for (size_t objectIndex = 0; objectIndex < nonStressObjectCount; ++objectIndex)
        {
            const RaytracingDemoSceneObject& object = objects[objectIndex];
            const bool emitter = isEmissiveObject(object);
            if (emissiveOnly && !emitter)
            {
                continue;
            }

            Assert(object.GeometryIndex < geometries.size(), "Dynamic ray tracing scene object geometry index is invalid.");
            const RaytracingDemoSceneGeometry& geometry = geometries[object.GeometryIndex];
            const std::vector<std::shared_ptr<Mesh>>& meshes = geometry.Model->GetMeshes();
            const size_t meshCount = (std::min)(meshes.size(), geometry.MeshPrototypes.size());
            for (size_t meshIndex = 0; meshIndex < meshCount; ++meshIndex)
            {
                const MeshPrototype& prototype = geometry.MeshPrototypes[meshIndex];
                if (meshes[meshIndex] == nullptr ||
                    prototype.m_Vertices.empty() ||
                    !prototype.m_SkinningVertexAttributes.empty())
                {
                    continue;
                }

                m_DynamicRayTracingObjectIndex = objectIndex;
                m_DynamicRayTracingGeometryIndex = object.GeometryIndex;
                m_DynamicRayTracingPrototypeIndex = static_cast<uint32_t>(meshIndex);
                m_DynamicRayTracingMesh = meshes[meshIndex];
                m_DynamicRayTracingBaseWorldMatrix = object.WorldMatrix;
                m_DynamicRayTracingBaseVertices = prototype.m_Vertices;
                m_DynamicRayTracingVertices = m_DynamicRayTracingBaseVertices;
                m_DynamicRayTracingEmitterActive = emitter;
                return true;
            }
        }
        return false;
    };
    if (!selectTarget(true))
    {
        selectTarget(false);
    }
}
//Modify End
