//Modify Begin:2026-07-27 by BestHui
#include <Scene/SceneResources.h>

#include <DX12Library/CommandList.h>
#include <Framework/Mesh.h>
#include <Framework/Model.h>
#include <Framework/ModelLoader.h>
#include <Framework/RayTracingAccelerationStructure.h>
#include <Framework/UnitySceneParser.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <unordered_map>
#include <optional>
#include <regex>

using namespace DirectX;

namespace
{
    bool IsUnityBuiltinMesh(const UnityAssetReference& mesh)
    {
        return mesh.Guid == "0000000000000000e000000000000000";
    }

    XMMATRIX BuildUnityWorldMatrix(const UnityTransformInfo& transform)
    {
        const XMVECTOR rotation = XMVectorSet(
            transform.WorldRotation.X,
            transform.WorldRotation.Y,
            transform.WorldRotation.Z,
            transform.WorldRotation.W);

        return
            XMMatrixScaling(transform.WorldScale.X, transform.WorldScale.Y, transform.WorldScale.Z) *
            XMMatrixRotationQuaternion(rotation) *
            XMMatrixTranslation(transform.WorldPosition.X, transform.WorldPosition.Y, transform.WorldPosition.Z);
    }

    std::string ToLower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
        return value;
    }

//Modify Begin:2026-07-30 by BestHui
    std::string Trim(std::string value)
    {
        const auto isNotSpace = [](const unsigned char character)
        {
            return !std::isspace(character);
        };
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), isNotSpace));
        value.erase(std::find_if(value.rbegin(), value.rend(), isNotSpace).base(), value.end());
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

        throw std::runtime_error("Unity imported mesh fileID resolved to a mesh name that does not exist in the FBX.");
    }

    std::unordered_map<int64_t, std::string> LoadUnityMeshFileIdNameMap(const std::filesystem::path& meshPath)
    {
        std::unordered_map<int64_t, std::string> result;
        const std::filesystem::path metaPath = meshPath.wstring() + L".meta";
        std::ifstream file(metaPath);
        if (!file.is_open())
        {
            return result;
        }

        static const std::regex oldStyleEntryRegex(R"(^\s*(-?\d+):\s*(.+?)\s*$)");
        static const std::regex firstRegex(R"(^\s*-\s*first:\s*\{fileID:\s*(-?\d+)\}\s*$)");
        static const std::regex secondRegex(R"(^\s*second:\s*(.+?)\s*$)");

        bool inOldStyleMap = false;
        bool inInternalIdMap = false;
        std::optional<int64_t> pendingFileId;
        std::string line;
        while (std::getline(file, line))
        {
            const std::string trimmed = Trim(line);
            if (trimmed.rfind("fileIDToRecycleName:", 0) == 0)
            {
                inOldStyleMap = true;
                inInternalIdMap = false;
                continue;
            }
            if (trimmed.rfind("internalIDToNameTable:", 0) == 0)
            {
                inOldStyleMap = false;
                inInternalIdMap = true;
                continue;
            }

            std::smatch match;
            if (inOldStyleMap && std::regex_match(line, match, oldStyleEntryRegex))
            {
                result[std::stoll(match[1].str())] = Trim(match[2].str());
                continue;
            }

            if (inInternalIdMap && std::regex_match(line, match, firstRegex))
            {
                pendingFileId = std::stoll(match[1].str());
                continue;
            }

            if (inInternalIdMap && pendingFileId.has_value() && std::regex_match(line, match, secondRegex))
            {
                result[*pendingFileId] = Trim(match[1].str());
                pendingFileId.reset();
            }
        }

        return result;
    }

    void AddSceneMeshFileIdNameHints(
        const UnitySceneData& scene,
        const std::string& meshGuid,
        std::unordered_map<int64_t, std::string>& fileIdToName)
    {
        if (!fileIdToName.empty())
        {
            return;
        }

        for (const UnitySceneObject& sceneObject : scene.Objects)
        {
            if (sceneObject.Mesh.Guid == meshGuid && sceneObject.Mesh.FileId != 0 && !sceneObject.Name.empty())
            {
                fileIdToName.emplace(sceneObject.Mesh.FileId, sceneObject.Name);
            }
        }
    }
