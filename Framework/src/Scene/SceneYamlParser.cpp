//Modify Begin:2026-07-29 by BestHui
#include <Framework/Scene/SceneYamlParser.h>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace
{
//Modify Begin:2026-07-30 by BestHui
    std::string Trim(const std::string& value);
    bool IsUnityTopLevelProperty(const std::string& line, const char* name);
    float ParseFloatAfterColon(const std::string& line, float fallback);
    int ParseIntAfterColon(const std::string& line, int fallback);
    UnityAssetReference ParseAssetReference(const std::string& line);
    UnityColor ParseColor(const std::string& line);
//Modify End

    struct UnityDocument
    {
        int ClassId = 0;
        int64_t FileId = 0;
        std::vector<std::string> Lines;
    };

    struct GameObjectData
    {
        int64_t FileId = 0;
        std::string Name;
        bool Active = true;
    };

    struct CameraData
    {
        int64_t GameObjectId = 0;
        bool Enabled = true;
        bool Orthographic = false;
        float FieldOfView = 60.0f;
        float NearClipPlane = 0.3f;
        float FarClipPlane = 1000.0f;
    };

    struct LightData
    {
        int64_t GameObjectId = 0;
        bool Enabled = true;
        UnityLightType Type = UnityLightType::Unknown;
        UnityColor Color;
        float Intensity = 1.0f;
        float Range = 10.0f;
        float SpotAngle = 30.0f;
//Modify Begin:2026-07-30 by BestHui
        float AngularRadius = 0.009f;
        float SourceRadius = 0.25f;
//Modify End
        UnityVector3 AreaSize = { 1.0f, 1.0f, 0.0f };
    };

    UnityRenderSettings ParseRenderSettings(const UnityDocument& document)
    {
        UnityRenderSettings settings;
        for (const std::string& line : document.Lines)
        {
            const std::string trimmed = Trim(line);
            if (IsUnityTopLevelProperty(line, "m_AmbientSkyColor"))
            {
                settings.AmbientSkyColor = ParseColor(trimmed);
            }
            else if (IsUnityTopLevelProperty(line, "m_AmbientEquatorColor"))
            {
                settings.AmbientEquatorColor = ParseColor(trimmed);
            }
            else if (IsUnityTopLevelProperty(line, "m_AmbientGroundColor"))
            {
                settings.AmbientGroundColor = ParseColor(trimmed);
            }
            else if (IsUnityTopLevelProperty(line, "m_AmbientIntensity"))
            {
                settings.AmbientIntensity = ParseFloatAfterColon(trimmed, 1.0f);
            }
            else if (IsUnityTopLevelProperty(line, "m_AmbientMode"))
            {
                settings.AmbientMode = ParseIntAfterColon(trimmed, 0);
            }
            else if (IsUnityTopLevelProperty(line, "m_SkyboxMaterial"))
            {
                settings.SkyboxMaterial = ParseAssetReference(trimmed);
            }
        }
        return settings;
    }

    struct MeshFilterData
    {
        int64_t GameObjectId = 0;
        UnityAssetReference Mesh;
    };

    struct MeshRendererData
    {
        int64_t GameObjectId = 0;
        bool Enabled = true;
        std::vector<UnityAssetReference> Materials;
    };

    std::string Trim(const std::string& value)
    {
        const size_t first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
        {
            return {};
        }
        const size_t last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, last - first + 1);
    }

    bool StartsWith(const std::string& value, const std::string& prefix)
    {
        return value.rfind(prefix, 0) == 0;
    }

    bool IsUnityTopLevelProperty(const std::string& line, const char* name)
    {
        return StartsWith(line, std::string("  ") + name + ":");
    }

    std::optional<std::string> ReadTextFile(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            return std::nullopt;
        }
        std::ostringstream stream;
        stream << file.rdbuf();
        return stream.str();
    }

    std::vector<std::string> SplitLines(const std::string& text)
    {
        std::vector<std::string> lines;
        std::istringstream stream(text);
        std::string line;
        while (std::getline(stream, line))
        {
            lines.push_back(line);
        }
        return lines;
    }

    std::vector<UnityDocument> ParseDocuments(const std::string& text)
    {
        static const std::regex headerRegex(R"(^--- !u!(\d+) &(-?\d+))");
        std::vector<UnityDocument> documents;
        UnityDocument* current = nullptr;

        for (const std::string& line : SplitLines(text))
        {
            std::smatch match;
            if (std::regex_search(line, match, headerRegex))
            {
                documents.push_back({});
                current = &documents.back();
                current->ClassId = std::stoi(match[1].str());
                current->FileId = std::stoll(match[2].str());
                continue;
            }

            if (current != nullptr)
            {
                current->Lines.push_back(line);
            }
        }

        return documents;
    }

//Modify Begin:2026-07-30 by BestHui
    std::string FormatFloat(const float value)
    {
        std::ostringstream stream;
        stream << std::setprecision(9) << value;
        return stream.str();
    }

    std::string FormatUnityVector3(const UnityVector3& value)
    {
        return "{x: " + FormatFloat(value.X) +
            ", y: " + FormatFloat(value.Y) +
            ", z: " + FormatFloat(value.Z) +
            "}";
    }

    std::string FormatUnityQuaternion(const UnityQuaternion& value)
    {
        return "{x: " + FormatFloat(value.X) +
            ", y: " + FormatFloat(value.Y) +
            ", z: " + FormatFloat(value.Z) +
            ", w: " + FormatFloat(value.W) +
            "}";
    }

    std::string ExtractLineIndent(const std::string& line)
    {
        const size_t firstNonSpace = line.find_first_not_of(' ');
        return firstNonSpace == std::string::npos ? std::string{} : line.substr(0, firstNonSpace);
    }

    bool IsDocumentHeader(const std::string& line)
    {
        return StartsWith(line, "--- !u!");
    }

    bool TryParseDocumentHeader(const std::string& line, int& classId, int64_t& fileId)
    {
        if (!IsDocumentHeader(line))
        {
            return false;
        }

        const size_t classBegin = line.find("!u!");
        const size_t fileBegin = line.find('&');
        if (classBegin == std::string::npos || fileBegin == std::string::npos || fileBegin <= classBegin + 3)
        {
            return false;
        }

        classId = std::stoi(line.substr(classBegin + 3, fileBegin - classBegin - 3));
        fileId = std::stoll(line.substr(fileBegin + 1));
        return true;
    }
