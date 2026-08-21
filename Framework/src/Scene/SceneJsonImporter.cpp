#include <Framework/Scene/SceneImporter.h>

#include <DirectXMath.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <variant>

//Modify Begin:2026-08-21 by Hui
namespace
{
    struct JsonValue
    {
        using Array = std::vector<JsonValue>;
        using Object = std::map<std::string, JsonValue, std::less<>>;
        std::variant<std::monostate, bool, double, std::string, Array, Object> Data;

        const Object& AsObject(const std::string_view context) const
        {
            if (!std::holds_alternative<Object>(Data))
            {
                throw std::runtime_error(std::string(context) + " must be an object.");
            }
            return std::get<Object>(Data);
        }

        const Array& AsArray(const std::string_view context) const
        {
            if (!std::holds_alternative<Array>(Data))
            {
                throw std::runtime_error(std::string(context) + " must be an array.");
            }
            return std::get<Array>(Data);
        }
    };

    class JsonParser final
    {
    public:
        explicit JsonParser(std::string source)
            : m_Source(std::move(source))
        {
        }

        JsonValue Parse()
        {
            JsonValue value = ParseValue();
            SkipWhitespace();
            if (m_Offset != m_Source.size())
            {
                Fail("Unexpected characters after root value");
            }
            return value;
        }

    private:
        JsonValue ParseValue()
        {
            SkipWhitespace();
            if (m_Offset >= m_Source.size())
            {
                Fail("Expected a JSON value");
            }

            const char character = m_Source[m_Offset];
            if (character == '{') return ParseObject();
            if (character == '[') return ParseArray();
            if (character == '"') return JsonValue{ ParseString() };
            if (character == 't') return ParseLiteral("true", true);
            if (character == 'f') return ParseLiteral("false", false);
            if (character == 'n') return ParseLiteral("null", std::monostate{});
            if (character == '-' || std::isdigit(static_cast<unsigned char>(character)) != 0) return ParseNumber();
            Fail("Invalid JSON value");
        }

        JsonValue ParseObject()
        {
            Consume('{');
            JsonValue::Object object;
            SkipWhitespace();
            if (TryConsume('}')) return JsonValue{ std::move(object) };
            while (true)
            {
                SkipWhitespace();
                if (Peek() != '"') Fail("Expected an object property name");
                std::string key = ParseString();
                SkipWhitespace();
                Consume(':');
                object.emplace(std::move(key), ParseValue());
                SkipWhitespace();
                if (TryConsume('}')) break;
                Consume(',');
            }
            return JsonValue{ std::move(object) };
        }

        JsonValue ParseArray()
        {
            Consume('[');
            JsonValue::Array array;
            SkipWhitespace();
            if (TryConsume(']')) return JsonValue{ std::move(array) };
            while (true)
            {
                array.push_back(ParseValue());
                SkipWhitespace();
                if (TryConsume(']')) break;
                Consume(',');
            }
            return JsonValue{ std::move(array) };
        }

        std::string ParseString()
        {
            Consume('"');
            std::string result;
            while (m_Offset < m_Source.size())
            {
                const char character = m_Source[m_Offset++];
                if (character == '"') return result;
                if (character == '\\')
                {
                    if (m_Offset >= m_Source.size()) Fail("Unterminated escape sequence");
                    const char escaped = m_Source[m_Offset++];
                    switch (escaped)
                    {
                    case '"': result += '"'; break;
                    case '\\': result += '\\'; break;
                    case '/': result += '/'; break;
                    case 'b': result += '\b'; break;
                    case 'f': result += '\f'; break;
                    case 'n': result += '\n'; break;
                    case 'r': result += '\r'; break;
                    case 't': result += '\t'; break;
                    default: Fail("Unsupported string escape");
                    }
                }
                else
                {
                    result += character;
                }
            }
            Fail("Unterminated string");
        }

        template <typename T>
        JsonValue ParseLiteral(const std::string_view literal, T value)
        {
            if (m_Source.compare(m_Offset, literal.size(), literal) != 0)
            {
                Fail("Invalid literal");
            }
            m_Offset += literal.size();
            return JsonValue{ value };
        }

        JsonValue ParseNumber()
        {
            const char* begin = m_Source.c_str() + m_Offset;
            char* end = nullptr;
            const double value = std::strtod(begin, &end);
            if (end == begin || !std::isfinite(value)) Fail("Invalid number");
            m_Offset = static_cast<size_t>(end - m_Source.c_str());
            return JsonValue{ value };
        }

        void SkipWhitespace()
        {
            while (m_Offset < m_Source.size() && std::isspace(static_cast<unsigned char>(m_Source[m_Offset])) != 0)
            {
                ++m_Offset;
            }
        }

        char Peek() const { return m_Offset < m_Source.size() ? m_Source[m_Offset] : '\0'; }

        bool TryConsume(const char expected)
        {
            if (Peek() != expected) return false;
            ++m_Offset;
            return true;
        }

        void Consume(const char expected)
        {
            SkipWhitespace();
            if (!TryConsume(expected))
            {
                Fail(std::string("Expected '") + expected + "'");
            }
        }