//Modify End

    const MeshPrototype& FindUnityImportedMeshPrototype(
        const std::vector<MeshPrototype>& prototypes,
        const UnitySceneObject& object,
        const std::unordered_map<int64_t, std::string>& fileIdToName)
    {
        if (prototypes.empty())
        {
            throw std::runtime_error("Unity imported mesh asset contains no mesh prototypes.");
        }

//Modify Begin:2026-07-30 by BestHui
        if (object.Mesh.FileId != 0)
        {
            const auto nameIterator = fileIdToName.find(object.Mesh.FileId);
            if (nameIterator != fileIdToName.end())
            {
                return FindMeshPrototypeByName(prototypes, nameIterator->second);
            }
        }

        throw std::runtime_error("Unity imported mesh fileID does not have a mesh name mapping.");
//Modify End
    }

    std::wstring ToWidePath(const std::filesystem::path& path)
    {
        return path.wstring();
    }

    std::string ToUtf8Path(const std::filesystem::path& path)
    {
        return path.string();
    }

    RaytracingDemoMaterialData MakeUnityMaterial(
        const UnityMaterialInfo& material,
        const uint32_t diffuseTextureIndex,
        const uint32_t normalTextureIndex,
        const uint32_t metallicTextureIndex,
        const uint32_t roughnessTextureIndex,
        const uint32_t ambientOcclusionTextureIndex)
    {
        RaytracingDemoMaterialData output{};
        output.Diffuse = {
            material.BaseColor.R,
            material.BaseColor.G,
            material.BaseColor.B,
            material.BaseColor.A
        };
        output.Specular = {
            material.SpecColor.R,
            material.SpecColor.G,
            material.SpecColor.B,
            material.SpecColor.A
        };
        const UnityTextureBinding& baseTexture = material.BaseMap.Texture.IsValid() ? material.BaseMap : material.MainTex;
        output.TilingOffset = {
            baseTexture.Scale.X,
            baseTexture.Scale.Y,
            baseTexture.Offset.X,
            baseTexture.Offset.Y
        };
        output.DiffuseTextureIndex = diffuseTextureIndex;
        output.NormalTextureIndex = normalTextureIndex;
        output.MetallicTextureIndex = metallicTextureIndex;
        output.RoughnessTextureIndex = roughnessTextureIndex;
        output.AmbientOcclusionTextureIndex = ambientOcclusionTextureIndex;
        output.HasDiffuseMap = material.BaseMap.Texture.IsValid() || material.MainTex.Texture.IsValid() ? 1u : 0u;
        output.HasNormalMap = material.NormalMap.Texture.IsValid() ? 1u : 0u;
        output.HasMetallicMap = material.MetallicGlossMap.Texture.IsValid() ? 1u : 0u;
        output.HasRoughnessMap = material.MetallicGlossMap.Texture.IsValid() ? 1u : 0u;
        output.HasAmbientOcclusionMap = material.OcclusionMap.Texture.IsValid() ? 1u : 0u;
        output.Metallic = material.Metallic;
        output.Roughness = std::clamp(1.0f - material.Smoothness, 0.02f, 1.0f);
        return output;
    }

}

RaytracingDemoSceneResources::RaytracingDemoSceneResources()
    : m_MaterialBuffer(L"Ray Tracing Materials")
    , m_GeometryBuffer(L"Ray Tracing Geometry Data")
{
}

void RaytracingDemoSceneResources::Clear()
{
    m_GeometryBuffer = StructuredBuffer(L"Ray Tracing Geometry Data");
    m_MaterialBuffer = StructuredBuffer(L"Ray Tracing Materials");
    m_SceneObjects.clear();
    m_Materials.clear();
    m_Textures.clear();
}

