//Modify Begin:2026-08-26 by Hui
#include <Framework/Scene/SceneImporter.h>

#include <DirectXMath.h>

#include <Windows.h>
#include <shlwapi.h>
#include <wrl/client.h>
#include <xmllite.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace
{
    enum class TransformTarget
    {
        None,
        Sensor,
        Shape,
        SpotEmitter
    };

    struct MitsubaSensor
    {
        bool Found = false;
        bool HasToWorld = false;
        float FieldOfView = 45.0f;
        std::string FieldOfViewAxis = "x";
        uint32_t FilmWidth = 1280;
        uint32_t FilmHeight = 720;
        XMMATRIX ToWorld = XMMatrixIdentity();
    };

    struct MitsubaShape
    {
        std::string Type;
        std::string Name;
        std::filesystem::path Filename;
        std::string MaterialReferenceId;
        bool HasToWorld = false;
        XMMATRIX ToWorld = XMMatrixIdentity();
        bool IsAreaEmitter = false;
        XMFLOAT3 Radiance = { 0.0f, 0.0f, 0.0f };
    };

    struct MitsubaBsdf
    {
        std::string Id;
        std::string EffectiveType;
        XMFLOAT3 BaseColor = { 0.72f, 0.72f, 0.72f };
        XMFLOAT3 SpecColor = { 0.2f, 0.2f, 0.2f };
        std::filesystem::path BaseTextureReference;
        float Roughness = 0.45f;
        float Metallic = 0.0f;
        bool HasBaseTexture = false;
    };

    struct MitsubaTexture
    {
        std::string PropertyName;
        std::filesystem::path Filename;
        bool IsBitmap = false;
    };

    struct MitsubaSpotEmitter
    {
        bool HasToWorld = false;
        XMMATRIX ToWorld = XMMatrixIdentity();
        XMFLOAT3 Intensity = { 1.0f, 1.0f, 1.0f };
        float CutoffAngleDegrees = 20.0f;
        float BeamWidthDegrees = 15.0f;
        float Range = 600.0f;
    };

    struct ImportCounters
    {
        size_t TopLevelBsdfDefinitions = 0;
        size_t ImportedPbrMaterials = 0;
        size_t ImportedBaseColorTextures = 0;
        size_t UnresolvedMaterialReferences = 0;
        size_t MissingMeshFiles = 0;
        size_t MissingTextureFiles = 0;
        size_t UnsupportedShapes = 0;
    };

    std::string ToUtf8(const WCHAR* value, const UINT length)
    {
        if (value == nullptr || length == 0)
        {
            return {};
        }

        const int outputLength = WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            value,
            static_cast<int>(length),
            nullptr,
            0,
            nullptr,
            nullptr);
        if (outputLength <= 0)
        {
            throw std::runtime_error("Mitsuba XML contains an invalid UTF-16 name or value.");
        }

        std::string result(static_cast<size_t>(outputLength), '\0');
        if (WideCharToMultiByte(
                CP_UTF8,
                WC_ERR_INVALID_CHARS,
                value,
                static_cast<int>(length),
                result.data(),
                outputLength,
                nullptr,
                nullptr) != outputLength)
        {
            throw std::runtime_error("Failed to convert a Mitsuba XML name or value to UTF-8.");
        }
        return result;
    }

    std::string ToLower(std::string value)
    {
        std::ranges::transform(value, value.begin(), [](const unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
        return value;
    }

    std::string FormatHresult(const HRESULT result)
    {
        std::ostringstream stream;
        stream << "0x" << std::hex << static_cast<uint32_t>(result);
        return stream.str();
    }

    std::unordered_map<std::string, std::string> ReadAttributes(IXmlReader& reader)
    {
        std::unordered_map<std::string, std::string> attributes;
        HRESULT result = reader.MoveToFirstAttribute();
        if (result == S_FALSE)
        {
            return attributes;
        }
        if (FAILED(result))
        {
            throw std::runtime_error("Failed to read Mitsuba XML attributes: " + FormatHresult(result));
        }

        while (result == S_OK)
        {
            const WCHAR* name = nullptr;
            UINT nameLength = 0;
            const WCHAR* value = nullptr;
            UINT valueLength = 0;
            if (FAILED(reader.GetLocalName(&name, &nameLength)) || FAILED(reader.GetValue(&value, &valueLength)))
            {
                throw std::runtime_error("Failed to retrieve a Mitsuba XML attribute.");
            }
            attributes.emplace(ToUtf8(name, nameLength), ToUtf8(value, valueLength));
            result = reader.MoveToNextAttribute();
        }
        if (result != S_FALSE)
        {
            throw std::runtime_error("Failed to iterate Mitsuba XML attributes: " + FormatHresult(result));
        }
        if (FAILED(reader.MoveToElement()))
        {
            throw std::runtime_error("Failed to restore the Mitsuba XML element after reading attributes.");
        }
        return attributes;
    }

    std::string GetAttribute(
        const std::unordered_map<std::string, std::string>& attributes,
        const std::string_view key)
    {
        const auto iterator = attributes.find(std::string(key));
        return iterator != attributes.end() ? iterator->second : std::string();
    }

    std::string ResolveMitsubaVariables(
        std::string value,
        const std::unordered_map<std::string, std::string>& defaults)
    {
        constexpr size_t MaximumExpansionPasses = 32;
        for (size_t expansionPass = 0; expansionPass < MaximumExpansionPasses; ++expansionPass)
        {
            bool substituted = false;
            for (size_t cursor = 0; cursor < value.size();)
            {
                const size_t variableStart = value.find('$', cursor);
                if (variableStart == std::string::npos)
                {
                    break;
                }

                size_t variableEnd = variableStart + 1;
                while (variableEnd < value.size())
                {
                    const unsigned char character = static_cast<unsigned char>(value[variableEnd]);
                    if (!std::isalnum(character) && character != '_')
                    {
                        break;
                    }
                    ++variableEnd;
                }
                if (variableEnd == variableStart + 1)
                {
                    throw std::runtime_error("Mitsuba default variable has no name.");
                }

                const std::string variableName = value.substr(variableStart + 1, variableEnd - variableStart - 1);
                const auto defaultValue = defaults.find(variableName);
                if (defaultValue == defaults.end())
                {
                    throw std::runtime_error("Mitsuba default variable is not defined before use: $" + variableName + ".");
                }
                value.replace(variableStart, variableEnd - variableStart, defaultValue->second);
                cursor = variableStart + defaultValue->second.size();
                substituted = true;
            }

            if (!substituted)
            {
                return value;
            }
        }
        throw std::runtime_error("Mitsuba default variables contain a cyclic expansion.");
    }

    float ParseFloat(const std::string& value, const std::string_view context)
    {
        std::istringstream stream(value);
        float result = 0.0f;
        stream >> result;
        if (!stream || !std::isfinite(result))
        {
            throw std::runtime_error("Invalid floating-point value for Mitsuba " + std::string(context) + ": '" + value + "'.");
        }
        return result;
    }

    uint32_t ParsePositiveInteger(const std::string& value, const std::string_view context)
    {
        std::istringstream stream(value);
        uint64_t result = 0;
        stream >> result;
        if (!stream || result == 0 || result > (std::numeric_limits<uint32_t>::max)())
        {
            throw std::runtime_error("Invalid positive integer for Mitsuba " + std::string(context) + ": '" + value + "'.");
        }
        return static_cast<uint32_t>(result);
    }

    std::array<float, 16> ParseMatrix(const std::string& value)
    {
        std::string normalized = value;
        std::ranges::replace(normalized, ',', ' ');
        std::istringstream stream(normalized);
        std::array<float, 16> result{};
        for (float& component : result)
        {
            stream >> component;
            if (!stream || !std::isfinite(component))
            {
                throw std::runtime_error("Mitsuba matrix must contain exactly 16 finite floating-point values.");
            }
        }

        std::string extraValue;
        if (stream >> extraValue)
        {
            throw std::runtime_error("Mitsuba matrix contains more than 16 values.");
        }
        return result;
    }

    XMMATRIX ConvertMitsubaToDirectX(const std::array<float, 16>& matrixValues)
    {
        const XMMATRIX mitsubaColumnVectorMatrix(
            matrixValues[0], matrixValues[1], matrixValues[2], matrixValues[3],
            matrixValues[4], matrixValues[5], matrixValues[6], matrixValues[7],
            matrixValues[8], matrixValues[9], matrixValues[10], matrixValues[11],
            matrixValues[12], matrixValues[13], matrixValues[14], matrixValues[15]);

        const XMMATRIX handedness = XMMatrixScaling(1.0f, 1.0f, -1.0f);
        const XMMATRIX leftHandedColumnVectorMatrix =
            XMMatrixMultiply(XMMatrixMultiply(handedness, mitsubaColumnVectorMatrix), handedness);
        return XMMatrixTranspose(leftHandedColumnVectorMatrix);
    }

    XMFLOAT3 ParseColor3(const std::string& value, const std::string_view context)
    {
        std::string normalized = value;
        std::ranges::replace(normalized, ',', ' ');
        std::istringstream stream(normalized);
        XMFLOAT3 color{};
        stream >> color.x >> color.y >> color.z;
        if (!stream || !std::isfinite(color.x) || !std::isfinite(color.y) || !std::isfinite(color.z))
        {
            throw std::runtime_error("Invalid RGB value for Mitsuba " + std::string(context) + ": '" + value + "'.");
        }
        return color;
    }

    bool IsWithinDirectory(
        const std::filesystem::path& candidate,
        const std::filesystem::path& directory)
    {
        std::error_code errorCode;
        const std::filesystem::path relativePath = std::filesystem::relative(candidate, directory, errorCode);
        if (errorCode || relativePath.empty())
        {
            return false;
        }

        const std::filesystem::path parentMarker("..");
        return relativePath != parentMarker && !relativePath.string().starts_with("..\\") && !relativePath.string().starts_with("../");
    }

    std::optional<std::filesystem::path> ResolveSceneAssetPath(
        const std::filesystem::path& sceneDirectory,
        const std::filesystem::path& reference,
        const std::string_view assetKind,
        std::vector<std::string>& diagnostics)
    {
        if (reference.empty() || reference.is_absolute())
        {
            diagnostics.push_back(
                "Mitsuba " + std::string(assetKind) + " has an empty or absolute filename and was skipped.");
            return std::nullopt;
        }

        std::error_code errorCode;
        const std::filesystem::path canonicalDirectory = std::filesystem::weakly_canonical(sceneDirectory, errorCode);
        if (errorCode)
        {
            throw std::runtime_error("Failed to resolve Mitsuba scene directory: " + sceneDirectory.string());
        }

        const std::filesystem::path canonicalCandidate =
            std::filesystem::weakly_canonical(canonicalDirectory / reference, errorCode);
        if (errorCode || !IsWithinDirectory(canonicalCandidate, canonicalDirectory))
        {
            diagnostics.push_back(
                "Mitsuba " + std::string(assetKind) + " filename escapes the scene directory and was skipped: " +
                reference.string());
            return std::nullopt;
        }
        if (!std::filesystem::is_regular_file(canonicalCandidate, errorCode) || errorCode)
        {
            diagnostics.push_back(
                "Mitsuba " + std::string(assetKind) + " file does not exist and was skipped: " + reference.string());
            return std::nullopt;
        }
        return canonicalCandidate;
    }

    bool IsMitsubaColorProperty(const std::string_view propertyName)
    {
        const std::string property = ToLower(std::string(propertyName));
        return property == "reflectance" ||
            property == "diffuse_reflectance" ||
            property == "base_color" ||
            property == "basecolor";
    }

    bool IsMitsubaMetalBsdf(const std::string_view type)
    {
        const std::string normalizedType = ToLower(std::string(type));
        return normalizedType == "conductor" || normalizedType == "roughconductor";
    }

    bool IsMitsubaDielectricBsdf(const std::string_view type)
    {
        const std::string normalizedType = ToLower(std::string(type));
        return normalizedType == "dielectric" || normalizedType == "thindielectric";
    }

    void ApplyMitsubaBsdfType(MitsubaBsdf& material, const std::string_view sourceType)
    {
        const std::string type = ToLower(std::string(sourceType));
        if (type.empty() || type == "twosided" || type == "mask" || type == "bumpmap")
        {
            return;
        }

        material.EffectiveType = type;
        if (IsMitsubaMetalBsdf(type))
        {
            material.Metallic = 1.0f;
            material.SpecColor = { 1.0f, 1.0f, 1.0f };
        }
        else if (IsMitsubaDielectricBsdf(type))
        {
            material.Metallic = 0.0f;
            material.BaseColor = { 0.95f, 0.95f, 0.95f };
            material.SpecColor = { 0.95f, 0.95f, 0.95f };
            material.Roughness = 0.08f;
        }
        else if (type == "diffuse")
        {
            material.Metallic = 0.0f;
            material.Roughness = 0.85f;
        }
        else if (type == "plastic" || type == "roughplastic")
        {
            material.Metallic = 0.0f;
            material.Roughness = type == "plastic" ? 0.35f : 0.45f;
        }
    }

    void ApplyMitsubaBsdfColor(
        MitsubaBsdf& material,
        const std::string_view propertyName,
        const XMFLOAT3& color)
    {
        const std::string property = ToLower(std::string(propertyName));
        if (IsMitsubaColorProperty(property))
        {
            material.BaseColor = color;
        }
        else if (property == "specular_reflectance")
        {
            if (IsMitsubaMetalBsdf(material.EffectiveType))
            {
                material.BaseColor = color;
            }
            else
            {
                material.SpecColor = color;
            }
        }
    }

    uint32_t AddMitsubaPbrMaterial(
        const MitsubaBsdf& source,
        const std::filesystem::path& sceneDirectory,
        Scene& scene,
        std::vector<std::string>& diagnostics,
        ImportCounters& counters)
    {
        SceneMaterial material;
        material.Name = source.Id.empty() ? "Mitsuba XML PBR" : source.Id;
        material.SourceId = source.Id;
        material.BaseColor = { source.BaseColor.x, source.BaseColor.y, source.BaseColor.z, 1.0f };
        material.SpecColor = { source.SpecColor.x, source.SpecColor.y, source.SpecColor.z, 1.0f };
        material.Metallic = std::clamp(source.Metallic, 0.0f, 1.0f);
        material.Roughness = std::clamp(source.Roughness, 0.02f, 1.0f);
        material.IsPbrMaterial = true;

        if (source.HasBaseTexture)
        {
            const std::optional<std::filesystem::path> texturePath =
                ResolveSceneAssetPath(sceneDirectory, source.BaseTextureReference, "bitmap texture", diagnostics);
            if (texturePath.has_value())
            {
                material.BaseMap.AssetPath = *texturePath;
                ++counters.ImportedBaseColorTextures;
            }
            else
            {
                ++counters.MissingTextureFiles;
            }
        }

        ++counters.ImportedPbrMaterials;
        return scene.AddMaterial(std::move(material));
    }

    void AddDefaultPbrMaterial(Scene& scene)
    {
        SceneMaterial material;
        material.Name = "Mitsuba XML Default PBR";
        material.SourceId = "__mitsuba_default_pbr__";
        material.BaseColor = { 0.72f, 0.72f, 0.72f, 1.0f };
        material.SpecColor = { 0.2f, 0.2f, 0.2f, 1.0f };
        material.Metallic = 0.0f;
        material.Roughness = 0.45f;
        material.IsPbrMaterial = true;
        scene.AddMaterial(std::move(material));
    }

    void AddObjShape(
        const MitsubaShape& source,
        const std::filesystem::path& sceneDirectory,
        Scene& scene,
        std::vector<std::string>& diagnostics,
        ImportCounters& counters,
        const std::unordered_map<std::string, uint32_t>& materialIndices)
    {
        const std::optional<std::filesystem::path> meshPath =
            ResolveSceneAssetPath(sceneDirectory, source.Filename, "OBJ shape", diagnostics);
        if (!meshPath.has_value())
        {
            ++counters.MissingMeshFiles;
            return;
        }

        SceneNode node;
        node.Name = source.Name.empty() ? "Mitsuba OBJ" : source.Name;
        node.SourceId = node.Name;
        node.LocalMatrix = source.ToWorld;
        node.WorldMatrix = source.ToWorld;
        const uint32_t nodeIndex = scene.AddNode(std::move(node));

        SceneObject object;
        object.Name = source.Name.empty() ? meshPath->stem().string() : source.Name;
        object.WorldMatrix = source.ToWorld;
        object.Mesh.Kind = SceneMeshKind::ExternalMesh;
        object.Mesh.AssetPath = *meshPath;
        if (!source.MaterialReferenceId.empty())
        {
            const auto materialIterator = materialIndices.find(source.MaterialReferenceId);
            if (materialIterator != materialIndices.end())
            {
                object.MaterialIndex = materialIterator->second;
            }
            else
            {
                ++counters.UnresolvedMaterialReferences;
                diagnostics.push_back(
                    "Mitsuba OBJ shape uses an undefined BSDF and fell back to default PBR: " +
                    source.MaterialReferenceId + ".");
            }
        }
        object.NodeIndex = nodeIndex;
        scene.AddObject(std::move(object));
    }

    void AddAreaEmitter(
        const MitsubaShape& source,
        Scene& scene,
        std::vector<std::string>& diagnostics)
    {
        const XMVECTOR localOrigin = XMVectorZero();
        const XMVECTOR localAxisU = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
        const XMVECTOR localAxisV = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
        const XMVECTOR transformedAxisU = XMVector3TransformNormal(localAxisU, source.ToWorld);
        const XMVECTOR transformedAxisV = XMVector3TransformNormal(localAxisV, source.ToWorld);
        const float extentU = XMVectorGetX(XMVector3Length(transformedAxisU));
        const float extentV = XMVectorGetX(XMVector3Length(transformedAxisV));
        if (extentU <= 1.0e-5f || extentV <= 1.0e-5f)
        {
            diagnostics.push_back("Mitsuba rectangle area emitter has a degenerate transform and was skipped.");
            return;
        }

        XMFLOAT3 position{};
        XMFLOAT3 axisU{};
        XMFLOAT3 axisV{};
        XMFLOAT3 normal{};
        // The Mitsuba-to-DirectX conversion contains a handedness reflection. A reflection
        // reverses the winding of the transformed tangent basis, so flip one axis to preserve
        // the source emitter's authored front side for one-sided direct-light evaluation.
        const XMVECTOR normalizedAxisU = XMVectorScale(transformedAxisU, 1.0f / extentU);
        const XMVECTOR normalizedAxisV = XMVectorScale(transformedAxisV, -1.0f / extentV);
        XMStoreFloat3(&position, XMVector3TransformCoord(localOrigin, source.ToWorld));
        XMStoreFloat3(&axisU, normalizedAxisU);
        XMStoreFloat3(&axisV, normalizedAxisV);
        XMStoreFloat3(&normal, XMVector3Normalize(XMVector3Cross(normalizedAxisU, normalizedAxisV)));

        const float intensity = (std::max)({ source.Radiance.x, source.Radiance.y, source.Radiance.z, 0.0f });
        if (intensity <= 1.0e-5f)
        {
            diagnostics.push_back("Mitsuba rectangle area emitter has zero radiance and was skipped.");
            return;
        }

        AreaLight light;
        light.PositionWs = { position.x, position.y, position.z, 1.0f };
        light.NormalWs = { normal.x, normal.y, normal.z, 0.0f };
        light.AxisUWsAndExtent = { axisU.x, axisU.y, axisU.z, extentU };
        light.AxisVWsAndExtent = { axisV.x, axisV.y, axisV.z, extentV };
        light.Color = {
            source.Radiance.x / intensity,
            source.Radiance.y / intensity,
            source.Radiance.z / intensity,
            intensity
        };
        light.Range = 50.0f;
        scene.AddAreaLight(light);
    }

    void AddSpotEmitter(
        const MitsubaSpotEmitter& source,
        Scene& scene,
        std::vector<std::string>& diagnostics)
    {
        const float intensity = (std::max)({ source.Intensity.x, source.Intensity.y, source.Intensity.z, 0.0f });
        if (intensity <= 1.0e-5f)
        {
            diagnostics.push_back("Mitsuba spot emitter has zero intensity and was skipped.");
            return;
        }

        const XMVECTOR position = XMVector3TransformCoord(XMVectorZero(), source.ToWorld);
        const XMVECTOR direction = XMVector3TransformNormal(
            XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f),
            source.ToWorld);
        if (XMVectorGetX(XMVector3LengthSq(direction)) <= 1.0e-8f)
        {
            diagnostics.push_back("Mitsuba spot emitter has a degenerate to_world transform and was skipped.");
            return;
        }

        XMFLOAT3 positionValue{};
        XMFLOAT3 directionValue{};
        XMStoreFloat3(&positionValue, position);
        XMStoreFloat3(&directionValue, XMVector3Normalize(direction));

        const float outerAngle = XMConvertToRadians(std::clamp(source.CutoffAngleDegrees * 0.5f, 0.1f, 89.9f));
        const float innerAngle = XMConvertToRadians(std::clamp(
            source.BeamWidthDegrees * 0.5f,
            0.0f,
            XMConvertToDegrees(outerAngle)));
        const PointLight attenuationSource(
            { positionValue.x, positionValue.y, positionValue.z, 1.0f },
            std::clamp(source.Range, 0.1f, 600.0f));

        SpotLight light;
        light.PositionWs = attenuationSource.PositionWs;
        light.DirectionWs = { directionValue.x, directionValue.y, directionValue.z, 0.0f };
        light.Color = {
            source.Intensity.x / intensity,
            source.Intensity.y / intensity,
            source.Intensity.z / intensity,
            1.0f
        };
        light.Intensity = intensity;
        light.InnerConeAngle = innerAngle;
        light.OuterConeAngle = outerAngle;
        light.Range = attenuationSource.Range;
        light.ConstantAttenuation = attenuationSource.ConstantAttenuation;
        light.LinearAttenuation = attenuationSource.LinearAttenuation;
        light.QuadraticAttenuation = attenuationSource.QuadraticAttenuation;
        scene.AddSpotLight(light);
    }

    float ConvertToVerticalFieldOfView(const MitsubaSensor& sensor)
    {
        const float horizontalToVerticalAspect =
            static_cast<float>(sensor.FilmWidth) / static_cast<float>(sensor.FilmHeight);
        const float fovRadians = XMConvertToRadians(std::clamp(sensor.FieldOfView, 1.0f, 179.0f));
        const std::string axis = ToLower(sensor.FieldOfViewAxis);

        if (axis == "y" || axis.empty())
        {
            return XMConvertToDegrees(fovRadians);
        }
        if (axis == "x")
        {
            return XMConvertToDegrees(2.0f * std::atan(std::tan(fovRadians * 0.5f) / horizontalToVerticalAspect));
        }
        if (axis == "diagonal")
        {
            const float diagonal = std::sqrt(horizontalToVerticalAspect * horizontalToVerticalAspect + 1.0f);
            return XMConvertToDegrees(2.0f * std::atan(std::tan(fovRadians * 0.5f) / diagonal));
        }
        if (axis == "smaller")
        {
            return horizontalToVerticalAspect >= 1.0f ?
                XMConvertToDegrees(fovRadians) :
                XMConvertToDegrees(2.0f * std::atan(std::tan(fovRadians * 0.5f) / horizontalToVerticalAspect));
        }
        if (axis == "larger")
        {
            return horizontalToVerticalAspect >= 1.0f ?
                XMConvertToDegrees(2.0f * std::atan(std::tan(fovRadians * 0.5f) / horizontalToVerticalAspect)) :
                XMConvertToDegrees(fovRadians);
        }
        return XMConvertToDegrees(fovRadians);
    }

    void AddSensorCamera(const MitsubaSensor& source, Scene& scene, std::vector<std::string>& diagnostics)
    {
        if (!source.Found)
        {
            return;
        }
        if (!source.HasToWorld)
        {
            diagnostics.push_back("Mitsuba sensor has no to_world transform; using identity camera transform.");
        }

        const XMVECTOR eye = XMVector3TransformCoord(XMVectorZero(), source.ToWorld);
        // The handedness reflection maps Mitsuba's local +Z sensor axis to the renderer's local -Z axis.
        const XMVECTOR direction = XMVector3Normalize(
            XMVector3TransformNormal(XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f), source.ToWorld));
        const XMVECTOR up = XMVector3Normalize(
            XMVector3TransformNormal(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), source.ToWorld));
        if (XMVectorGetX(XMVector3LengthSq(direction)) <= 1.0e-8f ||
            XMVectorGetX(XMVector3LengthSq(up)) <= 1.0e-8f)
        {
            throw std::runtime_error("Mitsuba sensor has a degenerate to_world transform.");
        }

        SceneCamera camera;
        camera.Name = "Mitsuba Camera";
        camera.RuntimeCamera = std::make_shared<Camera>();
        camera.RuntimeCamera->SetLookAt(eye, XMVectorAdd(eye, direction), up);
        camera.FieldOfView = std::clamp(ConvertToVerticalFieldOfView(source), 1.0f, 179.0f);
        camera.NearClipPlane = 0.01f;
        camera.FarClipPlane = 1000.0f;
        scene.SetCamera(camera);
    }
}