        [[noreturn]] void Fail(const std::string_view message) const
        {
            throw std::runtime_error("JSON parse error at byte " + std::to_string(m_Offset) + ": " + std::string(message));
        }

        std::string m_Source;
        size_t m_Offset = 0;
    };

    const JsonValue* Find(const JsonValue::Object& object, const std::string_view name)
    {
        const auto iterator = object.find(name);
        return iterator == object.end() ? nullptr : &iterator->second;
    }

    std::string ReadString(const JsonValue& value, const std::string_view context)
    {
        if (!std::holds_alternative<std::string>(value.Data))
        {
            throw std::runtime_error(std::string(context) + " must be a string.");
        }
        return std::get<std::string>(value.Data);
    }

    float ReadNumber(const JsonValue& value, const std::string_view context)
    {
        if (!std::holds_alternative<double>(value.Data))
        {
            throw std::runtime_error(std::string(context) + " must be a number.");
        }
        return static_cast<float>(std::get<double>(value.Data));
    }

    bool ReadBool(const JsonValue& value, const std::string_view context)
    {
        if (!std::holds_alternative<bool>(value.Data))
        {
            throw std::runtime_error(std::string(context) + " must be a boolean.");
        }
        return std::get<bool>(value.Data);
    }

    DirectX::XMFLOAT3 ReadFloat3(const JsonValue& value, const std::string_view context)
    {
        const JsonValue::Array& array = value.AsArray(context);
        if (array.size() != 3) throw std::runtime_error(std::string(context) + " must contain three values.");
        return { ReadNumber(array[0], context), ReadNumber(array[1], context), ReadNumber(array[2], context) };
    }

    DirectX::XMFLOAT4 ReadFloat4(const JsonValue& value, const std::string_view context)
    {
        const JsonValue::Array& array = value.AsArray(context);
        if (array.size() != 4) throw std::runtime_error(std::string(context) + " must contain four values.");
        return {
            ReadNumber(array[0], context), ReadNumber(array[1], context),
            ReadNumber(array[2], context), ReadNumber(array[3], context)
        };
    }

    DirectX::XMFLOAT4 ReadColor(const JsonValue& value, const std::string_view context)
    {
        const JsonValue::Array& array = value.AsArray(context);
        if (array.size() != 3 && array.size() != 4) throw std::runtime_error(std::string(context) + " must contain three or four values.");
        return {
            ReadNumber(array[0], context), ReadNumber(array[1], context), ReadNumber(array[2], context),
            array.size() == 4 ? ReadNumber(array[3], context) : 1.0f
        };
    }

    std::filesystem::path FindAssetsRoot(const std::filesystem::path& scenePath, const std::string& configuredAssetsRoot)
    {
        for (std::filesystem::path directory = scenePath.parent_path(); !directory.empty(); directory = directory.parent_path())
        {
            const std::filesystem::path candidate = directory / configuredAssetsRoot;
            if (std::filesystem::is_directory(candidate)) return std::filesystem::absolute(candidate);
            if (directory == directory.root_path()) break;
        }
        throw std::runtime_error("Unable to locate assetsRoot '" + configuredAssetsRoot + "' for JSON scene: " + scenePath.string());
    }

    std::filesystem::path ResolveAssetPath(const std::filesystem::path& assetsRoot, const JsonValue* value, const std::string_view context)
    {
        if (value == nullptr) return {};
        const std::filesystem::path path = ReadString(*value, context);
        return path.is_absolute() ? path : std::filesystem::absolute(assetsRoot / path);
    }

    DirectX::XMMATRIX ReadWorldMatrix(const JsonValue::Object& gameObject)
    {
        const JsonValue* transformValue = Find(gameObject, "transform");
        if (transformValue == nullptr) return DirectX::XMMatrixIdentity();
        const JsonValue::Object& transform = transformValue->AsObject("transform");
        DirectX::XMFLOAT3 position{};
        DirectX::XMFLOAT3 scale = { 1.0f, 1.0f, 1.0f };
        DirectX::XMVECTOR rotation = DirectX::XMQuaternionIdentity();
        if (const JsonValue* value = Find(transform, "position")) position = ReadFloat3(*value, "transform.position");
        if (const JsonValue* value = Find(transform, "scale")) scale = ReadFloat3(*value, "transform.scale");
        if (const JsonValue* value = Find(transform, "rotationQuaternion"))
        {
            const DirectX::XMFLOAT4 quaternion = ReadFloat4(*value, "transform.rotationQuaternion");
            rotation = DirectX::XMQuaternionNormalize(DirectX::XMLoadFloat4(&quaternion));
        }
        else if (const JsonValue* value = Find(transform, "rotationEulerDegrees"))
        {
            const DirectX::XMFLOAT3 rotationDegrees = ReadFloat3(*value, "transform.rotationEulerDegrees");
            rotation = DirectX::XMQuaternionRotationRollPitchYaw(
                DirectX::XMConvertToRadians(rotationDegrees.x),
                DirectX::XMConvertToRadians(rotationDegrees.y),
                DirectX::XMConvertToRadians(rotationDegrees.z));
        }
        return DirectX::XMMatrixScaling(scale.x, scale.y, scale.z) *
            DirectX::XMMatrixRotationQuaternion(rotation) *
            DirectX::XMMatrixTranslation(position.x, position.y, position.z);
    }

