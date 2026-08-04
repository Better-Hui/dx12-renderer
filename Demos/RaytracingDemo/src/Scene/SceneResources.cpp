//Modify Begin:2026-07-27 by BestHui
#include <Scene/SceneResources.h>

#include <DX12Library/CommandList.h>
//Modify Begin:2026-07-30 by BestHui
#include <DX12Library/Helpers.h>
//Modify End
#include <Framework/Geometry/Mesh.h>
//Modify Begin:2026-07-30 by BestHui
#include <Framework/Geometry/Meshlet.h>
//Modify End
#include <Framework/Geometry/Model.h>
#include <Framework/Geometry/ModelLoader.h>
#include <Framework/Rendering/RayTracing/RayTracingAccelerationStructure.h>

#include <algorithm>
#include <cctype>
#include <cmath>
//Modify Begin:2026-07-30 by BestHui
#include <cstddef>
//Modify End
#include <filesystem>
#include <stdexcept>
#include <unordered_map>
#include <utility>

using namespace DirectX;

namespace
{
    Microsoft::WRL::ComPtr<ID3D12Device5> GetRayTracingDevice(const Microsoft::WRL::ComPtr<ID3D12Device2>& device)
    {
        Assert(device != nullptr, "Raytracing demo scene resources require a D3D12 device.");

        Microsoft::WRL::ComPtr<ID3D12Device5> rayTracingDevice;
        ThrowIfFailed(device.As(&rayTracingDevice));
        return rayTracingDevice;
    }

    std::string ToLower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
        return value;
    }

    const MeshPrototype& FindMeshPrototypeByName(
        const std::vector<MeshPrototype>& prototypes,
        const std::string& expectedName)
    {
        const std::string expectedLower = ToLower(expectedName);
        for (const MeshPrototype& prototype : prototypes)
        {
            if (ToLower(prototype.m_Name) == expectedLower)
            {
                return prototype;
            }
        }

        for (const MeshPrototype& prototype : prototypes)
        {
            const std::string prototypeName = ToLower(prototype.m_Name);
            if (!expectedLower.empty() && prototypeName.find(expectedLower) != std::string::npos)
            {
                return prototype;
            }
        }

        throw std::runtime_error("Scene mesh submesh name does not exist in the imported model.");
    }

    std::wstring ToWidePath(const std::filesystem::path& path)
    {
        return path.wstring();
    }

    std::string ToUtf8Path(const std::filesystem::path& path)
    {
        return path.string();
    }

    RaytracingDemoMaterialData MakeSceneMaterial(
        const SceneMaterial& material,
        const uint32_t diffuseTextureIndex,
        const uint32_t normalTextureIndex,
        const uint32_t metallicTextureIndex,
        const uint32_t roughnessTextureIndex,
        const uint32_t ambientOcclusionTextureIndex)
    {
        RaytracingDemoMaterialData output{};
        output.Diffuse = material.BaseColor;
        output.Specular = material.SpecColor;
        output.TilingOffset = material.BaseMap.ScaleOffset;
        output.DiffuseTextureIndex = diffuseTextureIndex;
        output.NormalTextureIndex = normalTextureIndex;
        output.MetallicTextureIndex = metallicTextureIndex;
        output.RoughnessTextureIndex = roughnessTextureIndex;
        output.AmbientOcclusionTextureIndex = ambientOcclusionTextureIndex;
        output.HasDiffuseMap = material.BaseMap.IsValid() ? 1u : 0u;
        output.HasNormalMap = material.NormalMap.IsValid() ? 1u : 0u;
        output.HasMetallicMap = material.MetallicGlossMap.IsValid() ? 1u : 0u;
        output.HasRoughnessMap = material.MetallicGlossMap.IsValid() ? 1u : 0u;
        output.HasAmbientOcclusionMap = material.OcclusionMap.IsValid() ? 1u : 0u;
        output.Metallic = material.Metallic;
        output.Roughness = std::clamp(material.Roughness, 0.02f, 1.0f);
        return output;
    }