//Modify End

    int64_t ParseFileId(const std::string& line)
    {
        static const std::regex fileIdRegex(R"(fileID:\s*(-?\d+))");
        std::smatch match;
        return std::regex_search(line, match, fileIdRegex) ? std::stoll(match[1].str()) : 0;
    }

    float ParseFloatAfterColon(const std::string& line, const float fallback = 0.0f)
    {
        const size_t colon = line.find(':');
        if (colon == std::string::npos)
        {
            return fallback;
        }
        return std::stof(Trim(line.substr(colon + 1)));
    }

    int ParseIntAfterColon(const std::string& line, const int fallback = 0)
    {
        const size_t colon = line.find(':');
        if (colon == std::string::npos)
        {
            return fallback;
        }
        return std::stoi(Trim(line.substr(colon + 1)));
    }

    std::string ParseStringAfterColon(const std::string& line)
    {
        const size_t colon = line.find(':');
        return colon == std::string::npos ? std::string{} : Trim(line.substr(colon + 1));
    }

    float ParseNamedFloat(const std::string& line, const char* name, const float fallback = 0.0f)
    {
        const std::regex valueRegex(std::string(name) + R"(:\s*([-+0-9.eE]+))");
        std::smatch match;
        return std::regex_search(line, match, valueRegex) ? std::stof(match[1].str()) : fallback;
    }

    UnityVector3 ParseVector3(const std::string& line)
    {
        return {
            ParseNamedFloat(line, "x"),
            ParseNamedFloat(line, "y"),
            ParseNamedFloat(line, "z")
        };
    }

    UnityQuaternion ParseQuaternion(const std::string& line)
    {
        return {
            ParseNamedFloat(line, "x"),
            ParseNamedFloat(line, "y"),
            ParseNamedFloat(line, "z"),
            ParseNamedFloat(line, "w", 1.0f)
        };
    }

    UnityColor ParseColor(const std::string& line)
    {
        return {
            ParseNamedFloat(line, "r", 1.0f),
            ParseNamedFloat(line, "g", 1.0f),
            ParseNamedFloat(line, "b", 1.0f),
            ParseNamedFloat(line, "a", 1.0f)
        };
    }

    UnityTextureBinding* FindTextureBinding(UnityMaterialInfo& material, const std::string& propertyName)
    {
        if (propertyName == "_BaseMap")
        {
            return &material.BaseMap;
        }
//Modify Begin:2026-07-30 by BestHui
        if (propertyName == "_MainTex" || propertyName == "_Tex")
//Modify End
        {
            return &material.MainTex;
        }
        if (propertyName == "_BumpMap")
        {
            return &material.NormalMap;
        }
        if (propertyName == "_MetallicGlossMap" || propertyName == "_SpecGlossMap")
        {
            return &material.MetallicGlossMap;
        }
        if (propertyName == "_OcclusionMap")
        {
            return &material.OcclusionMap;
        }
        if (propertyName == "_EmissionMap")
        {
            return &material.EmissionMap;
        }
        return nullptr;
    }

    UnityVector3 Add(const UnityVector3& lhs, const UnityVector3& rhs)
    {
        return { lhs.X + rhs.X, lhs.Y + rhs.Y, lhs.Z + rhs.Z };
    }

    UnityVector3 Mul(const UnityVector3& lhs, const UnityVector3& rhs)
    {
        return { lhs.X * rhs.X, lhs.Y * rhs.Y, lhs.Z * rhs.Z };
    }

    UnityQuaternion Mul(const UnityQuaternion& lhs, const UnityQuaternion& rhs)
    {
        return {
            lhs.W * rhs.X + lhs.X * rhs.W + lhs.Y * rhs.Z - lhs.Z * rhs.Y,
            lhs.W * rhs.Y - lhs.X * rhs.Z + lhs.Y * rhs.W + lhs.Z * rhs.X,
            lhs.W * rhs.Z + lhs.X * rhs.Y - lhs.Y * rhs.X + lhs.Z * rhs.W,
            lhs.W * rhs.W - lhs.X * rhs.X - lhs.Y * rhs.Y - lhs.Z * rhs.Z
        };
    }

    UnityVector3 Rotate(const UnityQuaternion& rotation, const UnityVector3& value)
    {
        const UnityQuaternion vector = { value.X, value.Y, value.Z, 0.0f };
        const UnityQuaternion inverse = { -rotation.X, -rotation.Y, -rotation.Z, rotation.W };
        const UnityQuaternion result = Mul(Mul(rotation, vector), inverse);
        return { result.X, result.Y, result.Z };
    }

    UnityAssetReference ParseAssetReference(const std::string& line)
    {
        UnityAssetReference reference;
        reference.FileId = ParseFileId(line);

        static const std::regex guidRegex(R"(guid:\s*([0-9a-fA-F]+))");
        std::smatch guidMatch;
        if (std::regex_search(line, guidMatch, guidRegex))
        {
            reference.Guid = guidMatch[1].str();
        }

        static const std::regex typeRegex(R"(type:\s*(-?\d+))");
        std::smatch typeMatch;
        if (std::regex_search(line, typeMatch, typeRegex))
        {
            reference.Type = std::stoi(typeMatch[1].str());
        }

        return reference;
    }

    UnityTransformInfo ParseTransform(const UnityDocument& document)
    {
        UnityTransformInfo transform;
        transform.FileId = document.FileId;
        for (const std::string& line : document.Lines)
        {
            const std::string trimmed = Trim(line);
            if (StartsWith(trimmed, "m_GameObject:"))
            {
                transform.GameObjectId = ParseFileId(trimmed);
            }
            else if (StartsWith(trimmed, "m_LocalPosition:"))
            {
                transform.LocalPosition = ParseVector3(trimmed);
            }
            else if (StartsWith(trimmed, "m_LocalRotation:"))
            {
                transform.LocalRotation = ParseQuaternion(trimmed);
            }
            else if (StartsWith(trimmed, "m_LocalScale:"))
            {
                transform.LocalScale = ParseVector3(trimmed);
            }
            else if (StartsWith(trimmed, "m_Father:"))
            {
                transform.ParentTransformId = ParseFileId(trimmed);
            }
        }
        return transform;
    }

    GameObjectData ParseGameObject(const UnityDocument& document)
    {
        GameObjectData gameObject;
        gameObject.FileId = document.FileId;
        for (const std::string& line : document.Lines)
        {
            const std::string trimmed = Trim(line);
            if (StartsWith(trimmed, "m_Name:"))
            {
                gameObject.Name = ParseStringAfterColon(trimmed);
            }
            else if (StartsWith(trimmed, "m_IsActive:"))
            {
                gameObject.Active = ParseIntAfterColon(trimmed, 1) != 0;
            }
        }
        return gameObject;
    }

    CameraData ParseCamera(const UnityDocument& document)
    {
        CameraData camera;
        for (const std::string& line : document.Lines)
        {
            const std::string trimmed = Trim(line);
            if (StartsWith(trimmed, "m_GameObject:"))
            {
                camera.GameObjectId = ParseFileId(trimmed);
            }
            else if (StartsWith(trimmed, "m_Enabled:"))
            {
                camera.Enabled = ParseIntAfterColon(trimmed, 1) != 0;
            }
            else if (StartsWith(trimmed, "field of view:") || StartsWith(trimmed, "m_FieldOfView:"))
            {
                camera.FieldOfView = ParseFloatAfterColon(trimmed, 60.0f);
            }
            else if (StartsWith(trimmed, "near clip plane:") || StartsWith(trimmed, "m_NearClipPlane:"))
            {
                camera.NearClipPlane = ParseFloatAfterColon(trimmed, 0.3f);
            }
            else if (StartsWith(trimmed, "far clip plane:") || StartsWith(trimmed, "m_FarClipPlane:"))
            {
                camera.FarClipPlane = ParseFloatAfterColon(trimmed, 1000.0f);
            }
            else if (StartsWith(trimmed, "orthographic:") || StartsWith(trimmed, "m_Orthographic:"))
            {
                camera.Orthographic = ParseIntAfterColon(trimmed, 0) != 0;
            }
        }
        return camera;
    }

    LightData ParseLight(const UnityDocument& document)
    {
        LightData light;
        for (const std::string& line : document.Lines)
        {
            const std::string trimmed = Trim(line);
            if (IsUnityTopLevelProperty(line, "m_GameObject"))
            {
                light.GameObjectId = ParseFileId(trimmed);
            }
            else if (IsUnityTopLevelProperty(line, "m_Enabled"))
            {
                light.Enabled = ParseIntAfterColon(trimmed, 1) != 0;
            }
            else if (IsUnityTopLevelProperty(line, "m_Type"))
            {
                const int type = ParseIntAfterColon(trimmed, 255);
                light.Type = type >= 0 && type <= 3 ? static_cast<UnityLightType>(type) : UnityLightType::Unknown;
            }
            else if (IsUnityTopLevelProperty(line, "m_Color"))
            {
                light.Color = ParseColor(trimmed);
            }
            else if (IsUnityTopLevelProperty(line, "m_Intensity"))
            {
                light.Intensity = ParseFloatAfterColon(trimmed, 1.0f);
            }
            else if (IsUnityTopLevelProperty(line, "m_Range"))
            {
                light.Range = ParseFloatAfterColon(trimmed, 10.0f);
            }
            else if (IsUnityTopLevelProperty(line, "m_SpotAngle"))
            {
                light.SpotAngle = ParseFloatAfterColon(trimmed, 30.0f);
            }
//Modify Begin:2026-07-30 by BestHui
            else if (IsUnityTopLevelProperty(line, "m_ShadowAngle"))
            {
                light.AngularRadius = std::max(0.0f, ParseFloatAfterColon(trimmed, 0.5f)) * 0.017453292519943295f;
            }
            else if (IsUnityTopLevelProperty(line, "m_ShadowRadius"))
            {
                light.SourceRadius = std::max(0.0f, ParseFloatAfterColon(trimmed, 0.25f));
            }
//Modify End
            else if (IsUnityTopLevelProperty(line, "m_AreaSize"))
            {
                light.AreaSize = ParseVector3(trimmed);
            }
        }
        return light;
    }

    MeshFilterData ParseMeshFilter(const UnityDocument& document)
    {
        MeshFilterData meshFilter;
        for (const std::string& line : document.Lines)
        {
            const std::string trimmed = Trim(line);
            if (StartsWith(trimmed, "m_GameObject:"))
            {
                meshFilter.GameObjectId = ParseFileId(trimmed);
            }
            else if (StartsWith(trimmed, "m_Mesh:"))
            {
                meshFilter.Mesh = ParseAssetReference(trimmed);
            }
        }
        return meshFilter;
    }

    MeshRendererData ParseMeshRenderer(const UnityDocument& document)
    {
        MeshRendererData meshRenderer;
        bool readingMaterials = false;
        for (const std::string& line : document.Lines)
        {
            const std::string trimmed = Trim(line);
            if (StartsWith(trimmed, "m_GameObject:"))
            {
                meshRenderer.GameObjectId = ParseFileId(trimmed);
            }
            else if (StartsWith(trimmed, "m_Enabled:"))
            {
                meshRenderer.Enabled = ParseIntAfterColon(trimmed, 1) != 0;
            }
            else if (StartsWith(trimmed, "m_Materials:"))
            {
                readingMaterials = true;
            }
            else if (readingMaterials && StartsWith(trimmed, "- "))
            {
                meshRenderer.Materials.push_back(ParseAssetReference(trimmed));
            }
            else if (readingMaterials && !StartsWith(trimmed, "- ") && !trimmed.empty() && trimmed[0] != '{')
            {
                readingMaterials = false;
            }
        }
        return meshRenderer;
    }

    std::filesystem::path FindAssetsRoot(const std::filesystem::path& scenePath)
    {
        std::filesystem::path current = std::filesystem::absolute(scenePath).parent_path();
        while (!current.empty())
        {
            if (current.filename() == "Assets")
            {
                return current;
            }
            current = current.parent_path();
        }
        return {};
    }

    std::unordered_map<std::string, std::filesystem::path> BuildGuidToAssetPathMap(const std::filesystem::path& assetsRoot)
    {
        std::unordered_map<std::string, std::filesystem::path> guidToAssetPath;
        if (assetsRoot.empty() || !std::filesystem::exists(assetsRoot))
        {
            return guidToAssetPath;
        }

        static const std::regex guidRegex(R"(^guid:\s*([0-9a-fA-F]+))");
        for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(assetsRoot))
        {
            if (!entry.is_regular_file() || entry.path().extension() != ".meta")
            {
                continue;
            }

            std::ifstream file(entry.path());
            std::string line;
            while (std::getline(file, line))
            {
                std::smatch match;
                if (std::regex_search(line, match, guidRegex))
                {
                    std::filesystem::path assetPath = entry.path();
                    assetPath.replace_extension();
                    guidToAssetPath.insert_or_assign(match[1].str(), assetPath);
                    break;
                }
            }
        }
        return guidToAssetPath;
    }

    void ResolveAssetReference(
        UnityAssetReference& reference,
        const std::unordered_map<std::string, std::filesystem::path>& guidToAssetPath)
    {
        if (reference.Guid.empty())
        {
            return;
        }

        const auto findResult = guidToAssetPath.find(reference.Guid);
        if (findResult != guidToAssetPath.end())
        {
            reference.AssetPath = findResult->second;
        }
    }

    UnityMaterialInfo ParseMaterialAsset(const UnityAssetReference& reference)
    {
        UnityMaterialInfo material;
        material.Reference = reference;
        material.Name = reference.AssetPath.empty() ? reference.Guid : reference.AssetPath.stem().string();

        const std::optional<std::string> text = ReadTextFile(reference.AssetPath);
        if (!text.has_value())
        {
            return material;
        }

        UnityTextureBinding* currentTextureBinding = nullptr;
        for (const std::string& line : SplitLines(*text))
        {
            const std::string trimmed = Trim(line);
            if (StartsWith(trimmed, "m_Name:"))
            {
                const std::string name = ParseStringAfterColon(trimmed);
                if (!name.empty())
                {
                    material.Name = name;
                }
            }
            else if (StartsWith(trimmed, "m_Shader:"))
            {
                material.Shader = ParseAssetReference(trimmed);
            }
            else if (StartsWith(trimmed, "- _BaseMap:")
//Modify Begin:2026-07-30 by BestHui
                || StartsWith(trimmed, "- _MainTex:")
                || StartsWith(trimmed, "- _Tex:")
//Modify End
                || StartsWith(trimmed, "- _BumpMap:")
                || StartsWith(trimmed, "- _MetallicGlossMap:")
                || StartsWith(trimmed, "- _SpecGlossMap:")
                || StartsWith(trimmed, "- _OcclusionMap:")
                || StartsWith(trimmed, "- _EmissionMap:"))
            {
                const size_t propertyBegin = trimmed.find('_');
                const size_t propertyEnd = trimmed.find(':', propertyBegin);
                const std::string propertyName = propertyEnd == std::string::npos
                    ? std::string{}
                    : trimmed.substr(propertyBegin, propertyEnd - propertyBegin);
                currentTextureBinding = FindTextureBinding(material, propertyName);
            }
            else if (currentTextureBinding != nullptr && StartsWith(trimmed, "m_Texture:"))
            {
                currentTextureBinding->Texture = ParseAssetReference(trimmed);
            }
            else if (currentTextureBinding != nullptr && StartsWith(trimmed, "m_Scale:"))
            {
                currentTextureBinding->Scale = ParseVector3(trimmed);
            }
            else if (currentTextureBinding != nullptr && StartsWith(trimmed, "m_Offset:"))
            {
                currentTextureBinding->Offset = ParseVector3(trimmed);
            }
            else if (StartsWith(trimmed, "- _BaseColor:") || StartsWith(trimmed, "- _Color:"))
            {
                material.BaseColor = ParseColor(trimmed);
                material.IsPbrMaterial = true;
            }
            else if (StartsWith(trimmed, "- _SpecColor:"))
            {
                material.SpecColor = ParseColor(trimmed);
            }
            else if (StartsWith(trimmed, "- _EmissionColor:"))
            {
                material.EmissionColor = ParseColor(trimmed);
            }
            else if (StartsWith(trimmed, "- _Metallic:"))
            {
                material.Metallic = ParseFloatAfterColon(trimmed, 0.0f);
                material.IsPbrMaterial = true;
            }
            else if (StartsWith(trimmed, "- _Smoothness:") || StartsWith(trimmed, "- _Glossiness:"))
            {
                material.Smoothness = ParseFloatAfterColon(trimmed, material.Smoothness);
                material.IsPbrMaterial = true;
            }
            else if (StartsWith(trimmed, "- _OcclusionStrength:"))
            {
                material.OcclusionStrength = ParseFloatAfterColon(trimmed, 1.0f);
            }
            else if (StartsWith(trimmed, "- _BumpScale:"))
            {
                material.NormalScale = ParseFloatAfterColon(trimmed, 1.0f);
            }
            else if (StartsWith(trimmed, "- "))
            {
                currentTextureBinding = nullptr;
            }
        }
        return material;
    }

    void ResolveTextureBinding(
        UnityTextureBinding& binding,
        const std::unordered_map<std::string, std::filesystem::path>& guidToAssetPath)
    {
        ResolveAssetReference(binding.Texture, guidToAssetPath);
    }

    UnityTransformInfo ResolveWorldTransform(
        const UnityTransformInfo& transform,
        const std::map<int64_t, UnityTransformInfo>& transformsByFileId,
        std::set<int64_t>& resolving)
    {
        UnityTransformInfo resolved = transform;
        resolved.WorldPosition = transform.LocalPosition;
        resolved.WorldRotation = transform.LocalRotation;
        resolved.WorldScale = transform.LocalScale;

        if (transform.ParentTransformId == 0 || resolving.contains(transform.FileId))
        {
            return resolved;
        }

        const auto parentIterator = transformsByFileId.find(transform.ParentTransformId);
        if (parentIterator == transformsByFileId.end())
        {
            return resolved;
        }

        resolving.insert(transform.FileId);
        const UnityTransformInfo parent = ResolveWorldTransform(parentIterator->second, transformsByFileId, resolving);
        resolving.erase(transform.FileId);

        resolved.WorldScale = Mul(parent.WorldScale, transform.LocalScale);
        resolved.WorldRotation = Mul(parent.WorldRotation, transform.LocalRotation);
        resolved.WorldPosition = Add(parent.WorldPosition, Rotate(parent.WorldRotation, Mul(parent.WorldScale, transform.LocalPosition)));
        return resolved;
    }
}