    DirectX::XMFLOAT3 ReadPosition(const JsonValue::Object& gameObject)
    {
        const JsonValue* transformValue = Find(gameObject, "transform");
        if (transformValue == nullptr) return {};
        const JsonValue::Object& transform = transformValue->AsObject("transform");
        const JsonValue* position = Find(transform, "position");
        return position == nullptr ? DirectX::XMFLOAT3{} : ReadFloat3(*position, "transform.position");
    }

    DirectX::XMVECTOR ReadRotation(const JsonValue::Object& gameObject)
    {
        const JsonValue* transformValue = Find(gameObject, "transform");
        if (transformValue == nullptr) return DirectX::XMQuaternionIdentity();
        const JsonValue::Object& transform = transformValue->AsObject("transform");
        if (const JsonValue* rotation = Find(transform, "rotationQuaternion"))
        {
            const DirectX::XMFLOAT4 quaternion = ReadFloat4(*rotation, "transform.rotationQuaternion");
            return DirectX::XMQuaternionNormalize(DirectX::XMLoadFloat4(&quaternion));
        }
        const JsonValue* rotation = Find(transform, "rotationEulerDegrees");
        if (rotation == nullptr) return DirectX::XMQuaternionIdentity();
        const DirectX::XMFLOAT3 degrees = ReadFloat3(*rotation, "transform.rotationEulerDegrees");
        return DirectX::XMQuaternionRotationRollPitchYaw(
            DirectX::XMConvertToRadians(degrees.x),
            DirectX::XMConvertToRadians(degrees.y),
            DirectX::XMConvertToRadians(degrees.z));
    }

    void AddMaterials(
        const JsonValue::Object& root,
        const std::filesystem::path& assetsRoot,
        Scene& scene,
        std::map<std::string, uint32_t, std::less<>>& materialIndices)
    {
        SceneMaterial defaultMaterial;
        defaultMaterial.Name = "Default PBR";
        defaultMaterial.SourceId = "__default__";
        defaultMaterial.BaseColor = { 0.85f, 0.85f, 0.85f, 1.0f };
        defaultMaterial.Roughness = 0.45f;
        defaultMaterial.IsPbrMaterial = true;
        materialIndices.emplace(defaultMaterial.SourceId, scene.AddMaterial(defaultMaterial));

        const JsonValue* materialsValue = Find(root, "materials");
        if (materialsValue == nullptr) return;
        for (const auto& [name, materialValue] : materialsValue->AsObject("materials"))
        {
            const JsonValue::Object& materialObject = materialValue.AsObject("materials." + name);
            SceneMaterial material;
            material.Name = name;
            material.SourceId = name;
            material.IsPbrMaterial = true;
            if (const JsonValue* value = Find(materialObject, "baseColor")) material.BaseColor = ReadColor(*value, "baseColor");
            if (const JsonValue* value = Find(materialObject, "emissionColor")) material.EmissionColor = ReadColor(*value, "emissionColor");
            if (const JsonValue* value = Find(materialObject, "metallic")) material.Metallic = ReadNumber(*value, "metallic");
            if (const JsonValue* value = Find(materialObject, "roughness")) material.Roughness = ReadNumber(*value, "roughness");
            if (const JsonValue* value = Find(materialObject, "normalScale")) material.NormalScale = ReadNumber(*value, "normalScale");
            if (const JsonValue* value = Find(materialObject, "occlusionStrength")) material.OcclusionStrength = ReadNumber(*value, "occlusionStrength");
            material.BaseMap.AssetPath = ResolveAssetPath(assetsRoot, Find(materialObject, "baseMap"), "baseMap");
            material.NormalMap.AssetPath = ResolveAssetPath(assetsRoot, Find(materialObject, "normalMap"), "normalMap");
            material.MetallicGlossMap.AssetPath = ResolveAssetPath(assetsRoot, Find(materialObject, "metallicGlossMap"), "metallicGlossMap");
            material.MetallicMap.AssetPath = ResolveAssetPath(assetsRoot, Find(materialObject, "metallicMap"), "metallicMap");
            material.RoughnessMap.AssetPath = ResolveAssetPath(assetsRoot, Find(materialObject, "roughnessMap"), "roughnessMap");
            material.OcclusionMap.AssetPath = ResolveAssetPath(assetsRoot, Find(materialObject, "occlusionMap"), "occlusionMap");
            material.EmissionMap.AssetPath = ResolveAssetPath(assetsRoot, Find(materialObject, "emissionMap"), "emissionMap");
            materialIndices.emplace(name, scene.AddMaterial(std::move(material)));
        }
    }