//Modify Begin:2026-07-30 by BestHui
    MeshPrototype CreateBuiltinPlanePrototype(const float width, const float height)
    {
//Modify Begin:2026-07-31 by BestHui
        return Mesh::CreatePlanePrototype(width, height);
//Modify End
    }

//Modify Begin:2026-07-31 by BestHui
    MeshPrototype CreateBuiltinCubePrototype(const float size)
    {
        return Mesh::CreateCubePrototype(size);
    }
    MeshPrototype CreateStressSpherePrototype(const float diameter, const size_t tessellation)
    {
        VertexCollectionType vertices;
        IndexCollectionType indices;

        if (tessellation < 3)
        {
            throw std::out_of_range("tessellation parameter out of range");
        }

        const float radius = diameter * 0.5f;
        const size_t verticalSegments = tessellation;
        const size_t horizontalSegments = tessellation * 2;

        vertices.emplace_back(
            XMFLOAT3(0.0f, radius, 0.0f),
            XMFLOAT3(0.0f, 1.0f, 0.0f),
            XMFLOAT2(0.5f, 0.0f));

        for (size_t i = 1; i < verticalSegments; ++i)
        {
            const float v = static_cast<float>(i) / static_cast<float>(verticalSegments);
            const float polar = static_cast<float>(i) * XM_PI / static_cast<float>(verticalSegments);
            float sinPolar = 0.0f;
            float cosPolar = 0.0f;
            XMScalarSinCos(&sinPolar, &cosPolar, polar);

            float dxz = 0.0f;
            float dy = 0.0f;
            dxz = sinPolar;
            dy = cosPolar;

            for (size_t j = 0; j < horizontalSegments; ++j)
            {
                const float u = static_cast<float>(j) / static_cast<float>(horizontalSegments);
                const float longitude = static_cast<float>(j) * XM_2PI / static_cast<float>(horizontalSegments);
                float dx = 0.0f;
                float dz = 0.0f;
                XMScalarSinCos(&dx, &dz, longitude);
                dx *= dxz;
                dz *= dxz;

                const XMVECTOR normal = XMVectorSet(dx, dy, dz, 0.0f);
                const XMVECTOR position = normal * radius;
                const XMVECTOR uv = XMVectorSet(u, v, 0.0f, 0.0f);
                XMFLOAT3 positionFloat{};
                XMFLOAT3 normalFloat{};
                XMFLOAT2 uvFloat{};
                XMStoreFloat3(&positionFloat, position);
                XMStoreFloat3(&normalFloat, normal);
                XMStoreFloat2(&uvFloat, uv);
                vertices.emplace_back(positionFloat, normalFloat, uvFloat);
            }
        }

        const uint16_t bottomIndex = static_cast<uint16_t>(vertices.size());
        vertices.emplace_back(
            XMFLOAT3(0.0f, -radius, 0.0f),
            XMFLOAT3(0.0f, -1.0f, 0.0f),
            XMFLOAT2(0.5f, 1.0f));

        const auto ringIndex = [horizontalSegments](const size_t ring, const size_t segment)
        {
            return static_cast<uint16_t>(1 + ring * horizontalSegments + (segment % horizontalSegments));
        };

        for (size_t j = 0; j < horizontalSegments; ++j)
        {
            indices.push_back(0);
            indices.push_back(ringIndex(0, j));
            indices.push_back(ringIndex(0, j + 1));
        }

        for (size_t i = 0; i + 1 < verticalSegments - 1; ++i)
        {
            for (size_t j = 0; j < horizontalSegments; ++j)
            {
                indices.push_back(ringIndex(i, j));
                indices.push_back(ringIndex(i + 1, j));
                indices.push_back(ringIndex(i, j + 1));

                indices.push_back(ringIndex(i, j + 1));
                indices.push_back(ringIndex(i + 1, j));
                indices.push_back(ringIndex(i + 1, j + 1));
            }
        }

        const size_t lastRing = verticalSegments - 2;
        for (size_t j = 0; j < horizontalSegments; ++j)
        {
            indices.push_back(ringIndex(lastRing, j + 1));
            indices.push_back(ringIndex(lastRing, j));
            indices.push_back(bottomIndex);
        }

        MeshPrototype prototype(std::move(vertices), std::move(indices), true, true);
        prototype.m_Name = "StressSphere";
        return prototype;
    }