SceneImportResult SceneImporter::ImportMitsubaXmlFromFile(
    const std::filesystem::path& scenePath,
    const SceneImportOptions& options)
{
    if (!std::filesystem::exists(scenePath))
    {
        throw std::runtime_error("Mitsuba XML scene file does not exist: " + scenePath.string());
    }
    if (ToLower(scenePath.extension().string()) != ".xml")
    {
        throw std::invalid_argument("ImportMitsubaXmlFromFile requires an .xml file.");
    }

    SceneImportResult result;
    result.ScenePath = std::filesystem::weakly_canonical(std::filesystem::absolute(scenePath));
    const std::filesystem::path sceneDirectory = result.ScenePath.parent_path();
    result.SceneData.SetSourcePaths(result.ScenePath, sceneDirectory, sceneDirectory);
    AddDefaultPbrMaterial(result.SceneData);

    ComPtr<IStream> inputStream;
    const std::wstring sourcePath = result.ScenePath.wstring();
    HRESULT xmlResult = SHCreateStreamOnFileW(
        sourcePath.c_str(),
        STGM_READ | STGM_SHARE_DENY_NONE,
        inputStream.GetAddressOf());
    if (FAILED(xmlResult))
    {
        throw std::runtime_error("Failed to open Mitsuba XML scene: " + result.ScenePath.string() + " (" + FormatHresult(xmlResult) + ").");
    }

    ComPtr<IXmlReader> reader;
    xmlResult = CreateXmlReader(IID_PPV_ARGS(reader.GetAddressOf()), nullptr);
    if (FAILED(xmlResult) || FAILED(reader->SetInput(inputStream.Get())))
    {
        throw std::runtime_error("Failed to initialize the Mitsuba XML reader.");
    }

    MitsubaSensor sensor;
    MitsubaShape activeShape;
    MitsubaSpotEmitter activeSpotEmitter;
    MitsubaBsdf activeBsdf;
    MitsubaTexture activeTexture;
    bool hasActiveShape = false;
    bool hasActiveSensor = false;
    bool hasActiveSpotEmitter = false;
    bool hasActiveBsdf = false;
    bool hasActiveTexture = false;
    uint32_t activeBsdfDepth = 0;
    std::vector<std::string> elementStack;
    std::vector<TransformTarget> transformTargets;
    std::vector<MitsubaShape> pendingObjShapes;
    std::unordered_map<std::string, uint32_t> materialIndices;
    std::unordered_map<std::string, std::string> defaultValues;
    ImportCounters counters;

    const auto finalizeElement = [&](const std::string& elementName)
    {
        if (elementName == "texture" && hasActiveTexture)
        {
            if (activeTexture.IsBitmap &&
                IsMitsubaColorProperty(activeTexture.PropertyName) &&
                !activeTexture.Filename.empty())
            {
                activeBsdf.BaseTextureReference = activeTexture.Filename;
                activeBsdf.HasBaseTexture = true;
            }
            hasActiveTexture = false;
            return;
        }

        if (elementName == "bsdf" && hasActiveBsdf)
        {
            if (activeBsdfDepth == 0)
            {
                throw std::runtime_error("Mitsuba BSDF nesting underflow.");
            }
            --activeBsdfDepth;
            if (activeBsdfDepth == 0)
            {
                const uint32_t materialIndex = AddMitsubaPbrMaterial(
                    activeBsdf,
                    sceneDirectory,
                    result.SceneData,
                    result.Diagnostics,
                    counters);
                if (!activeBsdf.Id.empty())
                {
                    if (materialIndices.contains(activeBsdf.Id))
                    {
                        throw std::runtime_error("Mitsuba XML defines the same BSDF id more than once: " + activeBsdf.Id + ".");
                    }
                    materialIndices.emplace(activeBsdf.Id, materialIndex);
                }
                hasActiveBsdf = false;
            }
            return;
        }

        if (elementName == "transform")
        {
            if (!transformTargets.empty())
            {
                transformTargets.pop_back();
            }
            return;
        }

        if (elementName == "shape" && hasActiveShape)
        {
            if (activeShape.Type == "obj")
            {
                pendingObjShapes.push_back(activeShape);
            }
            else if (activeShape.Type == "rectangle" && activeShape.IsAreaEmitter)
            {
                AddAreaEmitter(activeShape, result.SceneData, result.Diagnostics);
            }
            else
            {
                ++counters.UnsupportedShapes;
                result.Diagnostics.push_back("Unsupported Mitsuba shape type was skipped: " + activeShape.Type + ".");
            }
            hasActiveShape = false;
            return;
        }

        if (elementName == "emitter" && hasActiveSpotEmitter)
        {
            AddSpotEmitter(activeSpotEmitter, result.SceneData, result.Diagnostics);
            hasActiveSpotEmitter = false;
            return;
        }

        if (elementName == "sensor")
        {
            hasActiveSensor = false;
        }
    };

    while (true)
    {
        XmlNodeType nodeType = XmlNodeType_None;
        xmlResult = reader->Read(&nodeType);
        if (xmlResult == S_FALSE)
        {
            break;
        }
        if (FAILED(xmlResult))
        {
            throw std::runtime_error("Failed while parsing Mitsuba XML scene: " + FormatHresult(xmlResult));
        }

        if (nodeType == XmlNodeType_EndElement)
        {
            const WCHAR* localName = nullptr;
            UINT localNameLength = 0;
            if (FAILED(reader->GetLocalName(&localName, &localNameLength)))
            {
                throw std::runtime_error("Failed to retrieve a Mitsuba XML closing element.");
            }
            const std::string elementName = ToUtf8(localName, localNameLength);
            finalizeElement(elementName);
            if (!elementStack.empty())
            {
                elementStack.pop_back();
            }
            continue;
        }
        if (nodeType != XmlNodeType_Element)
        {
            continue;
        }

        const WCHAR* localName = nullptr;
        UINT localNameLength = 0;
        if (FAILED(reader->GetLocalName(&localName, &localNameLength)))
        {
            throw std::runtime_error("Failed to retrieve a Mitsuba XML element.");
        }
        const std::string elementName = ToUtf8(localName, localNameLength);
        const std::string parentName = elementStack.empty() ? std::string() : elementStack.back();
        const std::unordered_map<std::string, std::string> attributes = ReadAttributes(*reader.Get());
        const bool isEmptyElement = reader->IsEmptyElement();
        const std::string name = GetAttribute(attributes, "name");
        const std::string rawValue = GetAttribute(attributes, "value");
        std::string value;
        if (elementName == "default" && parentName == "scene")
        {
            if (name.empty())
            {
                throw std::runtime_error("Mitsuba default definition has no name.");
            }
            defaultValues[name] = ResolveMitsubaVariables(rawValue, defaultValues);
            value = defaultValues[name];
        }
        else
        {
            value = ResolveMitsubaVariables(rawValue, defaultValues);
        }

        if (elementName == "sensor")
        {
            if (sensor.Found)
            {
                result.Diagnostics.push_back("Mitsuba XML contains multiple sensors; the last sensor is selected.");
            }
            sensor = {};
            sensor.Found = true;
            hasActiveSensor = true;
        }
        else if (elementName == "shape")
        {
            activeShape = {};
            activeShape.Type = ToLower(GetAttribute(attributes, "type"));
            activeShape.Name = GetAttribute(attributes, "id");
            hasActiveShape = true;
        }
        else if (elementName == "bsdf" && parentName == "scene")
        {
            activeBsdf = {};
            activeBsdf.Id = GetAttribute(attributes, "id");
            ApplyMitsubaBsdfType(activeBsdf, GetAttribute(attributes, "type"));
            hasActiveBsdf = true;
            activeBsdfDepth = 1;
            ++counters.TopLevelBsdfDefinitions;
        }
        else if (elementName == "bsdf" && hasActiveBsdf)
        {
            ++activeBsdfDepth;
            const std::string nestedId = GetAttribute(attributes, "id");
            if (activeBsdf.Id.empty() && !nestedId.empty())
            {
                // Bump or mask wrappers can own the top-level scope while the referenced BSDF id lives below them.
                activeBsdf.Id = nestedId;
            }
            ApplyMitsubaBsdfType(activeBsdf, GetAttribute(attributes, "type"));
        }
        else if (elementName == "texture" && hasActiveBsdf)
        {
            activeTexture = {};
            activeTexture.PropertyName = ToLower(name);
            activeTexture.IsBitmap = ToLower(GetAttribute(attributes, "type")) == "bitmap";
            hasActiveTexture = true;
        }
        else if (elementName == "emitter" && parentName == "scene" && ToLower(GetAttribute(attributes, "type")) == "spot")
        {
            activeSpotEmitter = {};
            hasActiveSpotEmitter = true;
        }
        else if (elementName == "transform" && (ToLower(name) == "to_world" || ToLower(name) == "toworld"))
        {
            TransformTarget target = TransformTarget::None;
            if (parentName == "sensor" && hasActiveSensor)
            {
                target = TransformTarget::Sensor;
            }
            else if (parentName == "shape" && hasActiveShape)
            {
                target = TransformTarget::Shape;
            }
            else if (parentName == "emitter" && hasActiveSpotEmitter)
            {
                target = TransformTarget::SpotEmitter;
            }
            transformTargets.push_back(target);
        }
        else if (elementName == "matrix" && !transformTargets.empty() && transformTargets.back() != TransformTarget::None)
        {
            const XMMATRIX matrix = ConvertMitsubaToDirectX(ParseMatrix(value));
            if (transformTargets.back() == TransformTarget::Sensor)
            {
                sensor.ToWorld = matrix;
                sensor.HasToWorld = true;
            }
            else
            {
                if (transformTargets.back() == TransformTarget::Shape)
                {
                    activeShape.ToWorld = matrix;
                    activeShape.HasToWorld = true;
                }
                else
                {
                    activeSpotEmitter.ToWorld = matrix;
                    activeSpotEmitter.HasToWorld = true;
                }
            }
        }
        else if (elementName == "float" && parentName == "sensor" && hasActiveSensor && name == "fov")
        {
            sensor.FieldOfView = ParseFloat(value, "sensor fov");
        }
        else if (elementName == "string" && parentName == "sensor" && hasActiveSensor && name == "fov_axis")
        {
            sensor.FieldOfViewAxis = value;
        }
        else if (elementName == "integer" && parentName == "film" && hasActiveSensor && name == "width")
        {
            sensor.FilmWidth = ParsePositiveInteger(value, "film width");
        }
        else if (elementName == "integer" && parentName == "film" && hasActiveSensor && name == "height")
        {
            sensor.FilmHeight = ParsePositiveInteger(value, "film height");
        }
        else if (elementName == "string" && parentName == "shape" && hasActiveShape && name == "filename")
        {
            activeShape.Filename = std::filesystem::path(value);
        }
        else if (elementName == "string" && parentName == "texture" && hasActiveTexture && ToLower(name) == "filename")
        {
            activeTexture.Filename = std::filesystem::path(value);
        }
        else if (elementName == "ref" && parentName == "shape" && hasActiveShape)
        {
            activeShape.MaterialReferenceId = GetAttribute(attributes, "id");
        }
        else if (elementName == "emitter" && parentName == "shape" && hasActiveShape)
        {
            activeShape.IsAreaEmitter = ToLower(GetAttribute(attributes, "type")) == "area";
        }
        else if (elementName == "rgb" && parentName == "emitter" && hasActiveShape && name == "radiance")
        {
            activeShape.Radiance = ParseColor3(value, "area-emitter radiance");
        }
        else if (elementName == "rgb" && parentName == "emitter" && hasActiveSpotEmitter && ToLower(name) == "intensity")
        {
            activeSpotEmitter.Intensity = ParseColor3(value, "spot-emitter intensity");
        }
        else if (elementName == "rgb" && hasActiveBsdf)
        {
            ApplyMitsubaBsdfColor(activeBsdf, name, ParseColor3(value, "BSDF color"));
        }
        else if (elementName == "float" && parentName == "emitter" && hasActiveSpotEmitter)
        {
            const std::string propertyName = ToLower(name);
            if (propertyName == "cutoffangle" || propertyName == "cutoff_angle")
            {
                activeSpotEmitter.CutoffAngleDegrees = ParseFloat(value, "spot-emitter cutoff angle");
            }
            else if (propertyName == "beamwidth" || propertyName == "beam_width")
            {
                activeSpotEmitter.BeamWidthDegrees = ParseFloat(value, "spot-emitter beam width");
            }
            else if (propertyName == "range")
            {
                activeSpotEmitter.Range = ParseFloat(value, "spot-emitter range");
            }
        }
        else if (elementName == "float" && hasActiveBsdf)
        {
            const std::string propertyName = ToLower(name);
            if (propertyName == "alpha" || propertyName == "roughness")
            {
                const float roughnessParameter = std::clamp(ParseFloat(value, "BSDF roughness"), 0.0f, 1.0f);
                activeBsdf.Roughness = std::clamp(std::sqrt(roughnessParameter), 0.02f, 1.0f);
            }
        }

        elementStack.push_back(elementName);
        if (isEmptyElement)
        {
            finalizeElement(elementName);
            elementStack.pop_back();
        }
    }

    if (!elementStack.empty() || !transformTargets.empty() || hasActiveShape || hasActiveSensor || hasActiveSpotEmitter)
    {
        throw std::runtime_error("Mitsuba XML scene ended with an unclosed element.");
    }
    if (hasActiveBsdf || hasActiveTexture || activeBsdfDepth != 0)
    {
        throw std::runtime_error("Mitsuba XML scene ended with an unclosed BSDF or texture.");
    }

    for (const MitsubaShape& shape : pendingObjShapes)
    {
        AddObjShape(
            shape,
            sceneDirectory,
            result.SceneData,
            result.Diagnostics,
            counters,
            materialIndices);
    }

    AddSensorCamera(sensor, result.SceneData, result.Diagnostics);
    if (options.RequireCamera && !result.SceneData.HasCamera())
    {
        throw std::runtime_error("Mitsuba XML scene has no supported perspective sensor: " + result.ScenePath.string());
    }
    if (options.RequireRenderableObject && result.SceneData.GetObjects().empty())
    {
        throw std::runtime_error("Mitsuba XML scene has no supported OBJ renderable objects: " + result.ScenePath.string());
    }

    std::ostringstream summary;
    summary << "Imported Mitsuba XML scene: nodes=" << result.SceneData.GetNodes().size()
            << ", objects=" << result.SceneData.GetObjects().size()
            << ", materials=" << result.SceneData.GetMaterials().size()
            << " (defaultPbr=1, importedBsdfPbr=" << counters.ImportedPbrMaterials
            << ", baseColorTextures=" << counters.ImportedBaseColorTextures << ")"
            << ", cameras=" << (result.SceneData.HasCamera() ? 1 : 0)
            << ", spotLights=" << result.SceneData.GetSpotLights().size()
            << ", areaLights=" << result.SceneData.GetAreaLights().size()
            << ", topLevelBsdfs=" << counters.TopLevelBsdfDefinitions
            << ", unresolvedMaterialRefs=" << counters.UnresolvedMaterialReferences
            << ", missingMeshes=" << counters.MissingMeshFiles
            << ", missingTextures=" << counters.MissingTextureFiles
            << ", unsupportedShapes=" << counters.UnsupportedShapes;
    result.Diagnostics.push_back(summary.str());
    return result;
}
//Modify End