    void AddGameObjects(
        const JsonValue::Object& root,
        const std::filesystem::path& assetsRoot,
        Scene& scene,
        const std::map<std::string, uint32_t, std::less<>>& materialIndices)
    {
        const JsonValue* gameObjectsValue = Find(root, "gameObjects");
        if (gameObjectsValue == nullptr) return;
        const JsonValue::Array& gameObjects = gameObjectsValue->AsArray("gameObjects");
        for (size_t gameObjectIndex = 0; gameObjectIndex < gameObjects.size(); ++gameObjectIndex)
        {
            const JsonValue& gameObjectValue = gameObjects[gameObjectIndex];
            const JsonValue::Object& gameObject = gameObjectValue.AsObject("gameObject");
            const std::string name = Find(gameObject, "name") != nullptr ? ReadString(*Find(gameObject, "name"), "gameObject.name") : "GameObject";
            SceneNode node;
            node.Name = name;
            node.SourceId = "json:gameObject:" + std::to_string(gameObjectIndex);
            node.LocalMatrix = ReadWorldMatrix(gameObject);
            node.WorldMatrix = node.LocalMatrix;
            const uint32_t nodeIndex = scene.AddNode(std::move(node));
            if (const JsonValue* active = Find(gameObject, "active"); active != nullptr && !ReadBool(*active, "gameObject.active")) continue;
            const DirectX::XMFLOAT3 position = ReadPosition(gameObject);
            const DirectX::XMVECTOR rotation = ReadRotation(gameObject);

            if (const JsonValue* cameraValue = Find(gameObject, "camera"))
            {
                const JsonValue::Object& cameraObject = cameraValue->AsObject("camera");
                SceneCamera camera;
                camera.Name = name;
                camera.RuntimeCamera = std::make_shared<Camera>();
                camera.RuntimeCamera->SetTranslation(DirectX::XMLoadFloat3(&position));
                camera.RuntimeCamera->SetRotation(rotation);
                if (const JsonValue* value = Find(cameraObject, "fieldOfView")) camera.FieldOfView = ReadNumber(*value, "camera.fieldOfView");
                if (const JsonValue* value = Find(cameraObject, "nearClipPlane")) camera.NearClipPlane = ReadNumber(*value, "camera.nearClipPlane");
                if (const JsonValue* value = Find(cameraObject, "farClipPlane")) camera.FarClipPlane = ReadNumber(*value, "camera.farClipPlane");
                camera.SourceBinding.NodeIndex = nodeIndex;
                scene.SetCamera(camera);
            }

            if (const JsonValue* rendererValue = Find(gameObject, "meshRenderer"))
            {
                const JsonValue::Object& renderer = rendererValue->AsObject("meshRenderer");
                const JsonValue* meshValue = Find(renderer, "mesh");
                if (meshValue == nullptr) throw std::runtime_error("meshRenderer.mesh is required for " + name);
                SceneObject object;
                object.Name = name;
                object.WorldMatrix = ReadWorldMatrix(gameObject);
                object.NodeIndex = nodeIndex;
                if (std::holds_alternative<std::string>(meshValue->Data))
                {
                    object.Mesh.Kind = SceneMeshKind::ExternalMesh;
                    object.Mesh.AssetPath = ResolveAssetPath(assetsRoot, meshValue, "meshRenderer.mesh");
                }
                else
                {
                    const JsonValue::Object& meshObject = meshValue->AsObject("meshRenderer.mesh");
                    const std::string builtin = Find(meshObject, "builtin") != nullptr ? ReadString(*Find(meshObject, "builtin"), "meshRenderer.mesh.builtin") : "";
                    if (builtin == "plane") object.Mesh.Kind = SceneMeshKind::BuiltinPlane;
                    else if (builtin == "cube") object.Mesh.Kind = SceneMeshKind::BuiltinCube;
                    else
                    {
                        object.Mesh.Kind = SceneMeshKind::ExternalMesh;
                        object.Mesh.AssetPath = ResolveAssetPath(assetsRoot, Find(meshObject, "path"), "meshRenderer.mesh.path");
                        if (const JsonValue* submesh = Find(meshObject, "submesh")) object.Mesh.SubmeshName = ReadString(*submesh, "meshRenderer.mesh.submesh");
                    }
                }
                if (const JsonValue* materialValue = Find(renderer, "material"))
                {
                    const std::string materialName = ReadString(*materialValue, "meshRenderer.material");
                    const auto iterator = materialIndices.find(materialName);
                    if (iterator == materialIndices.end()) throw std::runtime_error("Unknown material '" + materialName + "' on " + name);
                    object.MaterialIndex = iterator->second;
                }
                else
                {
                    object.MaterialIndex = materialIndices.at("__default__");
                }
                scene.AddObject(std::move(object));
            }

            if (const JsonValue* lightValue = Find(gameObject, "light"))
            {
                const JsonValue::Object& lightObject = lightValue->AsObject("light");
                const std::string type = Find(lightObject, "type") != nullptr ? ReadString(*Find(lightObject, "type"), "light.type") : "point";
                const DirectX::XMFLOAT4 color = Find(lightObject, "color") != nullptr ? ReadColor(*Find(lightObject, "color"), "light.color") : DirectX::XMFLOAT4{ 1, 1, 1, 1 };
                const float intensity = Find(lightObject, "intensity") != nullptr ? ReadNumber(*Find(lightObject, "intensity"), "light.intensity") : 1.0f;
                if (type == "directional")
                {
                    DirectX::XMFLOAT3 direction{};
                    if (const JsonValue* directionValue = Find(lightObject, "direction"))
                    {
                        direction = ReadFloat3(*directionValue, "light.direction");
                    }
                    else
                    {
                        DirectX::XMStoreFloat3(&direction, DirectX::XMVector3Rotate(DirectX::XMVectorSet(0, 0, -1, 0), rotation));
                    }
                    DirectX::XMStoreFloat3(&direction, DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&direction)));
                    DirectionalLight light;
                    const float angularRadius = Find(lightObject, "angularRadius") != nullptr
                        ? ReadNumber(*Find(lightObject, "angularRadius"), "light.angularRadius")
                        : 0.009f;
                    light.m_DirectionWs = { direction.x, direction.y, direction.z, std::max(0.0f, angularRadius) };
                    light.m_Color = { color.x, color.y, color.z, intensity };
                    scene.AddDirectionalLight(light);
                }
                else if (type == "area")
                {
                    const DirectX::XMFLOAT3 areaSize = Find(lightObject, "areaSize") != nullptr ? ReadFloat3(*Find(lightObject, "areaSize"), "light.areaSize") : DirectX::XMFLOAT3{ 1, 1, 0 };
                    AreaLight light;
                    DirectX::XMFLOAT3 normal{};
                    DirectX::XMFLOAT3 axisU{};
                    DirectX::XMFLOAT3 axisV{};
                    if (const JsonValue* normalValue = Find(lightObject, "normal");
                        normalValue != nullptr && Find(lightObject, "axisU") != nullptr && Find(lightObject, "axisV") != nullptr)
                    {
                        normal = ReadFloat3(*normalValue, "light.normal");
                        axisU = ReadFloat3(*Find(lightObject, "axisU"), "light.axisU");
                        axisV = ReadFloat3(*Find(lightObject, "axisV"), "light.axisV");
                    }
                    else
                    {
                        DirectX::XMStoreFloat3(&normal, DirectX::XMVector3Normalize(DirectX::XMVector3Rotate(DirectX::XMVectorSet(0, 0, 1, 0), rotation)));
                        DirectX::XMStoreFloat3(&axisU, DirectX::XMVector3Normalize(DirectX::XMVector3Rotate(DirectX::XMVectorSet(1, 0, 0, 0), rotation)));
                        DirectX::XMStoreFloat3(&axisV, DirectX::XMVector3Normalize(DirectX::XMVector3Rotate(DirectX::XMVectorSet(0, 1, 0, 0), rotation)));
                    }
                    light.PositionWs = { position.x, position.y, position.z, 1.0f };
                    light.NormalWs = { normal.x, normal.y, normal.z, 0.0f };
                    light.AxisUWsAndExtent = { axisU.x, axisU.y, axisU.z, areaSize.x * 0.5f };
                    light.AxisVWsAndExtent = { axisV.x, axisV.y, axisV.z, areaSize.y * 0.5f };
                    light.Color = { color.x, color.y, color.z, intensity };
                    if (const JsonValue* range = Find(lightObject, "range")) light.Range = ReadNumber(*range, "light.range");
                    scene.AddAreaLight(light);
                }
                else if (type == "spot")
                {
                    DirectX::XMFLOAT3 direction{};
                    if (const JsonValue* directionValue = Find(lightObject, "direction"))
                    {
                        direction = ReadFloat3(*directionValue, "light.direction");
                    }
                    else
                    {
                        DirectX::XMStoreFloat3(
                            &direction,
                            DirectX::XMVector3Rotate(DirectX::XMVectorSet(0, 0, -1, 0), rotation));
                    }
                    DirectX::XMStoreFloat3(
                        &direction,
                        DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&direction)));
                    const float range = Find(lightObject, "range") != nullptr
                        ? ReadNumber(*Find(lightObject, "range"), "light.range")
                        : 20.0f;
                    const float outerAngleDegrees = Find(lightObject, "spotAngle") != nullptr
                        ? ReadNumber(*Find(lightObject, "spotAngle"), "light.spotAngle") * 0.5f
                        : 15.0f;
                    const float innerAngleDegrees = Find(lightObject, "innerSpotAngle") != nullptr
                        ? ReadNumber(*Find(lightObject, "innerSpotAngle"), "light.innerSpotAngle") * 0.5f
                        : outerAngleDegrees * 0.8f;
                    const PointLight attenuationSource(
                        { position.x, position.y, position.z, 1.0f },
                        std::max(0.1f, range));
                    SpotLight light;
                    light.PositionWs = attenuationSource.PositionWs;
                    light.DirectionWs = { direction.x, direction.y, direction.z, 0.0f };
                    light.Color = { color.x, color.y, color.z, 1.0f };
                    light.Intensity = intensity;
                    light.InnerConeAngle = DirectX::XMConvertToRadians(
                        std::clamp(innerAngleDegrees, 0.0f, outerAngleDegrees));
                    light.OuterConeAngle = DirectX::XMConvertToRadians(
                        std::clamp(outerAngleDegrees, 0.1f, 89.9f));
                    light.Range = attenuationSource.Range;
                    light.ConstantAttenuation = attenuationSource.ConstantAttenuation;
                    light.LinearAttenuation = attenuationSource.LinearAttenuation;
                    light.QuadraticAttenuation = attenuationSource.QuadraticAttenuation;
                    if (const JsonValue* sourceRadius = Find(lightObject, "sourceRadius"))
                    {
                        light.SourceRadius = std::max(0.0f, ReadNumber(*sourceRadius, "light.sourceRadius"));
                    }
                    scene.AddSpotLight(light);
                }
                else
                {
                    const float range = Find(lightObject, "range") != nullptr ? ReadNumber(*Find(lightObject, "range"), "light.range") : 20.0f;
                    PointLight light({ position.x, position.y, position.z, 1.0f }, std::max(0.1f, range));
                    light.Color = { color.x, color.y, color.z, intensity };
                    if (const JsonValue* sourceRadius = Find(lightObject, "sourceRadius"))
                    {
                        light.SourceRadius = std::max(0.0f, ReadNumber(*sourceRadius, "light.sourceRadius"));
                    }
                    light.RecalculateAttenuationCoefficients();
                    scene.AddPointLight(light);
                }
            }
        }
    }
}