//Modify End

}

RaytracingDemoSceneResources::RaytracingDemoSceneResources(Microsoft::WRL::ComPtr<ID3D12Device2> device)
    : m_MaterialBuffer(L"Ray Tracing Materials")
    , m_GeometryBuffer(L"Ray Tracing Geometry Data")
    , m_BindlessDescriptorHeap(*device.Get())
    , m_RayTracingAccelerationStructure(GetRayTracingDevice(device))
{
}

void RaytracingDemoSceneResources::Clear()
{
    m_GeometryBuffer = StructuredBuffer(L"Ray Tracing Geometry Data");
    m_MaterialBuffer = StructuredBuffer(L"Ray Tracing Materials");
//Modify Begin:2026-07-30 by BestHui
    m_BindlessDescriptorHeap.Reset();
//Modify End
//Modify Begin:2026-07-31 by BestHui
//Modify Begin:2026-07-30 by BestHui
    m_MeshletSceneResources.Clear();
//Modify End
//Modify End
    m_SceneGeometries.clear();
    m_SceneObjects.clear();
    m_Materials.clear();
    m_Textures.clear();
//Modify Begin:2026-07-30 by BestHui
    m_StressTestSphereObjects.clear();
    m_StressTestSphereMeshletInstances.clear();
    m_StressTestSphereRayTracingInstances.clear();
    m_StressTestSphereObjectStart = 0;
    m_StressTestSpheresEnabled = true;
//Modify End
}

uint32_t RaytracingDemoSceneResources::AddTexture(CommandList& commandList, const std::wstring& path, TextureUsageType usage)
{
    auto texture = std::make_shared<Texture>();
    commandList.LoadTextureFromFile(*texture, path, usage);
//Modify Begin:2026-07-30 by BestHui
    const uint32_t textureIndex = static_cast<uint32_t>(m_Textures.size());
    m_BindlessDescriptorHeap.AddShaderResourceView(*texture);
//Modify End
    m_Textures.push_back(texture);
//Modify Begin:2026-07-30 by BestHui
    return textureIndex;
//Modify End
}

uint32_t RaytracingDemoSceneResources::AddMaterial(const RaytracingDemoMaterialData& material)
{
    m_Materials.push_back(material);
    return static_cast<uint32_t>(m_Materials.size() - 1);
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
    const bool hasAmbientOcclusionMap)
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
    material.HasDiffuseMap = hasDiffuseMap ? 1u : 0u;
    material.HasNormalMap = hasNormalMap ? 1u : 0u;
    material.HasMetallicMap = hasMetallicMap ? 1u : 0u;
    material.HasRoughnessMap = hasRoughnessMap ? 1u : 0u;
    material.HasAmbientOcclusionMap = hasAmbientOcclusionMap ? 1u : 0u;
    material.Metallic = metallic;
    material.Roughness = roughness;
    return AddMaterial(material);
}

