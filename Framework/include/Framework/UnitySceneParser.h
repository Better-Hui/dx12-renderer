//Modify Begin:2026-07-29 by BestHui
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct UnityAssetReference
{
    int64_t FileId = 0;
    std::string Guid;
    int Type = 0;
    std::filesystem::path AssetPath;

    bool IsValid() const { return FileId != 0 || !Guid.empty(); }
};

struct UnityVector3
{
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
};

struct UnityQuaternion
{
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
    float W = 1.0f;
};

struct UnityColor
{
    float R = 1.0f;
    float G = 1.0f;
    float B = 1.0f;
    float A = 1.0f;
};

struct UnityTransformInfo
{
    int64_t FileId = 0;
    int64_t GameObjectId = 0;
    int64_t ParentTransformId = 0;
    UnityVector3 LocalPosition;
    UnityQuaternion LocalRotation;
    UnityVector3 LocalScale = { 1.0f, 1.0f, 1.0f };
    UnityVector3 WorldPosition;
    UnityQuaternion WorldRotation;
    UnityVector3 WorldScale = { 1.0f, 1.0f, 1.0f };
};

struct UnitySceneObject
{
    int64_t GameObjectId = 0;
    std::string Name;
    bool Active = true;
    UnityTransformInfo Transform;
    UnityAssetReference Mesh;
    std::vector<UnityAssetReference> Materials;
};

struct UnityCameraInfo
{
    int64_t GameObjectId = 0;
    std::string Name;
    bool Enabled = true;
    bool Orthographic = false;
    float FieldOfView = 60.0f;
    float NearClipPlane = 0.3f;
    float FarClipPlane = 1000.0f;
    UnityTransformInfo Transform;
};

enum class UnityLightType
{
    Spot = 0,
    Directional = 1,
    Point = 2,
    Area = 3,
    Unknown = 255
};

struct UnityLightInfo
{
    int64_t GameObjectId = 0;
    std::string Name;
    bool Enabled = true;
    UnityLightType Type = UnityLightType::Unknown;
    UnityColor Color;
    float Intensity = 1.0f;
    float Range = 10.0f;
    float SpotAngle = 30.0f;
    UnityVector3 AreaSize = { 1.0f, 1.0f, 0.0f };
    UnityTransformInfo Transform;
};

struct UnityMaterialInfo
{
    UnityAssetReference Reference;
    std::string Name;
    UnityAssetReference Shader;
};

struct UnitySceneData
{
    std::filesystem::path ScenePath;
    std::filesystem::path ProjectRoot;
    std::filesystem::path AssetsRoot;
    std::vector<UnitySceneObject> Objects;
    std::vector<UnityCameraInfo> Cameras;
    std::vector<UnityLightInfo> Lights;
    std::vector<UnityMaterialInfo> Materials;
};

struct UnitySceneParseOptions
{
    bool ResolveAssetPaths = true;
    bool ParseMaterialAssets = true;
};

class UnitySceneParser
{
public:
    static UnitySceneData ParseFromFile(
        const std::filesystem::path& scenePath,
        const UnitySceneParseOptions& options = {});
};
//Modify End