SceneImportResult SceneImporter::ImportJsonFromFile(
    const std::filesystem::path& scenePath,
    const SceneImportOptions& options)
{
    if (!std::filesystem::exists(scenePath))
    {
        throw std::runtime_error("JSON scene file does not exist: " + scenePath.string());
    }

    std::ifstream input(scenePath);
    std::stringstream source;
    source << input.rdbuf();
    const JsonValue rootValue = JsonParser(source.str()).Parse();
    const JsonValue::Object& document = rootValue.AsObject("JSON scene root");
    const JsonValue::Object& root = Find(document, "scene") != nullptr ? Find(document, "scene")->AsObject("scene") : document;
    const std::string assetsRootName = Find(root, "assetsRoot") != nullptr ? ReadString(*Find(root, "assetsRoot"), "assetsRoot") : "Assets";
    const std::filesystem::path absoluteScenePath = std::filesystem::absolute(scenePath);
    const std::filesystem::path assetsRoot = FindAssetsRoot(absoluteScenePath, assetsRootName);

    SceneImportResult result;
    result.ScenePath = absoluteScenePath;
    result.SceneData.SetSourcePaths(absoluteScenePath, assetsRoot.parent_path(), assetsRoot);
    std::map<std::string, uint32_t, std::less<>> materialIndices;
    AddMaterials(root, assetsRoot, result.SceneData, materialIndices);

    if (const JsonValue* renderSettingsValue = Find(root, "renderSettings"))
    {
        const JsonValue::Object& renderSettings = renderSettingsValue->AsObject("renderSettings");
        SceneSkybox skybox;
        if (const JsonValue* ambientColor = Find(renderSettings, "ambientColor")) skybox.AmbientColorAndIntensity = ReadColor(*ambientColor, "renderSettings.ambientColor");
        if (const JsonValue* ambientIntensity = Find(renderSettings, "ambientIntensity")) skybox.AmbientColorAndIntensity.w = ReadNumber(*ambientIntensity, "renderSettings.ambientIntensity");
        skybox.Texture.AssetPath = ResolveAssetPath(assetsRoot, Find(renderSettings, "skybox"), "renderSettings.skybox");
        result.SceneData.SetSkybox(skybox);
    }

    AddGameObjects(root, assetsRoot, result.SceneData, materialIndices);
    if (options.RequireCamera && !result.SceneData.HasCamera())
    {
        throw std::runtime_error("JSON scene has no camera: " + absoluteScenePath.string());
    }
    if (options.RequireRenderableObject && result.SceneData.GetObjects().empty())
    {
        throw std::runtime_error("JSON scene has no supported renderable objects: " + absoluteScenePath.string());
    }

    result.Diagnostics.push_back(
        "Imported JSON scene: objects=" + std::to_string(result.SceneData.GetObjects().size()) +
        ", nodes=" + std::to_string(result.SceneData.GetNodes().size()) +
        ", materials=" + std::to_string(result.SceneData.GetMaterials().size()) +
        ", directionalLights=" + std::to_string(result.SceneData.GetDirectionalLights().size()) +
        ", pointLights=" + std::to_string(result.SceneData.GetPointLights().size()) +
        ", spotLights=" + std::to_string(result.SceneData.GetSpotLights().size()) +
        ", areaLights=" + std::to_string(result.SceneData.GetAreaLights().size()));
    return result;
}