uint32_t RaytracingDemoSceneResources::AddDiffuseMaterial(
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

//Modify Begin:2026-07-30 by BestHui
std::vector<ShaderResourceView> RaytracingDemoSceneResources::CreateTextureShaderResourceViews() const
{
    std::vector<ShaderResourceView> shaderResourceViews;
    shaderResourceViews.reserve(m_Textures.size());
    for (const std::shared_ptr<Texture>& texture : m_Textures)
    {
        shaderResourceViews.emplace_back(texture);
    }
    return shaderResourceViews;
}
//Modify End

//Modify Begin:2026-07-30 by BestHui
void RaytracingDemoSceneResources::TransitionRayTracingShaderResources(
    CommandList& commandList,
    const D3D12_RESOURCE_STATES stateAfter) const
{
    for (const std::shared_ptr<Texture>& texture : m_Textures)
    {
        commandList.TransitionBarrier(*texture, stateAfter);
    }

    for (const std::shared_ptr<Mesh>& mesh : m_RayTracingAccelerationStructure.GetMeshes())
    {
        commandList.TransitionBarrier(mesh->GetVertexBuffer(), stateAfter);
        commandList.TransitionBarrier(mesh->GetIndexBuffer(), stateAfter);
    }
}
//Modify End

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
//Modify Begin:2026-07-31 by BestHui
        const uint32_t geometryIndex = AddSceneGeometry(model, std::vector<MeshPrototype>{ CreateBuiltinPlanePrototype(1.0f, 1.0f) });
        AddSceneObject(worldMatrix, geometryIndex, groundMaterial);
//Modify End
    }

    {
        //Modify Begin:2026-07-31 by BestHui
        const std::vector<MeshPrototype> chestPrototypes = modelLoader.LoadAsMeshPrototypes("Assets/Models/old-wooden-chest/chest_01.fbx");
        auto model = modelLoader.Load(commandList, chestPrototypes);
        const uint32_t geometryIndex = AddSceneGeometry(model, chestPrototypes);
//Modify End

        XMMATRIX worldMatrix =
            XMMatrixScaling(0.01f, 0.01f, 0.01f) *
            XMMatrixRotationRollPitchYaw(XMConvertToRadians(90.0f), 0.0f, 0.0f) *
            XMMatrixTranslation(0.0f, 0.25f, 15.0f);
//Modify Begin:2026-07-31 by BestHui
        AddSceneObject(worldMatrix, geometryIndex, chestMaterial);
//Modify End

        worldMatrix =
            XMMatrixScaling(0.01f, 0.01f, 0.01f) *
            XMMatrixRotationRollPitchYaw(XMConvertToRadians(90.0f), 0.0f, 0.0f) *
            XMMatrixTranslation(-50.0f, 0.25f, 15.0f);
//Modify Begin:2026-07-31 by BestHui
        AddSceneObject(worldMatrix, geometryIndex, chestMaterial);
//Modify End
    }

    {
        auto model = modelLoader.LoadExisting(Mesh::CreatePlane(commandList));
        const XMMATRIX worldMatrix = XMMatrixScaling(30.0f, 30.0f, 30.0f) * XMMatrixTranslation(-50.0f, 0.1f, 15.0f);
//Modify Begin:2026-07-31 by BestHui
        const uint32_t geometryIndex = AddSceneGeometry(model, std::vector<MeshPrototype>{ CreateBuiltinPlanePrototype(1.0f, 1.0f) });
        AddSceneObject(worldMatrix, geometryIndex, mirrorMaterial);
//Modify End
    }

    {
        auto model = modelLoader.LoadExisting(Mesh::CreateCube(commandList));
        const XMMATRIX worldMatrix = XMMatrixScaling(5.0f, 5.0f, 5.0f) * XMMatrixTranslation(-54.0f, 2.5f, 7.0f);
//Modify Begin:2026-07-31 by BestHui
        const uint32_t geometryIndex = AddSceneGeometry(model, std::vector<MeshPrototype>{ CreateBuiltinCubePrototype(1.0f) });
        AddSceneObject(worldMatrix, geometryIndex, cubeMaterial);
//Modify End
    }

    {
        //Modify Begin:2026-07-31 by BestHui
        const std::vector<MeshPrototype> cerberusPrototypes = modelLoader.LoadAsMeshPrototypes("Assets/Models/cerberus/Cerberus_LP.FBX");
        auto model = modelLoader.Load(commandList, cerberusPrototypes);
        const uint32_t geometryIndex = AddSceneGeometry(model, cerberusPrototypes);
//Modify End
        const XMMATRIX worldMatrix =
            XMMatrixScaling(0.10f, 0.10f, 0.10f) *
            XMMatrixRotationRollPitchYaw(XMConvertToRadians(90.0f), XMConvertToRadians(135.0f), 0.0f) *
            XMMatrixTranslation(15.0f, 5.0f, 10.0f);
//Modify Begin:2026-07-31 by BestHui
        AddSceneObject(worldMatrix, geometryIndex, cerberusMaterial);
//Modify End
    }

    {
        //Modify Begin:2026-07-31 by BestHui
        const std::vector<MeshPrototype> tvPrototypes = modelLoader.LoadAsMeshPrototypes("Assets/Models/tv/TV.FBX");
        auto model = modelLoader.Load(commandList, tvPrototypes);
        const uint32_t geometryIndex = AddSceneGeometry(model, tvPrototypes);
//Modify End
        const XMMATRIX worldMatrix =
            XMMatrixScaling(0.30f, 0.30f, 0.30f) *
            XMMatrixRotationRollPitchYaw(XMConvertToRadians(90.0f), XMConvertToRadians(-45.0f), 0.0f) *
            XMMatrixTranslation(-14.0f, 0.0f, 18.0f);
//Modify Begin:2026-07-31 by BestHui
        AddSceneObject(worldMatrix, geometryIndex, tvMaterial);
//Modify End
    }

