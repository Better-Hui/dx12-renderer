//Modify Begin:2026-08-21 by Hui
#pragma once

#include <DX12Library/Camera.h>

#include <Framework/Scene/Light.h>

#include <DirectXMath.h>

#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

enum class SceneMeshKind
{
    Unknown,
    BuiltinPlane,
    BuiltinCube,
    ExternalMesh
};

enum class SceneEmbeddedTextureEncoding
{
    EncodedFile,
    Bgra8
};

struct SceneEmbeddedTexture
{
    std::string CacheKey;
    std::string FormatHint;
    SceneEmbeddedTextureEncoding Encoding = SceneEmbeddedTextureEncoding::EncodedFile;
    uint32_t Width = 0;
    uint32_t Height = 0;
    std::vector<uint8_t> Data;

    bool IsValid() const { return !Data.empty(); }
};

struct SceneTextureBinding
{
    std::filesystem::path AssetPath;
    std::shared_ptr<const SceneEmbeddedTexture> EmbeddedTexture;
    DirectX::XMFLOAT4 ScaleOffset = { 1.0f, 1.0f, 0.0f, 0.0f };

    bool IsValid() const;
};

struct SceneMaterial
{
    std::string Name;
    std::string SourceId;
    DirectX::XMFLOAT4 BaseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT4 SpecColor = { 0.2f, 0.2f, 0.2f, 1.0f };
    DirectX::XMFLOAT4 EmissionColor = { 0.0f, 0.0f, 0.0f, 1.0f };
    SceneTextureBinding BaseMap;
    SceneTextureBinding NormalMap;
    SceneTextureBinding MetallicGlossMap;
    SceneTextureBinding MetallicMap;
    SceneTextureBinding RoughnessMap;
    SceneTextureBinding OcclusionMap;
    SceneTextureBinding EmissionMap;
    float Metallic = 0.0f;
    float Roughness = 0.5f;
    float OcclusionStrength = 1.0f;
    float NormalScale = 1.0f;
    bool IsPbrMaterial = false;
};

struct SceneMeshReference
{
    static constexpr uint32_t InvalidSubmeshIndex = (std::numeric_limits<uint32_t>::max)();

    SceneMeshKind Kind = SceneMeshKind::Unknown;
    std::filesystem::path AssetPath;
    std::string SubmeshName;
    uint32_t SubmeshIndex = InvalidSubmeshIndex;
};

struct SceneNode
{
    static constexpr uint32_t InvalidNodeIndex = (std::numeric_limits<uint32_t>::max)();

    std::string Name;
    std::string SourceId;
    DirectX::XMMATRIX LocalMatrix = DirectX::XMMatrixIdentity();
    DirectX::XMMATRIX WorldMatrix = DirectX::XMMatrixIdentity();
    uint32_t ParentIndex = InvalidNodeIndex;
    std::vector<uint32_t> Children;
};

struct SceneObject
{
    std::string Name;
    DirectX::XMMATRIX WorldMatrix = DirectX::XMMatrixIdentity();
    SceneMeshReference Mesh;
    uint32_t MaterialIndex = 0;
    uint32_t NodeIndex = SceneNode::InvalidNodeIndex;
};

struct SceneCameraSourceBinding
{
    int64_t ObjectId = 0;
    int64_t TransformId = 0;
    int64_t ParentTransformId = 0;
    DirectX::XMFLOAT3 LocalPosition = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT4 LocalRotation = { 0.0f, 0.0f, 0.0f, 1.0f };
    uint32_t NodeIndex = SceneNode::InvalidNodeIndex;
};

struct SceneCamera
{
    std::string Name;
    std::shared_ptr<Camera> RuntimeCamera = std::make_shared<Camera>();
    bool Orthographic = false;
    float FieldOfView = 60.0f;
    float NearClipPlane = 0.3f;
    float FarClipPlane = 1000.0f;
    SceneCameraSourceBinding SourceBinding;
};

struct SceneSkybox
{
    DirectX::XMFLOAT4 AmbientColorAndIntensity = { 0.0f, 0.0f, 0.0f, 1.0f };
    SceneTextureBinding Texture;
};

struct SceneLightGroupSettings
{
    bool DirectionalLightsEnabled = true;
    bool PointLightsEnabled = true;
    bool AreaLightsEnabled = true;
};

class Scene final
{
public:
    void Clear();

    void SetSourcePaths(
        std::filesystem::path sourcePath,
        std::filesystem::path projectRoot,
        std::filesystem::path assetsRoot);

    void SetCamera(const SceneCamera& camera);
    void SetSkybox(const SceneSkybox& skybox);
    void SetLightGroupSettings(const SceneLightGroupSettings& settings);

    uint32_t AddNode(SceneNode node);
    void SetNodeParent(uint32_t nodeIndex, uint32_t parentIndex);
    uint32_t AddMaterial(SceneMaterial material);
    void AddObject(SceneObject object);
    void AddDirectionalLight(const DirectionalLight& light);
    void AddPointLight(const PointLight& light);
    void AddSpotLight(const SpotLight& light);
    void AddAreaLight(const AreaLight& light);
    void SetDirectionalLights(std::vector<DirectionalLight> lights);
    void SetPointLights(std::vector<PointLight> lights);
    void SetSpotLights(std::vector<SpotLight> lights);
    void SetAreaLights(std::vector<AreaLight> lights);

    void UpdateCamera(const Camera& camera, float fieldOfView, float nearClipPlane, float farClipPlane);

    const std::filesystem::path& GetSourcePath() const;
    const std::filesystem::path& GetProjectRoot() const;
    const std::filesystem::path& GetAssetsRoot() const;
    const SceneSkybox& GetSkybox() const;
    const SceneLightGroupSettings& GetLightGroupSettings() const;
    const SceneCamera& GetCamera() const;
    bool HasCamera() const;
    SceneCamera& GetMutableCamera();
    Camera& GetRuntimeCamera();
    const Camera& GetRuntimeCamera() const;
    const std::vector<SceneNode>& GetNodes() const;
    const std::vector<SceneObject>& GetObjects() const;
    const std::vector<SceneMaterial>& GetMaterials() const;
    const std::vector<DirectionalLight>& GetDirectionalLights() const;
    const std::vector<PointLight>& GetPointLights() const;
    const std::vector<SpotLight>& GetSpotLights() const;
    const std::vector<AreaLight>& GetAreaLights() const;

private:
    std::filesystem::path m_SourcePath;
    std::filesystem::path m_ProjectRoot;
    std::filesystem::path m_AssetsRoot;
    SceneSkybox m_Skybox;
    SceneLightGroupSettings m_LightGroupSettings;
    SceneCamera m_Camera;
    std::vector<SceneNode> m_Nodes;
    std::vector<SceneObject> m_Objects;
    std::vector<SceneMaterial> m_Materials;
    std::vector<DirectionalLight> m_DirectionalLights;
    std::vector<PointLight> m_PointLights;
    std::vector<SpotLight> m_SpotLights;
    std::vector<AreaLight> m_AreaLights;
};
//Modify End