void SceneImporter::ApplyJsonRuntimeState(
    const std::filesystem::path& statePath,
    Scene& scene)
{
    std::ifstream input(statePath, std::ios::binary);
    if (!input)
    {
        throw std::runtime_error("Unable to open JSON runtime state: " + statePath.string());
    }
    JsonValue rootValue = JsonParser(std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>())).Parse();
    const JsonValue::Object& root = rootValue.AsObject("runtime state");

    if (const JsonValue* cameraValue = Find(root, "camera"))
    {
        const JsonValue::Object& cameraObject = cameraValue->AsObject("camera");
        SceneCamera camera = scene.GetCamera();
        if (const JsonValue* position = Find(cameraObject, "position"))
        {
            const DirectX::XMFLOAT3 value = ReadFloat3(*position, "camera.position");
            camera.RuntimeCamera->SetTranslation(DirectX::XMLoadFloat3(&value));
        }
        if (const JsonValue* rotation = Find(cameraObject, "rotationQuaternion"))
        {
            const DirectX::XMFLOAT4 value = ReadFloat4(*rotation, "camera.rotationQuaternion");
            camera.RuntimeCamera->SetRotation(DirectX::XMQuaternionNormalize(DirectX::XMLoadFloat4(&value)));
        }
        if (const JsonValue* fieldOfView = Find(cameraObject, "fieldOfView")) camera.FieldOfView = ReadNumber(*fieldOfView, "camera.fieldOfView");
        if (const JsonValue* nearClip = Find(cameraObject, "nearClipPlane")) camera.NearClipPlane = ReadNumber(*nearClip, "camera.nearClipPlane");
        if (const JsonValue* farClip = Find(cameraObject, "farClipPlane")) camera.FarClipPlane = ReadNumber(*farClip, "camera.farClipPlane");
        scene.SetCamera(camera);
    }

    if (const JsonValue* skyboxValue = Find(root, "skybox"))
    {
        SceneSkybox skybox = scene.GetSkybox();
        const JsonValue::Object& skyboxObject = skyboxValue->AsObject("skybox");
        if (const JsonValue* ambient = Find(skyboxObject, "ambientColorAndIntensity"))
        {
            skybox.AmbientColorAndIntensity = ReadFloat4(*ambient, "skybox.ambientColorAndIntensity");
        }
        scene.SetSkybox(skybox);
    }

    if (const JsonValue* lightGroupsValue = Find(root, "lightGroups"))
    {
        const JsonValue::Object& lightGroups = lightGroupsValue->AsObject("lightGroups");
        SceneLightGroupSettings settings = scene.GetLightGroupSettings();
        if (const JsonValue* directionalEnabled = Find(lightGroups, "directionalEnabled"))
        {
            settings.DirectionalLightsEnabled = ReadBool(*directionalEnabled, "lightGroups.directionalEnabled");
        }
        if (const JsonValue* pointEnabled = Find(lightGroups, "pointEnabled"))
        {
            settings.PointLightsEnabled = ReadBool(*pointEnabled, "lightGroups.pointEnabled");
        }
        if (const JsonValue* areaEnabled = Find(lightGroups, "areaEnabled"))
        {
            settings.AreaLightsEnabled = ReadBool(*areaEnabled, "lightGroups.areaEnabled");
        }
        scene.SetLightGroupSettings(settings);
    }

    if (const JsonValue* lightsValue = Find(root, "directionalLights"))
    {
        std::vector<DirectionalLight> lights;
        for (const JsonValue& value : lightsValue->AsArray("directionalLights"))
        {
            const JsonValue::Object& object = value.AsObject("directionalLight");
            DirectionalLight light{};
            light.m_DirectionWs = ReadFloat4(*Find(object, "directionAndAngularRadius"), "directionalLight.directionAndAngularRadius");
            light.m_Color = ReadFloat4(*Find(object, "colorAndIntensity"), "directionalLight.colorAndIntensity");
            lights.push_back(light);
        }
        scene.SetDirectionalLights(std::move(lights));
    }

    if (const JsonValue* lightsValue = Find(root, "pointLights"))
    {
        std::vector<PointLight> lights;
        for (const JsonValue& value : lightsValue->AsArray("pointLights"))
        {
            const JsonValue::Object& object = value.AsObject("pointLight");
            const DirectX::XMFLOAT4 position = ReadFloat4(*Find(object, "positionAndRange"), "pointLight.positionAndRange");
            PointLight light(position, std::max(0.1f, position.w));
            light.Color = ReadFloat4(*Find(object, "colorAndIntensity"), "pointLight.colorAndIntensity");
            light.SourceRadius = std::max(0.0f, ReadNumber(*Find(object, "sourceRadius"), "pointLight.sourceRadius"));
            light.RecalculateAttenuationCoefficients();
            lights.push_back(light);
        }
        scene.SetPointLights(std::move(lights));
    }

    if (const JsonValue* lightsValue = Find(root, "areaLights"))
    {
        std::vector<AreaLight> lights;
        for (const JsonValue& value : lightsValue->AsArray("areaLights"))
        {
            const JsonValue::Object& object = value.AsObject("areaLight");
            AreaLight light{};
            light.PositionWs = ReadFloat4(*Find(object, "positionAndRange"), "areaLight.positionAndRange");
            light.NormalWs = ReadFloat4(*Find(object, "normalAndType"), "areaLight.normalAndType");
            light.AxisUWsAndExtent = ReadFloat4(*Find(object, "axisUAndExtent"), "areaLight.axisUAndExtent");
            light.AxisVWsAndExtent = ReadFloat4(*Find(object, "axisVAndExtent"), "areaLight.axisVAndExtent");
            light.Color = ReadFloat4(*Find(object, "colorAndIntensity"), "areaLight.colorAndIntensity");
            light.Range = light.PositionWs.w;
            lights.push_back(light);
        }
        scene.SetAreaLights(std::move(lights));
    }
}