//Modify Begin:2026-07-31 by BestHui
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

    const MeshPrototype spherePrototype = CreateStressSpherePrototype(1.0f, 12);
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

//Modify Begin:2026-07-31 by BestHui
                AddSceneObject(worldMatrix, sphereGeometryIndex, material);
//Modify End
            }
        }
    }

    InitializeMeshletSceneResources();
    UploadMeshletBuffers(commandList);
//Modify End
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
//Modify Begin:2026-07-30 by BestHui
//Modify Begin:2026-07-30 by BestHui
    m_StressTestSpheresEnabled = enableStressTestSpheres;
//Modify End
    AddStressTestSpheres(commandList, whiteTexture);
    InitializeMeshletSceneResources();
    UploadMeshletBuffers(commandList);
//Modify End

    return !m_SceneObjects.empty();
}

std::vector<uint32_t> RaytracingDemoSceneResources::LoadSceneMaterials(
    CommandList& commandList,
    const Scene& scene,
    const uint32_t whiteTexture)
{
    const auto addTextureOrFallback = [this, &commandList, whiteTexture](
        const SceneTextureBinding& binding,
        const TextureUsageType usage) -> uint32_t
        {
            if (!binding.AssetPath.empty() && std::filesystem::exists(binding.AssetPath))
            {
                return AddTexture(commandList, ToWidePath(binding.AssetPath), usage);
            }
            return whiteTexture;
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

        const uint32_t diffuseTexture = addTextureOrFallback(sceneMaterial.BaseMap, TextureUsageType::Albedo);
        const uint32_t normalTexture = addTextureOrFallback(sceneMaterial.NormalMap, TextureUsageType::Normalmap);
        const uint32_t metallicTexture = addTextureOrFallback(sceneMaterial.MetallicGlossMap, TextureUsageType::Other);
        const uint32_t roughnessTexture = addTextureOrFallback(sceneMaterial.MetallicGlossMap, TextureUsageType::Other);
        const uint32_t occlusionTexture = addTextureOrFallback(sceneMaterial.OcclusionMap, TextureUsageType::Other);

        materialIndexMap.push_back(
            AddMaterial(MakeSceneMaterial(
                sceneMaterial,
                diffuseTexture,
                normalTexture,
                metallicTexture,
                roughnessTexture,
                occlusionTexture)));
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
//Modify Begin:2026-08-03 by BestHui
            auto model = modelLoader.LoadExisting(Mesh::CreatePlane(commandList));
//Modify Begin:2026-07-30 by BestHui
//Modify Begin:2026-07-31 by BestHui
            const uint32_t geometryIndex = AddSceneGeometry(model, std::vector<MeshPrototype>{ CreateBuiltinPlanePrototype(1.0f, 1.0f) });
            AddSceneObject(object.WorldMatrix, geometryIndex, materialIndex);
//Modify End
//Modify End
//Modify End
            continue;
        }

//Modify Begin:2026-08-03 by BestHui
        if (object.Mesh.Kind == SceneMeshKind::BuiltinCube)
        {
            auto model = modelLoader.LoadExisting(Mesh::CreateCube(commandList));
            const uint32_t geometryIndex = AddSceneGeometry(model, std::vector<MeshPrototype>{ CreateBuiltinCubePrototype(1.0f) });
            AddSceneObject(object.WorldMatrix, geometryIndex, materialIndex);
            continue;
        }
//Modify End

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

//Modify Begin:2026-08-03 by BestHui
        if (prototypeIterator->second.empty())
        {
            throw std::runtime_error("Imported mesh does not contain any renderable prototypes.");
        }
        const MeshPrototype& prototype = object.Mesh.SubmeshName.empty()
            ? prototypeIterator->second.front()
            : FindMeshPrototypeByName(prototypeIterator->second, object.Mesh.SubmeshName);
//Modify End
        auto model = modelLoader.Load(commandList, std::vector<MeshPrototype>{ prototype });
//Modify Begin:2026-07-30 by BestHui
//Modify Begin:2026-07-31 by BestHui
        const uint32_t geometryIndex = AddSceneGeometry(model, std::vector<MeshPrototype>{ prototype });
        AddSceneObject(object.WorldMatrix, geometryIndex, materialIndex);
//Modify End
//Modify End
    }
}