UnitySceneData SceneYamlParser::ParseFromFile(
    const std::filesystem::path& scenePath,
    const UnitySceneParseOptions& options)
{
    const std::optional<std::string> sceneText = ReadTextFile(scenePath);
    if (!sceneText.has_value())
    {
        throw std::runtime_error("Unity scene file could not be opened.");
    }

    UnitySceneData scene;
    scene.ScenePath = std::filesystem::absolute(scenePath);
    scene.AssetsRoot = FindAssetsRoot(scene.ScenePath);
    scene.ProjectRoot = scene.AssetsRoot.empty() ? std::filesystem::path{} : scene.AssetsRoot.parent_path();

    std::unordered_map<std::string, std::filesystem::path> guidToAssetPath;
    if (options.ResolveAssetPaths)
    {
        guidToAssetPath = BuildGuidToAssetPathMap(scene.AssetsRoot);
    }

    std::map<int64_t, GameObjectData> gameObjects;
    std::map<int64_t, UnityTransformInfo> transformsByGameObject;
    std::map<int64_t, UnityTransformInfo> transformsByFileId;
    std::map<int64_t, MeshFilterData> meshFiltersByGameObject;
    std::map<int64_t, MeshRendererData> meshRenderersByGameObject;
    std::vector<CameraData> cameras;
    std::vector<LightData> lights;

    for (const UnityDocument& document : ParseDocuments(*sceneText))
    {
        switch (document.ClassId)
        {
        case 104:
            scene.RenderSettings = ParseRenderSettings(document);
            break;
        case 1:
        {
            GameObjectData gameObject = ParseGameObject(document);
            gameObjects.insert_or_assign(gameObject.FileId, std::move(gameObject));
            break;
        }
        case 4:
        {
            UnityTransformInfo transform = ParseTransform(document);
            transformsByFileId.insert_or_assign(transform.FileId, transform);
            transformsByGameObject.insert_or_assign(transform.GameObjectId, std::move(transform));
            break;
        }
        case 20:
            cameras.push_back(ParseCamera(document));
            break;
        case 23:
        {
            MeshRendererData meshRenderer = ParseMeshRenderer(document);
            meshRenderersByGameObject.insert_or_assign(meshRenderer.GameObjectId, std::move(meshRenderer));
            break;
        }
        case 33:
        {
            MeshFilterData meshFilter = ParseMeshFilter(document);
            meshFiltersByGameObject.insert_or_assign(meshFilter.GameObjectId, std::move(meshFilter));
            break;
        }
        case 108:
            lights.push_back(ParseLight(document));
            break;
        default:
            break;
        }
    }

    for (auto& [gameObjectId, transform] : transformsByGameObject)
    {
        std::set<int64_t> resolving;
        transform = ResolveWorldTransform(transform, transformsByFileId, resolving);
    }

    std::unordered_map<std::string, UnityAssetReference> materialReferences;
    for (const auto& [gameObjectId, gameObject] : gameObjects)
    {
        UnitySceneObject object;
        object.GameObjectId = gameObjectId;
        object.Name = gameObject.Name;
        object.Active = gameObject.Active;
        if (const auto transform = transformsByGameObject.find(gameObjectId); transform != transformsByGameObject.end())
        {
            object.Transform = transform->second;
        }
        if (const auto meshFilter = meshFiltersByGameObject.find(gameObjectId); meshFilter != meshFiltersByGameObject.end())
        {
            object.Mesh = meshFilter->second.Mesh;
            ResolveAssetReference(object.Mesh, guidToAssetPath);
        }
        if (const auto meshRenderer = meshRenderersByGameObject.find(gameObjectId); meshRenderer != meshRenderersByGameObject.end())
        {
            object.RendererEnabled = meshRenderer->second.Enabled;
            object.Materials = meshRenderer->second.Materials;
            for (UnityAssetReference& materialReference : object.Materials)
            {
                ResolveAssetReference(materialReference, guidToAssetPath);
                if (!materialReference.Guid.empty())
                {
                    materialReferences.insert_or_assign(
                        materialReference.Guid + ":" + std::to_string(materialReference.FileId),
                        materialReference);
                }
            }
        }
        scene.Objects.push_back(std::move(object));
    }

    for (const CameraData& cameraData : cameras)
    {
        UnityCameraInfo camera;
        camera.GameObjectId = cameraData.GameObjectId;
        camera.Enabled = cameraData.Enabled;
        camera.Orthographic = cameraData.Orthographic;
        camera.FieldOfView = cameraData.FieldOfView;
        camera.NearClipPlane = cameraData.NearClipPlane;
        camera.FarClipPlane = cameraData.FarClipPlane;
        if (const auto gameObject = gameObjects.find(camera.GameObjectId); gameObject != gameObjects.end())
        {
            camera.Name = gameObject->second.Name;
            camera.Enabled = camera.Enabled && gameObject->second.Active;
        }
        if (const auto transform = transformsByGameObject.find(camera.GameObjectId); transform != transformsByGameObject.end())
        {
            camera.Transform = transform->second;
        }
        scene.Cameras.push_back(std::move(camera));
    }

    for (const LightData& lightData : lights)
    {
        UnityLightInfo light;
        light.GameObjectId = lightData.GameObjectId;
        light.Enabled = lightData.Enabled;
        light.Type = lightData.Type;
        light.Color = lightData.Color;
        light.Intensity = lightData.Intensity;
        light.Range = lightData.Range;
        light.SpotAngle = lightData.SpotAngle;
//Modify Begin:2026-07-30 by BestHui
        light.AngularRadius = lightData.AngularRadius;
        light.SourceRadius = lightData.SourceRadius;
//Modify End
        light.AreaSize = lightData.AreaSize;
        if (const auto gameObject = gameObjects.find(light.GameObjectId); gameObject != gameObjects.end())
        {
            light.Name = gameObject->second.Name;
            light.Enabled = light.Enabled && gameObject->second.Active;
        }
        if (const auto transform = transformsByGameObject.find(light.GameObjectId); transform != transformsByGameObject.end())
        {
            light.Transform = transform->second;
        }
        scene.Lights.push_back(std::move(light));
    }

    ResolveAssetReference(scene.RenderSettings.SkyboxMaterial, guidToAssetPath);

    if (options.ParseMaterialAssets)
    {
//Modify Begin:2026-07-30 by BestHui
        if (!scene.RenderSettings.SkyboxMaterial.AssetPath.empty())
        {
            scene.SkyboxMaterial = ParseMaterialAsset(scene.RenderSettings.SkyboxMaterial);
            ResolveAssetReference(scene.SkyboxMaterial.Shader, guidToAssetPath);
            ResolveTextureBinding(scene.SkyboxMaterial.MainTex, guidToAssetPath);
            ResolveTextureBinding(scene.SkyboxMaterial.BaseMap, guidToAssetPath);
            scene.HasSkyboxMaterial = true;
        }
//Modify End
        for (const auto& [materialKey, materialReference] : materialReferences)
        {
            static_cast<void>(materialKey);
            if (materialReference.AssetPath.extension() == ".obj")
            {
                continue;
            }

            UnityMaterialInfo material = ParseMaterialAsset(materialReference);
            ResolveAssetReference(material.Shader, guidToAssetPath);
            ResolveTextureBinding(material.BaseMap, guidToAssetPath);
            ResolveTextureBinding(material.MainTex, guidToAssetPath);
            ResolveTextureBinding(material.NormalMap, guidToAssetPath);
            ResolveTextureBinding(material.MetallicGlossMap, guidToAssetPath);
            ResolveTextureBinding(material.OcclusionMap, guidToAssetPath);
            ResolveTextureBinding(material.EmissionMap, guidToAssetPath);
            scene.Materials.push_back(std::move(material));
        }
    }

    return scene;
}