uint32_t RaytracingDemoSceneResources::AddTexture(CommandList& commandList, const std::wstring& path, TextureUsageType usage)
{
    auto texture = std::make_shared<Texture>();
    commandList.LoadTextureFromFile(*texture, path, usage);
    m_Textures.push_back(texture);
    return static_cast<uint32_t>(m_Textures.size() - 1);
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
        const XMMATRIX worldMatrix = XMMatrixScaling(30.0f, 30.0f, 30.0f) * XMMatrixTranslation(-50.0f, 0.1f, 15.0f);
        m_SceneObjects.push_back({ worldMatrix, model, mirrorMaterial });
    }

    {
        auto model = modelLoader.LoadExisting(Mesh::CreateCube(commandList));
        const XMMATRIX worldMatrix = XMMatrixScaling(5.0f, 5.0f, 5.0f) * XMMatrixTranslation(-54.0f, 2.5f, 7.0f);
        m_SceneObjects.push_back({ worldMatrix, model, cubeMaterial });
    }

    {
        auto model = modelLoader.Load(commandList, "Assets/Models/cerberus/Cerberus_LP.FBX");
        const XMMATRIX worldMatrix =
            XMMatrixScaling(0.10f, 0.10f, 0.10f) *
            XMMatrixRotationRollPitchYaw(XMConvertToRadians(90.0f), XMConvertToRadians(135.0f), 0.0f) *
            XMMatrixTranslation(15.0f, 5.0f, 10.0f);
        m_SceneObjects.push_back({ worldMatrix, model, cerberusMaterial });
    }

    {
        auto model = modelLoader.Load(commandList, "Assets/Models/tv/TV.FBX");
        const XMMATRIX worldMatrix =
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
            const XMMATRIX worldMatrix = XMMatrixTranslation(x * 1.5f, 5.0f + y * 2.0f, 25.0f);
            m_SceneObjects.push_back({ worldMatrix, model, material });
        }
    }
}