//Modify Begin:2026-07-31 by BestHui
uint32_t RaytracingDemoSceneResources::AddSceneGeometry(
    const std::shared_ptr<Model>& model,
    std::vector<MeshPrototype> prototypes)
{
    Assert(model != nullptr, "Scene geometry model must not be null.");
    const uint32_t geometryIndex = static_cast<uint32_t>(m_SceneGeometries.size());
    m_SceneGeometries.push_back({ model, std::move(prototypes) });
    return geometryIndex;
}

void RaytracingDemoSceneResources::AddSceneObject(
    const XMMATRIX& worldMatrix,
    const uint32_t geometryIndex,
    const uint32_t materialIndex)
{
    Assert(geometryIndex < m_SceneGeometries.size(), "Scene object geometry index is invalid.");
    m_SceneObjects.push_back({ worldMatrix, geometryIndex, materialIndex });
}

//Modify Begin:2026-07-30 by BestHui
void RaytracingDemoSceneResources::InitializeMeshletSceneResources()
{
    std::vector<MeshletSceneGeometrySource> meshletGeometries;
    meshletGeometries.reserve(m_SceneGeometries.size());
    for (const RaytracingDemoSceneGeometry& geometry : m_SceneGeometries)
    {
        meshletGeometries.push_back({ &geometry.MeshPrototypes });
    }

    m_MeshletSceneResources.Clear();
    m_MeshletSceneResources.InitializeGeometries(meshletGeometries);
    m_StressTestSphereMeshletInstances.clear();
    for (size_t objectIndex = 0; objectIndex < m_SceneObjects.size(); ++objectIndex)
    {
        const RaytracingDemoSceneObject& object = m_SceneObjects[objectIndex];
        const MeshletSceneInstanceHandle instanceHandle = m_MeshletSceneResources.AddInstance(
            { object.WorldMatrix, object.GeometryIndex, object.MaterialIndex });
        if (m_StressTestSpheresEnabled &&
            !m_StressTestSphereObjects.empty() &&
            objectIndex >= m_StressTestSphereObjectStart)
        {
            m_StressTestSphereMeshletInstances.push_back(instanceHandle);
        }
    }
}
//Modify End