//Modify Begin:2026-07-30 by BestHui
void SceneYamlParser::WriteCameraToFile(
    const std::filesystem::path& scenePath,
    const UnityCameraWriteInfo& camera)
{
    if (camera.GameObjectId == 0 || camera.TransformFileId == 0)
    {
        throw std::runtime_error("Unity camera write info is invalid.");
    }

    std::ifstream input(scenePath);
    if (!input.is_open())
    {
        throw std::runtime_error("Failed to open Unity scene for camera write.");
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line))
    {
        lines.push_back(line);
    }

    bool transformFound = false;
    bool localPositionWritten = false;
    bool localRotationWritten = false;
    bool cameraComponentFound = false;
    bool fieldOfViewWritten = false;

    int currentClassId = 0;
    int64_t currentFileId = 0;
    bool currentCameraMatches = false;

    for (std::string& currentLine : lines)
    {
        int headerClassId = 0;
        int64_t headerFileId = 0;
        if (TryParseDocumentHeader(currentLine, headerClassId, headerFileId))
        {
            currentClassId = headerClassId;
            currentFileId = headerFileId;
            currentCameraMatches = false;
            continue;
        }

        const std::string trimmed = Trim(currentLine);
        if (currentClassId == 20 && StartsWith(trimmed, "m_GameObject:"))
        {
            const UnityAssetReference gameObject = ParseAssetReference(trimmed);
            currentCameraMatches = gameObject.FileId == camera.GameObjectId;
            cameraComponentFound = cameraComponentFound || currentCameraMatches;
            continue;
        }

        if (currentClassId == 4 && currentFileId == camera.TransformFileId)
        {
            transformFound = true;
            const std::string indent = ExtractLineIndent(currentLine);
            if (StartsWith(trimmed, "m_LocalPosition:"))
            {
                currentLine = indent + "m_LocalPosition: " + FormatUnityVector3(camera.LocalPosition);
                localPositionWritten = true;
            }
            else if (StartsWith(trimmed, "m_LocalRotation:"))
            {
                currentLine = indent + "m_LocalRotation: " + FormatUnityQuaternion(camera.LocalRotation);
                localRotationWritten = true;
            }
        }
        else if (currentClassId == 20 && currentCameraMatches &&
            (StartsWith(trimmed, "field of view:") || StartsWith(trimmed, "m_FieldOfView:")))
        {
            const std::string indent = ExtractLineIndent(currentLine);
            const char* propertyName = StartsWith(trimmed, "m_FieldOfView:") ? "m_FieldOfView: " : "field of view: ";
            currentLine = indent + propertyName + FormatFloat(camera.FieldOfView);
            fieldOfViewWritten = true;
        }
    }

    if (!transformFound || !localPositionWritten || !localRotationWritten)
    {
        throw std::runtime_error("Failed to find Unity camera transform fields to write.");
    }
    if (!cameraComponentFound || !fieldOfViewWritten)
    {
        throw std::runtime_error("Failed to find Unity camera component fields to write.");
    }

    std::ofstream output(scenePath, std::ios::trunc);
    if (!output.is_open())
    {
        throw std::runtime_error("Failed to write Unity scene camera data.");
    }

    for (const std::string& outputLine : lines)
    {
        output << outputLine << '\n';
    }
}
//Modify End
//Modify End