bool RaytracingDemoSceneResources::LoadUnityScene(CommandList& commandList, const UnitySceneData& scene)
{
    if (scene.Objects.empty())
    {
        return false;
    }

    ModelLoader modelLoader;
    const uint32_t whiteTexture = AddTexture(commandList, L"Assets/Textures/white.png");
    const auto addUnityTextureOrFallback = [this, &commandList, whiteTexture](
        const UnityTextureBinding& binding,
        const TextureUsageType usage) -> uint32_t
    {
        if (!binding.Texture.AssetPath.empty() && std::filesystem::exists(binding.Texture.AssetPath))
        {
            return AddTexture(commandList, ToWidePath(binding.Texture.AssetPath), usage);
        }
        return whiteTexture;
    };

    std::unordered_map<std::string, uint32_t> materialByGuid;
    materialByGuid.reserve(scene.Materials.size());
    for (const UnityMaterialInfo& unityMaterial : scene.Materials)
    {
        if (unityMaterial.Reference.Guid.empty())
        {
            continue;
        }

        if (!unityMaterial.IsPbrMaterial)
        {
            materialByGuid.insert_or_assign(
                unityMaterial.Reference.Guid,
                AddDiffuseMaterial({ 0.8f, 0.8f, 0.8f, 1.0f }, { 1, 1, 0, 0 }, whiteTexture, 0.0f, 0.5f));
            continue;
        }

        const UnityTextureBinding& diffuseBinding = unityMaterial.BaseMap.Texture.IsValid()
            ? unityMaterial.BaseMap
            : unityMaterial.MainTex;
        const uint32_t diffuseTexture = addUnityTextureOrFallback(diffuseBinding, TextureUsageType::Albedo);
        const uint32_t normalTexture = addUnityTextureOrFallback(unityMaterial.NormalMap, TextureUsageType::Normalmap);
        const uint32_t metallicTexture = addUnityTextureOrFallback(unityMaterial.MetallicGlossMap, TextureUsageType::Other);
        const uint32_t roughnessTexture = addUnityTextureOrFallback(unityMaterial.MetallicGlossMap, TextureUsageType::Other);
        const uint32_t occlusionTexture = addUnityTextureOrFallback(unityMaterial.OcclusionMap, TextureUsageType::Other);

        materialByGuid.insert_or_assign(
            unityMaterial.Reference.Guid,
            AddMaterial(MakeUnityMaterial(
                unityMaterial,
                diffuseTexture,
                normalTexture,
                metallicTexture,
                roughnessTexture,
                occlusionTexture)));
    }

    const uint32_t defaultMaterial = AddDiffuseMaterial({ 0.85f, 0.85f, 0.85f, 1.0f }, { 1, 1, 0, 0 }, whiteTexture, 0.0f, 0.45f);
    std::unordered_map<std::string, std::vector<MeshPrototype>> importedMeshPrototypeCache;
//Modify Begin:2026-07-30 by BestHui
    std::unordered_map<std::string, std::unordered_map<int64_t, std::string>> importedMeshFileIdNameCache;
//Modify End

    for (const UnitySceneObject& object : scene.Objects)
    {
//Modify Begin:2026-07-30 by BestHui
        if (!object.Active || !object.RendererEnabled || !object.Mesh.IsValid())
//Modify End
        {
            continue;
        }

        uint32_t materialIndex = defaultMaterial;
        if (!object.Materials.empty())
        {
            const auto material = materialByGuid.find(object.Materials.front().Guid);
            if (material != materialByGuid.end())
            {
                materialIndex = material->second;
            }
        }

        if (IsUnityBuiltinMesh(object.Mesh))
        {
            if (object.Mesh.FileId == 10209)
            {
                auto model = modelLoader.LoadExisting(Mesh::CreatePlane(commandList, 10.0f, 10.0f));
                m_SceneObjects.push_back({ BuildUnityWorldMatrix(object.Transform), model, materialIndex });
            }
            continue;
        }

        if (object.Mesh.AssetPath.empty())
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

//Modify Begin:2026-07-30 by BestHui
        auto fileIdNameIterator = importedMeshFileIdNameCache.find(meshKey);
        if (fileIdNameIterator == importedMeshFileIdNameCache.end())
        {
            auto fileIdToName = LoadUnityMeshFileIdNameMap(meshPath);
            AddSceneMeshFileIdNameHints(scene, object.Mesh.Guid, fileIdToName);
            fileIdNameIterator = importedMeshFileIdNameCache.emplace(meshKey, std::move(fileIdToName)).first;
        }

        const MeshPrototype& prototype = FindUnityImportedMeshPrototype(prototypeIterator->second, object, fileIdNameIterator->second);
//Modify End
        auto model = modelLoader.Load(commandList, std::vector<MeshPrototype>{ prototype });
        m_SceneObjects.push_back({ BuildUnityWorldMatrix(object.Transform), model, materialIndex });
    }

    return !m_SceneObjects.empty();
}

void RaytracingDemoSceneResources::AddRayTracingInstances(RayTracingAccelerationStructure& accelerationStructure) const
{
    accelerationStructure.ClearInstances();

    for (const RaytracingDemoSceneObject& object : m_SceneObjects)
    {
        for (const auto& mesh : object.Model->GetMeshes())
        {
            accelerationStructure.AddInstance({
                mesh,
                object.WorldMatrix,
                object.MaterialIndex
            });
        }
    }
}

void RaytracingDemoSceneResources::UploadRayTracingBuffers(
    CommandList& commandList,
    const RayTracingAccelerationStructure& accelerationStructure)
{
    commandList.CopyStructuredBuffer(m_MaterialBuffer, m_Materials);
    commandList.CopyStructuredBuffer(m_GeometryBuffer, accelerationStructure.GetGeometryData());
}
//Modify End