void RaytracingDemoSceneResources::AddStressTestSpheres(CommandList& commandList, const uint32_t whiteTextureIndex)
{
//Modify Begin:2026-07-30 by BestHui
    m_StressTestSphereObjects.clear();
    m_StressTestSphereObjectStart = m_SceneObjects.size();
//Modify End
    ModelLoader modelLoader;
    MeshPrototype spherePrototype = CreateStressSpherePrototype(1.0f, 12);
    auto sphereModel = modelLoader.Load(commandList, std::vector<MeshPrototype>{ spherePrototype });
    const uint32_t sphereGeometryIndex = AddSceneGeometry(sphereModel, std::vector<MeshPrototype>{ spherePrototype });

    const uint32_t sphereMaterial = AddDiffuseMaterial(
        { 1.0f, 0.48f, 0.18f, 1.0f },
        { 1.0f, 1.0f, 0.0f, 0.0f },
        whiteTextureIndex,
        0.0f,
        0.38f);

    constexpr uint32_t Columns = 64;
    constexpr uint32_t Rows = 24;
    constexpr uint32_t DepthLayers = 8;
    constexpr float XSpacing = 0.62f;
    constexpr float YSpacing = 0.46f;
    constexpr float ZSpacing = 0.78f;
    constexpr float Radius = 0.17f;
    constexpr float CenterX = -4.50f;
    constexpr float CenterY = 2.85f;
    constexpr float CenterZ = -2.80f;
    const float startX = -static_cast<float>(Columns - 1) * XSpacing * 0.5f;
    const float startY = -static_cast<float>(Rows - 1) * YSpacing * 0.5f;
    const float startZ = -static_cast<float>(DepthLayers - 1) * ZSpacing * 0.5f;

    for (uint32_t layer = 0; layer < DepthLayers; ++layer)
    {
        for (uint32_t y = 0; y < Rows; ++y)
        {
            for (uint32_t x = 0; x < Columns; ++x)
            {
                const float wave = std::sin(static_cast<float>(x) * 0.37f + static_cast<float>(y) * 0.61f + static_cast<float>(layer) * 0.83f);
                const float stagger = (static_cast<float>((y + layer) & 1u) - 0.5f) * XSpacing * 0.35f;
                const XMMATRIX worldMatrix =
                    XMMatrixScaling(Radius, Radius, Radius) *
                    XMMatrixTranslation(
                        CenterX + startX + static_cast<float>(x) * XSpacing + stagger,
                        CenterY + startY + static_cast<float>(y) * YSpacing + wave * 0.045f,
                        CenterZ + startZ + static_cast<float>(layer) * ZSpacing);

//Modify Begin:2026-07-30 by BestHui
                m_StressTestSphereObjects.push_back({ worldMatrix, sphereGeometryIndex, sphereMaterial });
//Modify End
            }
        }
    }

//Modify Begin:2026-07-30 by BestHui
    if (m_StressTestSpheresEnabled)
    {
        m_SceneObjects.insert(
            m_SceneObjects.end(),
            m_StressTestSphereObjects.begin(),
            m_StressTestSphereObjects.end());
    }
//Modify End
}

//Modify Begin:2026-07-30 by BestHui
bool RaytracingDemoSceneResources::SetStressTestSpheresEnabled(CommandList& commandList, const bool enabled)
{
    if (m_StressTestSpheresEnabled == enabled)
    {
        return false;
    }

    Assert(!m_StressTestSphereObjects.empty(), "Stress test sphere objects have not been initialized.");
    if (enabled)
    {
        m_StressTestSphereObjectStart = m_SceneObjects.size();
        m_SceneObjects.insert(
            m_SceneObjects.end(),
            m_StressTestSphereObjects.begin(),
            m_StressTestSphereObjects.end());
        for (const RaytracingDemoSceneObject& object : m_StressTestSphereObjects)
        {
            m_StressTestSphereMeshletInstances.push_back(m_MeshletSceneResources.AddInstance(
                { object.WorldMatrix, object.GeometryIndex, object.MaterialIndex }));
            AddRayTracingInstances(
                m_RayTracingAccelerationStructure,
                object,
                &m_StressTestSphereRayTracingInstances);
        }
    }
    else
    {
        for (const MeshletSceneInstanceHandle handle : m_StressTestSphereMeshletInstances)
        {
            Assert(m_MeshletSceneResources.RemoveInstance(handle), "Stress test meshlet instance handle is invalid.");
        }
        m_StressTestSphereMeshletInstances.clear();
        for (const RayTracingInstanceHandle handle : m_StressTestSphereRayTracingInstances)
        {
            Assert(m_RayTracingAccelerationStructure.RemoveInstance(handle), "Stress test ray tracing instance handle is invalid.");
        }
        m_StressTestSphereRayTracingInstances.clear();

        const size_t stressTestSphereObjectEnd = m_StressTestSphereObjectStart + m_StressTestSphereObjects.size();
        Assert(stressTestSphereObjectEnd == m_SceneObjects.size(), "Stress test sphere objects must remain at the end of the scene.");
        m_SceneObjects.resize(m_StressTestSphereObjectStart);
    }

    m_StressTestSpheresEnabled = enabled;
    UploadMeshletBuffers(commandList);
    m_RayTracingAccelerationStructure.Update(commandList);
    UploadRayTracingGeometryBuffer(commandList, m_RayTracingAccelerationStructure);
    return true;
}
//Modify End