//Modify Begin:2026-07-30 by BestHui
#include <Framework/Scene/SceneImporter.h>

#include <DirectXMath.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

using namespace DirectX;

namespace
{
    constexpr const char* UnityBuiltinMeshGuid = "0000000000000000e000000000000000";
    constexpr int64_t UnityBuiltinCubeFileId = 10202;
    constexpr int64_t UnityBuiltinPlaneFileId = 10209;

    bool IsRenderableObject(const UnitySceneObject& object)
    {
        return object.Active && object.RendererEnabled && object.Mesh.IsValid();
    }

    bool IsUnityBuiltinMesh(const UnityAssetReference& mesh)
    {
        return mesh.Guid == UnityBuiltinMeshGuid;
    }

    std::string ImportTrim(std::string value)
    {
        const auto isNotSpace = [](const unsigned char character)
        {
            return !std::isspace(character);
        };
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), isNotSpace));
        value.erase(std::find_if(value.rbegin(), value.rend(), isNotSpace).base(), value.end());
        return value;
    }

    std::string ToLower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
        return value;
    }

    std::string MakeAssetReferenceKey(const UnityAssetReference& reference)
    {
        return reference.Guid + ":" + std::to_string(reference.FileId);
    }

    bool StartsWithText(const std::string& value, const char* prefix)
    {
        return value.rfind(prefix, 0) == 0;
    }

    std::filesystem::path ParseObjTexturePath(
        const std::filesystem::path& materialPath,
        std::string texturePath)
    {
        std::replace(texturePath.begin(), texturePath.end(), '\\', '/');
        return materialPath.parent_path() / ImportTrim(std::move(texturePath));
    }

//Modify Begin:2026-07-30 by BestHui
    std::filesystem::path ResolveObjNormalTexturePath(
        const std::filesystem::path& materialPath,
        std::string texturePath)
    {
        const std::filesystem::path requestedPath = ParseObjTexturePath(materialPath, std::move(texturePath));
        if (std::filesystem::exists(requestedPath))
        {
            return requestedPath;
        }

        const std::string stem = requestedPath.stem().string();
        const std::string lowerStem = ToLower(stem);
        constexpr std::string_view BumpSuffix = "_bump";
        if (!lowerStem.ends_with(BumpSuffix))
        {
            return requestedPath;
        }

        const std::string baseName = stem.substr(0u, stem.size() - BumpSuffix.size());
        for (const std::string_view suffix : { std::string_view("_normal"), std::string_view("_normals") })
        {
            std::filesystem::path candidate = requestedPath;
            candidate.replace_filename(baseName + std::string(suffix) + requestedPath.extension().string());
            if (std::filesystem::exists(candidate))
            {
                return candidate;
            }
        }

        return requestedPath;
    }