void SceneImporter::WriteJsonRuntimeState(
    const std::filesystem::path& statePath,
    const Scene& scene)
{
    std::ofstream output(statePath, std::ios::trunc);
    if (!output)
    {
        throw std::runtime_error("Unable to open JSON runtime state for writing: " + statePath.string());
    }
    output << std::fixed << std::setprecision(6);
    const auto writeFloat3 = [&output](const DirectX::XMFLOAT3& value)
    {
        output << '[' << value.x << ", " << value.y << ", " << value.z << ']';
    };
    const auto writeFloat4 = [&output](const DirectX::XMFLOAT4& value)
    {
        output << '[' << value.x << ", " << value.y << ", " << value.z << ", " << value.w << ']';
    };

    const SceneCamera& camera = scene.GetCamera();
    DirectX::XMFLOAT3 cameraPosition{};
    DirectX::XMFLOAT4 cameraRotation{};
    DirectX::XMStoreFloat3(&cameraPosition, camera.RuntimeCamera->GetTranslation());
    DirectX::XMStoreFloat4(&cameraRotation, camera.RuntimeCamera->GetRotation());
    output << "{\n  \"version\": 1,\n  \"camera\": { \"position\": ";
    writeFloat3(cameraPosition);
    output << ", \"rotationQuaternion\": ";
    writeFloat4(cameraRotation);
    output << ", \"fieldOfView\": " << camera.FieldOfView
        << ", \"nearClipPlane\": " << camera.NearClipPlane
        << ", \"farClipPlane\": " << camera.FarClipPlane << " },\n";
    output << "  \"skybox\": { \"ambientColorAndIntensity\": ";
    writeFloat4(scene.GetSkybox().AmbientColorAndIntensity);
    output << " },\n";

    const SceneLightGroupSettings& lightGroups = scene.GetLightGroupSettings();
    output << "  \"lightGroups\": { \"directionalEnabled\": "
        << (lightGroups.DirectionalLightsEnabled ? "true" : "false")
        << ", \"pointEnabled\": " << (lightGroups.PointLightsEnabled ? "true" : "false")
        << ", \"areaEnabled\": " << (lightGroups.AreaLightsEnabled ? "true" : "false")
        << " },\n";

    const auto writeArrayStart = [&output](const char* name)
    {
        output << "  \"" << name << "\": [";
    };
    writeArrayStart("directionalLights");
    for (size_t index = 0; index < scene.GetDirectionalLights().size(); ++index)
    {
        if (index != 0) output << ',';
        const DirectionalLight& light = scene.GetDirectionalLights()[index];
        output << "\n    { \"directionAndAngularRadius\": ";
        writeFloat4(light.m_DirectionWs);
        output << ", \"colorAndIntensity\": ";
        writeFloat4(light.m_Color);
        output << " }";
    }
    output << "\n  ],\n";

    writeArrayStart("pointLights");
    for (size_t index = 0; index < scene.GetPointLights().size(); ++index)
    {
        if (index != 0) output << ',';
        const PointLight& light = scene.GetPointLights()[index];
        output << "\n    { \"positionAndRange\": ";
        writeFloat4({ light.PositionWs.x, light.PositionWs.y, light.PositionWs.z, light.Range });
        output << ", \"colorAndIntensity\": ";
        writeFloat4(light.Color);
        output << ", \"sourceRadius\": " << light.SourceRadius << " }";
    }
    output << "\n  ],\n";

    writeArrayStart("areaLights");
    for (size_t index = 0; index < scene.GetAreaLights().size(); ++index)
    {
        if (index != 0) output << ',';
        const AreaLight& light = scene.GetAreaLights()[index];
        output << "\n    { \"positionAndRange\": ";
        writeFloat4({ light.PositionWs.x, light.PositionWs.y, light.PositionWs.z, light.Range });
        output << ", \"normalAndType\": ";
        writeFloat4(light.NormalWs);
        output << ", \"axisUAndExtent\": ";
        writeFloat4(light.AxisUWsAndExtent);
        output << ", \"axisVAndExtent\": ";
        writeFloat4(light.AxisVWsAndExtent);
        output << ", \"colorAndIntensity\": ";
        writeFloat4(light.Color);
        output << " }";
    }
    output << "\n  ]\n}\n";
    if (!output)
    {
        throw std::runtime_error("Failed while writing JSON runtime state: " + statePath.string());
    }
}
//Modify End