void RaytracingDemoSceneResources::UploadMeshletBuffers(CommandList& commandList)
{
//Modify Begin:2026-07-30 by BestHui
    m_MeshletSceneResources.Upload(commandList);
//Modify End
}
//Modify End

void RaytracingDemoSceneResources::BuildRayTracingAccelerationStructure(
    CommandList& commandList,
    const RayTracingAccelerationStructureBuildSettings settings)
{
    m_RayTracingAccelerationStructure.ClearInstances();
    m_StressTestSphereRayTracingInstances.clear();
    for (size_t objectIndex = 0; objectIndex < m_SceneObjects.size(); ++objectIndex)
    {
        std::vector<RayTracingInstanceHandle>* instanceHandles =
            m_StressTestSpheresEnabled &&
            !m_StressTestSphereObjects.empty() &&
            objectIndex >= m_StressTestSphereObjectStart
            ? &m_StressTestSphereRayTracingInstances
            : nullptr;
        AddRayTracingInstances(m_RayTracingAccelerationStructure, m_SceneObjects[objectIndex], instanceHandles);
    }
    m_RayTracingAccelerationStructure.Build(commandList, settings);
    UploadRayTracingMaterialBuffer(commandList);
    UploadRayTracingGeometryBuffer(commandList, m_RayTracingAccelerationStructure);
}

void RaytracingDemoSceneResources::AddRayTracingInstances(
    RayTracingAccelerationStructure& accelerationStructure,
    const RaytracingDemoSceneObject& object,
    std::vector<RayTracingInstanceHandle>* instanceHandles) const
{
    Assert(object.GeometryIndex < m_SceneGeometries.size(), "Scene object geometry index is invalid.");
    const RaytracingDemoSceneGeometry& geometry = m_SceneGeometries[object.GeometryIndex];
    for (const std::shared_ptr<Mesh>& mesh : geometry.Model->GetMeshes())
    {
        const RayTracingInstanceHandle handle = accelerationStructure.AddInstance({
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

void RaytracingDemoSceneResources::UploadRayTracingMaterialBuffer(CommandList& commandList)
{
    commandList.CopyStructuredBuffer(m_MaterialBuffer, m_Materials);
}

void RaytracingDemoSceneResources::UploadRayTracingGeometryBuffer(
    CommandList& commandList,
    const RayTracingAccelerationStructure& accelerationStructure)
{
//Modify Begin:2026-07-30 by BestHui
    std::vector<RayTracingGeometryData> geometryData = accelerationStructure.GetGeometryData();
    const std::vector<std::shared_ptr<Mesh>>& meshes = accelerationStructure.GetMeshes();
    std::vector<uint32_t> vertexBufferDescriptorIndices(meshes.size());
    std::vector<uint32_t> indexBufferDescriptorIndices(meshes.size());

    for (uint32_t meshIndex = 0; meshIndex < meshes.size(); ++meshIndex)
    {
        const Mesh& mesh = *meshes[meshIndex];
        vertexBufferDescriptorIndices[meshIndex] = m_BindlessDescriptorHeap.AddShaderResourceView(mesh.GetVertexBuffer());
        indexBufferDescriptorIndices[meshIndex] = m_BindlessDescriptorHeap.AddShaderResourceView(mesh.GetIndexBuffer());
    }

    for (RayTracingGeometryData& geometry : geometryData)
    {
        Assert(geometry.VertexBufferIndex < vertexBufferDescriptorIndices.size(), "Ray tracing vertex buffer descriptor index is invalid.");
        Assert(geometry.IndexBufferIndex < indexBufferDescriptorIndices.size(), "Ray tracing index buffer descriptor index is invalid.");
        geometry.VertexBufferIndex = vertexBufferDescriptorIndices[geometry.VertexBufferIndex];
        geometry.IndexBufferIndex = indexBufferDescriptorIndices[geometry.IndexBufferIndex];
    }

    commandList.CopyStructuredBuffer(m_GeometryBuffer, geometryData);
//Modify End
}
//Modify End