//Modify End

    struct ObjMaterialLibrary
    {
        std::unordered_map<std::string, std::string> GroupMaterialNames;
        std::unordered_map<std::string, SceneMaterial> Materials;
    };

    ObjMaterialLibrary LoadObjMaterialLibrary(const std::filesystem::path& meshPath)
    {
        ObjMaterialLibrary result;
        std::filesystem::path materialPath = meshPath;
        materialPath.replace_extension(".mtl");

        std::ifstream meshFile(meshPath);
        std::string currentGroup;
        std::string line;
        while (std::getline(meshFile, line))
        {
            const std::string trimmed = ImportTrim(line);
            if (StartsWithText(trimmed, "mtllib "))
            {
                materialPath = meshPath.parent_path() / ImportTrim(trimmed.substr(7));
            }
            else if (StartsWithText(trimmed, "g ") || StartsWithText(trimmed, "o "))
            {
                currentGroup = ImportTrim(trimmed.substr(2));
            }
            else if (!currentGroup.empty() && StartsWithText(trimmed, "usemtl "))
            {
                result.GroupMaterialNames.insert_or_assign(
                    ToLower(currentGroup),
                    ImportTrim(trimmed.substr(7)));
            }
        }

        std::ifstream materialFile(materialPath);
        SceneMaterial* material = nullptr;
        while (std::getline(materialFile, line))
        {
            const std::string trimmed = ImportTrim(line);
            if (StartsWithText(trimmed, "newmtl "))
            {
                SceneMaterial newMaterial;
                newMaterial.Name = ImportTrim(trimmed.substr(7));
                newMaterial.SourceId = meshPath.string() + "#" + newMaterial.Name;
                newMaterial.IsPbrMaterial = true;
                const std::string materialKey = ToLower(newMaterial.Name);
                result.Materials.insert_or_assign(materialKey, std::move(newMaterial));
                material = &result.Materials.at(materialKey);
                continue;
            }

            if (material == nullptr)
            {
                continue;
            }

            std::istringstream values(trimmed);
            std::string property;
            values >> property;
            if (property == "Kd" || property == "Ks")
            {
                DirectX::XMFLOAT4 color = property == "Kd" ? material->BaseColor : material->SpecColor;
                values >> color.x >> color.y >> color.z;
                if (property == "Kd")
                {
                    material->BaseColor = color;
                }
                else
                {
                    material->SpecColor = color;
                }
            }
            else if (property == "Ns")
            {
                float shininess = 0.0f;
                values >> shininess;
                material->Roughness = std::sqrt(2.0f / std::max(2.0f, shininess + 2.0f));
            }
            else if (property == "map_Kd")
            {
                std::string texturePath;
                std::getline(values, texturePath);
                material->BaseMap.AssetPath = ParseObjTexturePath(materialPath, texturePath);
            }
            else if (property == "map_bump" || property == "bump" || property == "norm")
            {
                std::string texturePath;
                std::getline(values, texturePath);
//Modify Begin:2026-07-30 by BestHui
                material->NormalMap.AssetPath = ResolveObjNormalTexturePath(materialPath, texturePath);
//Modify End
            }
        }

        return result;
    }

    std::optional<SceneMaterial> ResolveObjEmbeddedMaterial(
        const UnitySceneObject& object,
        const std::string& submeshName,
        std::unordered_map<std::string, ObjMaterialLibrary>& libraries)
    {
        if (object.Materials.empty() ||
            ToLower(object.Materials.front().AssetPath.extension().string()) != ".obj")
        {
            return std::nullopt;
        }

        const std::filesystem::path& meshPath = object.Materials.front().AssetPath;
        const std::string libraryKey = meshPath.string();
        auto libraryIterator = libraries.find(libraryKey);
        if (libraryIterator == libraries.end())
        {
            libraryIterator = libraries.emplace(libraryKey, LoadObjMaterialLibrary(meshPath)).first;
        }

        const auto groupMaterial = libraryIterator->second.GroupMaterialNames.find(ToLower(submeshName));
        if (groupMaterial == libraryIterator->second.GroupMaterialNames.end())
        {
            return std::nullopt;
        }

        const auto material = libraryIterator->second.Materials.find(ToLower(groupMaterial->second));
        return material != libraryIterator->second.Materials.end()
            ? std::optional<SceneMaterial>(material->second)
            : std::nullopt;
    }

    XMFLOAT3 ToFloat3(const UnityVector3& value)
    {
        return { value.X, value.Y, value.Z };
    }

    XMFLOAT4 ToFloat4(const UnityQuaternion& value)
    {
        return { value.X, value.Y, value.Z, value.W };
    }

    XMFLOAT4 ToFloat4(const UnityColor& value)
    {
        return { value.R, value.G, value.B, value.A };
    }

    UnityVector3 ToUnityVector3(const XMFLOAT3& value)
    {
        return { value.x, value.y, value.z };
    }

    UnityQuaternion ToUnityQuaternion(const XMFLOAT4& value)
    {
        return { value.x, value.y, value.z, value.w };
    }

    XMFLOAT3 RotateVector(const UnityQuaternion& rotation, const XMFLOAT3& value)
    {
        const XMVECTOR quaternion = XMVectorSet(rotation.X, rotation.Y, rotation.Z, rotation.W);
        const XMVECTOR vector = XMVectorSet(value.x, value.y, value.z, 0.0f);
        XMFLOAT3 result{};
        XMStoreFloat3(&result, XMVector3Rotate(vector, quaternion));
        return result;
    }

    XMFLOAT3 NormalizeVector(const XMFLOAT3& value)
    {
        const XMVECTOR vector = XMLoadFloat3(&value);
        if (XMVectorGetX(XMVector3LengthSq(vector)) <= 1.0e-8f)
        {
            return { 0.0f, 1.0f, 0.0f };
        }

        XMFLOAT3 result{};
        XMStoreFloat3(&result, XMVector3Normalize(vector));
        return result;
    }

    void BuildAreaLightAxes(const XMFLOAT3& normal, XMFLOAT3& axisU, XMFLOAT3& axisV)
    {
        const XMVECTOR normalVector = XMLoadFloat3(&normal);
        const XMVECTOR reference = std::abs(normal.y) < 0.99f ?
            XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f) :
            XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
        const XMVECTOR axisUVector = XMVector3Normalize(XMVector3Cross(reference, normalVector));
        const XMVECTOR axisVVector = XMVector3Normalize(XMVector3Cross(normalVector, axisUVector));
        XMStoreFloat3(&axisU, axisUVector);
        XMStoreFloat3(&axisV, axisVVector);
    }

    XMMATRIX BuildWorldMatrix(const UnityTransformInfo& transform)
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

    SceneTextureBinding ConvertTextureBinding(const UnityTextureBinding& binding)
    {
        SceneTextureBinding result;
        result.AssetPath = binding.Texture.AssetPath;
        result.ScaleOffset = { binding.Scale.X, binding.Scale.Y, binding.Offset.X, binding.Offset.Y };
        return result;
    }

    SceneMaterial ConvertMaterial(const UnityMaterialInfo& material)
    {
        SceneMaterial result;
        result.Name = material.Name;
        result.SourceId = material.Reference.Guid;
        result.BaseColor = ToFloat4(material.BaseColor);
        result.SpecColor = ToFloat4(material.SpecColor);
        result.EmissionColor = ToFloat4(material.EmissionColor);
        result.BaseMap = material.BaseMap.Texture.IsValid()
            ? ConvertTextureBinding(material.BaseMap)
            : ConvertTextureBinding(material.MainTex);
        result.NormalMap = ConvertTextureBinding(material.NormalMap);
        result.MetallicGlossMap = ConvertTextureBinding(material.MetallicGlossMap);
        result.OcclusionMap = ConvertTextureBinding(material.OcclusionMap);
        result.EmissionMap = ConvertTextureBinding(material.EmissionMap);
        result.Metallic = material.Metallic;
        result.Roughness = 1.0f - std::clamp(material.Smoothness, 0.0f, 1.0f);
        result.OcclusionStrength = material.OcclusionStrength;
        result.NormalScale = material.NormalScale;
        result.IsPbrMaterial = material.IsPbrMaterial;
        return result;
    }

    std::unordered_map<int64_t, std::string> LoadMeshFileIdNameMap(const std::filesystem::path& meshPath)
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
            const std::string trimmed = ImportTrim(line);
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
                result[std::stoll(match[1].str())] = ImportTrim(match[2].str());
                continue;
            }

            if (inInternalIdMap && std::regex_match(line, match, firstRegex))
            {
                pendingFileId = std::stoll(match[1].str());
                continue;
            }

            if (inInternalIdMap && pendingFileId.has_value() && std::regex_match(line, match, secondRegex))
            {
                result[*pendingFileId] = ImportTrim(match[1].str());
                pendingFileId.reset();
            }
        }

        return result;
    }

    float LoadModelImportScale(const std::filesystem::path& meshPath)
    {
        std::ifstream file(meshPath.wstring() + L".meta");
        if (!file.is_open())
        {
            return 1.0f;
        }

        bool inMeshesSection = false;
        std::string line;
        while (std::getline(file, line))
        {
            const std::string trimmed = ImportTrim(line);
            if (trimmed == "meshes:")
            {
                inMeshesSection = true;
                continue;
            }
            if (inMeshesSection && !line.empty() && line.front() != ' ')
            {
                break;
            }
            if (inMeshesSection && StartsWith(trimmed, "globalScale:"))
            {
                const float scale = ParseFloatAfterColon(trimmed, 1.0f);
                return std::isfinite(scale) && scale > 0.0f ? scale : 1.0f;
            }
        }

        return 1.0f;
    }

    void AddSceneMeshNameHints(
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

    std::string ResolveSubmeshName(
        const UnitySceneData& scene,
        const UnitySceneObject& object,
        std::unordered_map<std::string, std::unordered_map<int64_t, std::string>>& fileIdNameCache)
    {
        const std::string meshKey = object.Mesh.AssetPath.string();
        auto cacheIterator = fileIdNameCache.find(meshKey);
        if (cacheIterator == fileIdNameCache.end())
        {
            auto fileIdToName = LoadMeshFileIdNameMap(object.Mesh.AssetPath);
            AddSceneMeshNameHints(scene, object.Mesh.Guid, fileIdToName);
            cacheIterator = fileIdNameCache.emplace(meshKey, std::move(fileIdToName)).first;
        }

        const auto nameIterator = cacheIterator->second.find(object.Mesh.FileId);
        if (nameIterator != cacheIterator->second.end())
        {
            return nameIterator->second;
        }

        if (!object.Name.empty())
        {
            return object.Name;
        }

        throw std::runtime_error("Scene mesh reference does not have a submesh name mapping.");
    }

    const UnityCameraInfo* FindPrimaryCamera(const UnitySceneData& scene)
    {
        for (const UnityCameraInfo& camera : scene.Cameras)
        {
            if (camera.Enabled)
            {
                return &camera;
            }
        }

        if (!scene.Cameras.empty())
        {
            return &scene.Cameras.front();
        }

        return nullptr;
    }

    SceneCamera ConvertCamera(const UnityCameraInfo& camera)
    {
        SceneCamera result;
        result.Name = camera.Name;
        result.Orthographic = camera.Orthographic;
        result.FieldOfView = camera.FieldOfView;
        result.NearClipPlane = camera.NearClipPlane;
        result.FarClipPlane = camera.FarClipPlane;
        const XMFLOAT3 worldPosition = ToFloat3(camera.Transform.WorldPosition);
        result.RuntimeCamera = std::make_shared<Camera>();
        result.RuntimeCamera->SetTranslation(XMLoadFloat3(&worldPosition));
        const XMFLOAT4 rotation = ToFloat4(camera.Transform.WorldRotation);
        result.RuntimeCamera->SetRotation(XMLoadFloat4(&rotation));
        result.SourceBinding.ObjectId = camera.GameObjectId;
        result.SourceBinding.TransformId = camera.Transform.FileId;
        result.SourceBinding.ParentTransformId = camera.Transform.ParentTransformId;
        result.SourceBinding.LocalPosition = ToFloat3(camera.Transform.LocalPosition);
        result.SourceBinding.LocalRotation = ToFloat4(camera.Transform.LocalRotation);
        return result;
    }

    void ConvertLights(const UnitySceneData& unityScene, Scene& scene)
    {
        SceneSkybox skybox = scene.GetSkybox();
        skybox.AmbientColorAndIntensity = {
            unityScene.RenderSettings.AmbientSkyColor.R,
            unityScene.RenderSettings.AmbientSkyColor.G,
            unityScene.RenderSettings.AmbientSkyColor.B,
            unityScene.RenderSettings.AmbientIntensity
        };
        scene.SetSkybox(skybox);

        for (const UnityLightInfo& unityLight : unityScene.Lights)
        {
            if (!unityLight.Enabled)
            {
                continue;
            }

            if (unityLight.Type == UnityLightType::Directional)
            {
                DirectionalLight light{};
                const XMFLOAT3 direction = RotateVector(unityLight.Transform.WorldRotation, { 0.0f, 0.0f, -1.0f });
//Modify Begin:2026-07-30 by BestHui
                light.m_DirectionWs = { direction.x, direction.y, direction.z, std::max(0.0f, unityLight.AngularRadius) };
//Modify End
                light.m_Color = { unityLight.Color.R, unityLight.Color.G, unityLight.Color.B, unityLight.Intensity };
            scene.AddDirectionalLight(light);
            }
            else if (unityLight.Type == UnityLightType::Point || unityLight.Type == UnityLightType::Spot)
            {
                PointLight light(
                    {
                        unityLight.Transform.WorldPosition.X,
                        unityLight.Transform.WorldPosition.Y,
                        unityLight.Transform.WorldPosition.Z,
                        1.0f
                    },
                    std::max(0.1f, unityLight.Range));
                light.Color = { unityLight.Color.R, unityLight.Color.G, unityLight.Color.B, unityLight.Intensity };
//Modify Begin:2026-07-30 by BestHui
                light.SourceRadius = std::max(0.0f, unityLight.SourceRadius);
//Modify End
                light.RecalculateAttenuationCoefficients();
                scene.AddPointLight(light);
            }
            else if (unityLight.Type == UnityLightType::Area)
            {
                AreaLight light{};
                const XMFLOAT3 normal = NormalizeVector(RotateVector(unityLight.Transform.WorldRotation, { 0.0f, 0.0f, 1.0f }));
                const XMFLOAT3 axisU = NormalizeVector(RotateVector(unityLight.Transform.WorldRotation, { 1.0f, 0.0f, 0.0f }));
                const XMFLOAT3 axisV = NormalizeVector(RotateVector(unityLight.Transform.WorldRotation, { 0.0f, 1.0f, 0.0f }));
                light.PositionWs = {
                    unityLight.Transform.WorldPosition.X,
                    unityLight.Transform.WorldPosition.Y,
                    unityLight.Transform.WorldPosition.Z,
                    1.0f
                };
                light.NormalWs = { normal.x, normal.y, normal.z, 0.0f };
                light.AxisUWsAndExtent = { axisU.x, axisU.y, axisU.z, unityLight.AreaSize.X * 0.5f };
                light.AxisVWsAndExtent = { axisV.x, axisV.y, axisV.z, unityLight.AreaSize.Y * 0.5f };
                light.Color = { unityLight.Color.R, unityLight.Color.G, unityLight.Color.B, unityLight.Intensity };
                light.Range = unityLight.Range;
                scene.AddAreaLight(light);
            }
        }
    }

    void ConvertMaterials(const UnitySceneData& unityScene, Scene& scene, std::unordered_map<std::string, uint32_t>& materialBySourceId)
    {
        SceneMaterial defaultMaterial;
        defaultMaterial.Name = "Default PBR";
        defaultMaterial.SourceId = "__default__";
        defaultMaterial.BaseColor = { 0.85f, 0.85f, 0.85f, 1.0f };
        defaultMaterial.Roughness = 0.45f;
        defaultMaterial.IsPbrMaterial = true;
        scene.AddMaterial(defaultMaterial);
        materialBySourceId.emplace(defaultMaterial.SourceId, 0);

        for (const UnityMaterialInfo& unityMaterial : unityScene.Materials)
        {
            if (unityMaterial.Reference.Guid.empty())
            {
                continue;
            }

            SceneMaterial material = ConvertMaterial(unityMaterial);
            material.SourceId = MakeAssetReferenceKey(unityMaterial.Reference);
            if (!material.IsPbrMaterial)
            {
                continue;
            }

            const uint32_t materialIndex = static_cast<uint32_t>(scene.GetMaterials().size());
            materialBySourceId.insert_or_assign(material.SourceId, materialIndex);
            scene.AddMaterial(std::move(material));
        }
    }

    void ConvertObjects(
        const UnitySceneData& unityScene,
        Scene& scene,
        std::unordered_map<std::string, uint32_t>& materialBySourceId)
    {
        std::unordered_map<std::string, std::unordered_map<int64_t, std::string>> fileIdNameCache;
        std::unordered_map<std::string, ObjMaterialLibrary> objMaterialLibraries;
        std::unordered_map<std::string, float> modelImportScaleCache;

        for (const UnitySceneObject& unityObject : unityScene.Objects)
        {
            if (!IsRenderableObject(unityObject))
            {
                continue;
            }

//Modify Begin:2026-07-30 by BestHui
            const bool referencesUnsupportedMaterial = std::ranges::any_of(
                unityObject.Materials,
                [&materialBySourceId](const UnityAssetReference& materialReference)
                {
                    return !materialReference.AssetPath.empty() &&
                        ToLower(materialReference.AssetPath.extension().string()) == ".mat" &&
                        !materialBySourceId.contains(MakeAssetReferenceKey(materialReference));
                });
            if (referencesUnsupportedMaterial)
            {
                continue;
            }
//Modify End

            SceneObject object;
            object.Name = unityObject.Name;
            object.WorldMatrix = BuildWorldMatrix(unityObject.Transform);
            if (!unityObject.Materials.empty())
            {
                const auto material = materialBySourceId.find(MakeAssetReferenceKey(unityObject.Materials.front()));
                object.MaterialIndex = material != materialBySourceId.end() ? material->second : 0;
            }

            if (IsUnityBuiltinMesh(unityObject.Mesh))
            {
                if (unityObject.Mesh.FileId == UnityBuiltinPlaneFileId)
                {
                    object.Mesh.Kind = SceneMeshKind::BuiltinPlane;
                    scene.AddObject(std::move(object));
                }
                else if (unityObject.Mesh.FileId == UnityBuiltinCubeFileId)
                {
                    object.Mesh.Kind = SceneMeshKind::BuiltinCube;
                    scene.AddObject(std::move(object));
                }
                continue;
            }

            if (unityObject.Mesh.AssetPath.empty())
            {
                continue;
            }

            object.Mesh.Kind = SceneMeshKind::ExternalMesh;
            object.Mesh.AssetPath = unityObject.Mesh.AssetPath;
            object.Mesh.SubmeshName = ResolveSubmeshName(unityScene, unityObject, fileIdNameCache);
            const std::string meshPath = object.Mesh.AssetPath.string();
            const auto [scaleIterator, inserted] = modelImportScaleCache.try_emplace(meshPath, 1.0f);
            if (inserted)
            {
                scaleIterator->second = LoadModelImportScale(object.Mesh.AssetPath);
            }
            const float importScale = scaleIterator->second;
            object.WorldMatrix = XMMatrixScaling(importScale, importScale, importScale) * object.WorldMatrix;
            if (const std::optional<SceneMaterial> embeddedMaterial =
                ResolveObjEmbeddedMaterial(unityObject, object.Mesh.SubmeshName, objMaterialLibraries))
            {
                const auto material = materialBySourceId.find(embeddedMaterial->SourceId);
                if (material != materialBySourceId.end())
                {
                    object.MaterialIndex = material->second;
                }
                else
                {
                    object.MaterialIndex = scene.AddMaterial(*embeddedMaterial);
                    materialBySourceId.emplace(embeddedMaterial->SourceId, object.MaterialIndex);
                }
            }
            scene.AddObject(std::move(object));
        }
    }

    void ConvertSkybox(const UnitySceneData& unityScene, Scene& scene)
    {
        if (!unityScene.HasSkyboxMaterial)
        {
            return;
        }

        const UnityTextureBinding& skyboxTextureBinding = unityScene.SkyboxMaterial.MainTex.Texture.IsValid()
            ? unityScene.SkyboxMaterial.MainTex
            : unityScene.SkyboxMaterial.BaseMap;
        if (!skyboxTextureBinding.Texture.AssetPath.empty() && std::filesystem::exists(skyboxTextureBinding.Texture.AssetPath))
        {
            SceneSkybox skybox = scene.GetSkybox();
            skybox.Texture = ConvertTextureBinding(skyboxTextureBinding);
            scene.SetSkybox(skybox);
        }
    }

    Scene ConvertScene(const UnitySceneData& unityScene)
    {
        Scene scene;
        scene.SetSourcePaths(unityScene.ScenePath, unityScene.ProjectRoot, unityScene.AssetsRoot);

        const UnityCameraInfo* primaryCamera = FindPrimaryCamera(unityScene);
        if (primaryCamera != nullptr)
        {
            scene.SetCamera(ConvertCamera(*primaryCamera));
        }

        std::unordered_map<std::string, uint32_t> materialBySourceId;
        ConvertMaterials(unityScene, scene, materialBySourceId);
        ConvertObjects(unityScene, scene, materialBySourceId);
        ConvertLights(unityScene, scene);
        ConvertSkybox(unityScene, scene);
        return scene;
    }

    std::string MakeImportSummary(const UnitySceneData& unityScene, const Scene& scene)
    {
        std::ostringstream stream;
        stream << "Imported Unity scene: sourceObjects=" << unityScene.Objects.size()
               << ", sceneObjects=" << scene.GetObjects().size()
               << ", cameras=" << unityScene.Cameras.size()
               << ", directionalLights=" << scene.GetDirectionalLights().size()
               << ", pointLights=" << scene.GetPointLights().size()
               << ", areaLights=" << scene.GetAreaLights().size()
               << ", materials=" << scene.GetMaterials().size();
        return stream.str();
    }
}

SceneImportResult SceneImporter::ImportFromFile(
    const std::filesystem::path& scenePath,
    const SceneImportOptions& options)
{
//Modify Begin:2026-08-03 by BestHui
    if (scenePath.extension() == ".json")
    {
        return ImportJsonFromFile(scenePath, options);
    }
//Modify End
    if (!std::filesystem::exists(scenePath))
    {
        throw std::runtime_error("Unity scene file does not exist: " + scenePath.string());
    }

    SceneImportResult result;
    result.ScenePath = std::filesystem::absolute(scenePath);
    const UnitySceneData unityScene = SceneYamlParser::ParseFromFile(result.ScenePath, options.ParseOptions);
    const UnityCameraInfo* primaryCamera = FindPrimaryCamera(unityScene);
    if (options.RequireCamera && primaryCamera == nullptr)
    {
        throw std::runtime_error("Unity scene has no camera: " + result.ScenePath.string());
    }

    result.SceneData = ConvertScene(unityScene);
    if (options.RequireRenderableObject && result.SceneData.GetObjects().empty())
    {
        throw std::runtime_error("Unity scene has no supported renderable objects: " + result.ScenePath.string());
    }

    result.Diagnostics.push_back(MakeImportSummary(unityScene, result.SceneData));
    if (!result.SceneData.GetSkybox().Texture.IsValid())
    {
        result.Diagnostics.emplace_back("Scene has no external skybox texture; renderer may use its fallback skybox.");
    }
    if (result.SceneData.GetCamera().Orthographic)
    {
        result.Diagnostics.emplace_back("Primary camera is orthographic; the current path tracing sample uses perspective projection.");
    }

    return result;
}

void SceneImporter::WriteCameraToSourceFile(
    const std::filesystem::path& scenePath,
    const SceneCamera& camera)
{
    UnityCameraWriteInfo cameraWriteInfo;
    cameraWriteInfo.GameObjectId = camera.SourceBinding.ObjectId;
    cameraWriteInfo.TransformFileId = camera.SourceBinding.TransformId;
    cameraWriteInfo.LocalPosition = ToUnityVector3(camera.SourceBinding.LocalPosition);
    cameraWriteInfo.LocalRotation = ToUnityQuaternion(camera.SourceBinding.LocalRotation);
    cameraWriteInfo.FieldOfView = camera.FieldOfView;
    SceneYamlParser::WriteCameraToFile(scenePath, cameraWriteInfo);
}
//Modify End
