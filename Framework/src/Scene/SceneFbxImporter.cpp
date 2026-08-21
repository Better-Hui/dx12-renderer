//Modify Begin:2026-08-21 by Hui
#include <Framework/Scene/SceneImporter.h>

#include "../Geometry/AssimpImportSettings.h"

#include <assimp/Importer.hpp>
#include <assimp/camera.h>
#include <assimp/light.h>
#include <assimp/material.h>
#include <assimp/scene.h>
#include <assimp/texture.h>

#include <DirectXMath.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <memory>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace DirectX;

namespace
{
    struct ImportContext
    {
        const aiScene& SourceScene;
        const std::filesystem::path& ScenePath;
        Scene& Destination;
        std::vector<std::string>& Diagnostics;
        std::unordered_map<std::string, std::shared_ptr<const SceneEmbeddedTexture>> EmbeddedTextures;
        std::unordered_map<std::string, std::vector<uint32_t>> NodesByName;
        size_t EmbeddedTextureCount = 0;
        size_t MissingTextureCount = 0;
    };

    struct SceneBounds
    {
        XMFLOAT3 Minimum = {
            (std::numeric_limits<float>::max)(),
            (std::numeric_limits<float>::max)(),
            (std::numeric_limits<float>::max)()
        };
        XMFLOAT3 Maximum = {
            -(std::numeric_limits<float>::max)(),
            -(std::numeric_limits<float>::max)(),
            -(std::numeric_limits<float>::max)()
        };
        bool Valid = false;

        void Include(const XMVECTOR point)
        {
            XMFLOAT3 value{};
            XMStoreFloat3(&value, point);
            Minimum.x = (std::min)(Minimum.x, value.x);
            Minimum.y = (std::min)(Minimum.y, value.y);
            Minimum.z = (std::min)(Minimum.z, value.z);
            Maximum.x = (std::max)(Maximum.x, value.x);
            Maximum.y = (std::max)(Maximum.y, value.y);
            Maximum.z = (std::max)(Maximum.z, value.z);
            Valid = true;
        }
    };

    std::string PathToUtf8(const std::filesystem::path& path)
    {
        const std::u8string value = path.u8string();
        return { reinterpret_cast<const char*>(value.data()), value.size() };
    }

    std::filesystem::path PathFromUtf8(const std::string& value)
    {
        const std::u8string utf8(
            reinterpret_cast<const char8_t*>(value.data()),
            reinterpret_cast<const char8_t*>(value.data() + value.size()));
        return std::filesystem::path(utf8);
    }

    std::string ToLower(std::string value)
    {
        std::ranges::transform(value, value.begin(), [](const unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
        return value;
    }

    XMMATRIX ToDirectXMatrix(const aiMatrix4x4& source)
    {
        const XMMATRIX columnVectorMatrix(
            source.a1, source.a2, source.a3, source.a4,
            source.b1, source.b2, source.b3, source.b4,
            source.c1, source.c2, source.c3, source.c4,
            source.d1, source.d2, source.d3, source.d4);
        return XMMatrixTranspose(columnVectorMatrix);
    }

    XMVECTOR ToVector(const aiVector3D& value, const float w)
    {
        return XMVectorSet(value.x, value.y, value.z, w);
    }

    XMFLOAT3 TransformPoint(const aiVector3D& value, const XMMATRIX& matrix)
    {
        XMFLOAT3 result{};
        XMStoreFloat3(&result, XMVector3TransformCoord(ToVector(value, 1.0f), matrix));
        return result;
    }

    XMFLOAT3 TransformDirection(const aiVector3D& value, const XMMATRIX& matrix)
    {
        XMVECTOR direction = XMVector3TransformNormal(ToVector(value, 0.0f), matrix);
        if (XMVectorGetX(XMVector3LengthSq(direction)) <= 1.0e-12f)
        {
            direction = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
        }
        XMFLOAT3 result{};
        XMStoreFloat3(&result, XMVector3Normalize(direction));
        return result;
    }

    XMMATRIX GetNodeWorldMatrix(const ImportContext& context, const std::string& nodeName, uint32_t& nodeIndex)
    {
        const auto iterator = context.NodesByName.find(nodeName);
        if (iterator == context.NodesByName.end() || iterator->second.empty())
        {
            nodeIndex = SceneNode::InvalidNodeIndex;
            return XMMatrixIdentity();
        }
        nodeIndex = iterator->second.front();
        return context.Destination.GetNodes().at(nodeIndex).WorldMatrix;
    }

    void IncludeMeshBounds(SceneBounds& bounds, const aiMesh& mesh, const XMMATRIX& worldMatrix)
    {
        if (!mesh.HasPositions() || mesh.mNumVertices == 0)
        {
            return;
        }

        aiVector3D minimum = mesh.mVertices[0];
        aiVector3D maximum = mesh.mVertices[0];
        for (unsigned int vertexIndex = 1; vertexIndex < mesh.mNumVertices; ++vertexIndex)
        {
            const aiVector3D& position = mesh.mVertices[vertexIndex];
            minimum.x = (std::min)(minimum.x, position.x);
            minimum.y = (std::min)(minimum.y, position.y);
            minimum.z = (std::min)(minimum.z, position.z);
            maximum.x = (std::max)(maximum.x, position.x);
            maximum.y = (std::max)(maximum.y, position.y);
            maximum.z = (std::max)(maximum.z, position.z);
        }

        for (unsigned int corner = 0; corner < 8; ++corner)
        {
            const XMVECTOR localPoint = XMVectorSet(
                (corner & 1u) != 0 ? maximum.x : minimum.x,
                (corner & 2u) != 0 ? maximum.y : minimum.y,
                (corner & 4u) != 0 ? maximum.z : minimum.z,
                1.0f);
            bounds.Include(XMVector3TransformCoord(localPoint, worldMatrix));
        }
    }

    std::filesystem::path ResolveExternalTexturePath(
        ImportContext& context,
        const std::string& textureReference)
    {
        std::filesystem::path referencePath = PathFromUtf8(textureReference);
        std::array<std::filesystem::path, 3> candidates = {
            referencePath,
            context.ScenePath.parent_path() / referencePath,
            context.ScenePath.parent_path() / referencePath.filename()
        };

        for (std::filesystem::path& candidate : candidates)
        {
            if (!candidate.is_absolute())
            {
                candidate = std::filesystem::absolute(candidate);
            }
            std::error_code errorCode;
            if (std::filesystem::is_regular_file(candidate, errorCode))
            {
                return std::filesystem::weakly_canonical(candidate, errorCode);
            }
        }

        const std::string referenceStem = ToLower(referencePath.stem().string());
        const std::array<std::string, 7> semanticSuffixes = {
            "basecolor", "albedo", "normal", "metallic", "roughness", "occlusion", "emissive"
        };
        std::string semanticSuffix;
        for (const std::string& candidateSuffix : semanticSuffixes)
        {
            if (referenceStem.ends_with(candidateSuffix))
            {
                semanticSuffix = candidateSuffix;
                break;
            }
        }

        std::string sourcePrefix = referenceStem;
        if (!semanticSuffix.empty())
        {
            sourcePrefix.resize(sourcePrefix.size() - semanticSuffix.size());
            while (!sourcePrefix.empty() && (sourcePrefix.back() == '_' || sourcePrefix.back() == '-'))
            {
                sourcePrefix.pop_back();
            }
            for (const std::string_view materialToken : { std::string_view("_defaultmaterial"), std::string_view("_material") })
            {
                if (sourcePrefix.ends_with(materialToken))
                {
                    sourcePrefix.resize(sourcePrefix.size() - materialToken.size());
                }
            }
        }

        std::vector<std::filesystem::path> semanticMatches;
        if (!semanticSuffix.empty() && !sourcePrefix.empty())
        {
            std::error_code iteratorError;
            for (std::filesystem::directory_iterator iterator(context.ScenePath.parent_path(), iteratorError), end;
                !iteratorError && iterator != end;
                iterator.increment(iteratorError))
            {
                if (!iterator->is_regular_file(iteratorError))
                {
                    continue;
                }
                const std::string candidateStem = ToLower(iterator->path().stem().string());
                if (candidateStem.starts_with(sourcePrefix) && candidateStem.ends_with(semanticSuffix))
                {
                    semanticMatches.push_back(iterator->path());
                }
            }
        }
        if (semanticMatches.size() == 1)
        {
            context.Diagnostics.push_back(
                "FBX texture reference '" + textureReference + "' was remapped to same-directory asset '" +
                semanticMatches.front().filename().string() + "'.");
            return std::filesystem::weakly_canonical(semanticMatches.front());
        }

        ++context.MissingTextureCount;
        context.Diagnostics.push_back(
            "FBX texture could not be resolved: '" + textureReference + "'.");
        return {};
    }

    std::shared_ptr<const SceneEmbeddedTexture> ImportEmbeddedTexture(
        ImportContext& context,
        const std::string& textureReference,
        const aiTexture& sourceTexture)
    {
        if (const auto cached = context.EmbeddedTextures.find(textureReference);
            cached != context.EmbeddedTextures.end())
        {
            return cached->second;
        }

        auto texture = std::make_shared<SceneEmbeddedTexture>();
        texture->CacheKey = PathToUtf8(context.ScenePath) + "#embedded:" + textureReference;
        texture->FormatHint.assign(
            sourceTexture.achFormatHint,
            strnlen_s(sourceTexture.achFormatHint, sizeof(sourceTexture.achFormatHint)));

        if (sourceTexture.mHeight == 0)
        {
            if (sourceTexture.pcData == nullptr || sourceTexture.mWidth == 0)
            {
                context.Diagnostics.push_back(
                    "FBX embedded texture is empty: '" + textureReference + "'.");
                return {};
            }
            texture->Encoding = SceneEmbeddedTextureEncoding::EncodedFile;
            texture->Data.resize(sourceTexture.mWidth);
            std::memcpy(texture->Data.data(), sourceTexture.pcData, sourceTexture.mWidth);
        }
        else
        {
            const uint64_t texelCount = static_cast<uint64_t>(sourceTexture.mWidth) * sourceTexture.mHeight;
            if (sourceTexture.pcData == nullptr || texelCount == 0 ||
                texelCount > (std::numeric_limits<size_t>::max)() / sizeof(aiTexel))
            {
                context.Diagnostics.push_back(
                    "FBX embedded raw texture has invalid dimensions: '" + textureReference + "'.");
                return {};
            }
            texture->Encoding = SceneEmbeddedTextureEncoding::Bgra8;
            texture->Width = sourceTexture.mWidth;
            texture->Height = sourceTexture.mHeight;
            texture->Data.resize(static_cast<size_t>(texelCount) * sizeof(aiTexel));
            std::memcpy(texture->Data.data(), sourceTexture.pcData, texture->Data.size());
        }

        ++context.EmbeddedTextureCount;
        context.EmbeddedTextures.emplace(textureReference, texture);
        return texture;
    }

    SceneTextureBinding ImportTexture(
        ImportContext& context,
        const aiMaterial& material,
        const std::initializer_list<aiTextureType> textureTypes)
    {
        SceneTextureBinding binding;
        for (const aiTextureType textureType : textureTypes)
        {
            if (material.GetTextureCount(textureType) == 0)
            {
                continue;
            }

            aiString texturePath;
            if (material.GetTexture(textureType, 0, &texturePath) != AI_SUCCESS || texturePath.length == 0)
            {
                continue;
            }
            if (material.GetTextureCount(textureType) > 1)
            {
                context.Diagnostics.push_back(
                    "FBX material texture stack contains multiple entries; only the first is used.");
            }

            const std::string textureReference = texturePath.C_Str();
            if (const aiTexture* embeddedTexture = context.SourceScene.GetEmbeddedTexture(textureReference.c_str()))
            {
                binding.EmbeddedTexture = ImportEmbeddedTexture(context, textureReference, *embeddedTexture);
            }
            else
            {
                binding.AssetPath = ResolveExternalTexturePath(context, textureReference);
            }

            aiUVTransform uvTransform;
            if (aiGetMaterialUVTransform(
                &material,
                AI_MATKEY_UVTRANSFORM(textureType, 0),
                &uvTransform) == AI_SUCCESS)
            {
                binding.ScaleOffset = {
                    uvTransform.mScaling.x,
                    uvTransform.mScaling.y,
                    uvTransform.mTranslation.x,
                    uvTransform.mTranslation.y
                };
                if (std::abs(uvTransform.mRotation) > 1.0e-6f)
                {
                    context.Diagnostics.push_back(
                        "FBX texture UV rotation is not represented by SceneTextureBinding and was ignored.");
                }
            }
            return binding;
        }
        return binding;
    }

    XMFLOAT4 ReadColor(const aiMaterial& material, const char* key, const unsigned int type, const unsigned int index, const XMFLOAT4& fallback)
    {
        aiColor4D color;
        if (aiGetMaterialColor(&material, key, type, index, &color) != AI_SUCCESS)
        {
            return fallback;
        }
        return { color.r, color.g, color.b, color.a };
    }

    float ReadFloat(const aiMaterial& material, const char* key, const unsigned int type, const unsigned int index, const float fallback)
    {
        float value = fallback;
        return aiGetMaterialFloat(&material, key, type, index, &value) == AI_SUCCESS && std::isfinite(value)
            ? value
            : fallback;
    }

    void ImportMaterials(ImportContext& context)
    {
        if (context.SourceScene.mNumMaterials == 0)
        {
            SceneMaterial material;
            material.Name = "Default FBX Material";
            material.SourceId = "fbx:material:default";
            material.IsPbrMaterial = true;
            context.Destination.AddMaterial(std::move(material));
            return;
        }

        for (unsigned int materialIndex = 0; materialIndex < context.SourceScene.mNumMaterials; ++materialIndex)
        {
            const aiMaterial* sourceMaterial = context.SourceScene.mMaterials[materialIndex];
            if (sourceMaterial == nullptr)
            {
                SceneMaterial fallback;
                fallback.Name = "Material_" + std::to_string(materialIndex);
                fallback.SourceId = "fbx:material:" + std::to_string(materialIndex);
                fallback.IsPbrMaterial = true;
                context.Destination.AddMaterial(std::move(fallback));
                context.Diagnostics.push_back(
                    "FBX contains a null material at index " + std::to_string(materialIndex) + ".");
                continue;
            }

            SceneMaterial material;
            aiString materialName;
            if (aiGetMaterialString(sourceMaterial, AI_MATKEY_NAME, &materialName) == AI_SUCCESS && materialName.length > 0)
            {
                material.Name = materialName.C_Str();
            }
            else
            {
                material.Name = "Material_" + std::to_string(materialIndex);
            }
            material.SourceId = "fbx:material:" + std::to_string(materialIndex);
            material.BaseColor = ReadColor(*sourceMaterial, AI_MATKEY_BASE_COLOR, { 1, 1, 1, 1 });
            if (material.BaseColor.x == 1.0f && material.BaseColor.y == 1.0f &&
                material.BaseColor.z == 1.0f && material.BaseColor.w == 1.0f)
            {
                material.BaseColor = ReadColor(*sourceMaterial, AI_MATKEY_COLOR_DIFFUSE, material.BaseColor);
            }
            material.SpecColor = ReadColor(*sourceMaterial, AI_MATKEY_COLOR_SPECULAR, { 0.04f, 0.04f, 0.04f, 1.0f });
            material.EmissionColor = ReadColor(*sourceMaterial, AI_MATKEY_COLOR_EMISSIVE, { 0, 0, 0, 1 });
            const float emissiveIntensity = (std::max)(0.0f, ReadFloat(*sourceMaterial, AI_MATKEY_EMISSIVE_INTENSITY, 1.0f));
            material.EmissionColor.x *= emissiveIntensity;
            material.EmissionColor.y *= emissiveIntensity;
            material.EmissionColor.z *= emissiveIntensity;
            material.BaseColor.w *= std::clamp(ReadFloat(*sourceMaterial, AI_MATKEY_OPACITY, 1.0f), 0.0f, 1.0f);
            material.Metallic = std::clamp(ReadFloat(*sourceMaterial, AI_MATKEY_METALLIC_FACTOR, 0.0f), 0.0f, 1.0f);

            const float shininess = (std::max)(0.0f, ReadFloat(*sourceMaterial, AI_MATKEY_SHININESS, 0.0f));
            const float roughnessFallback = shininess > 0.0f
                ? std::sqrt(2.0f / (shininess + 2.0f))
                : 0.5f;
            material.Roughness = std::clamp(
                ReadFloat(*sourceMaterial, AI_MATKEY_ROUGHNESS_FACTOR, roughnessFallback),
                0.0f,
                1.0f);
            material.NormalScale = (std::max)(0.0f, ReadFloat(*sourceMaterial, AI_MATKEY_BUMPSCALING, 1.0f));
            material.BaseMap = ImportTexture(context, *sourceMaterial, { aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE });
            material.NormalMap = ImportTexture(context, *sourceMaterial, { aiTextureType_NORMALS, aiTextureType_NORMAL_CAMERA, aiTextureType_HEIGHT });
            material.MetallicMap = ImportTexture(context, *sourceMaterial, { aiTextureType_METALNESS });
            material.RoughnessMap = ImportTexture(context, *sourceMaterial, { aiTextureType_DIFFUSE_ROUGHNESS, aiTextureType_SHININESS });
            material.OcclusionMap = ImportTexture(context, *sourceMaterial, { aiTextureType_AMBIENT_OCCLUSION, aiTextureType_LIGHTMAP });
            material.EmissionMap = ImportTexture(context, *sourceMaterial, { aiTextureType_EMISSION_COLOR, aiTextureType_EMISSIVE });
            material.IsPbrMaterial = true;
            context.Destination.AddMaterial(std::move(material));
        }
    }

    void ImportNode(
        ImportContext& context,
        const aiNode& sourceNode,
        const uint32_t parentIndex,
        const XMMATRIX& parentWorldMatrix,
        const std::string& parentPath,
        const unsigned int siblingIndex,
        SceneBounds& bounds)
    {
        SceneNode node;
        node.Name = sourceNode.mName.length > 0 ? sourceNode.mName.C_Str() : "Node";
        node.SourceId = parentPath + "/" + node.Name + "[" + std::to_string(siblingIndex) + "]";
        node.LocalMatrix = ToDirectXMatrix(sourceNode.mTransformation);
        node.WorldMatrix = node.LocalMatrix * parentWorldMatrix;
        const uint32_t nodeIndex = context.Destination.AddNode(std::move(node));
        context.Destination.SetNodeParent(nodeIndex, parentIndex);
        context.NodesByName[context.Destination.GetNodes()[nodeIndex].Name].push_back(nodeIndex);

        for (unsigned int nodeMeshIndex = 0; nodeMeshIndex < sourceNode.mNumMeshes; ++nodeMeshIndex)
        {
            const unsigned int sourceMeshIndex = sourceNode.mMeshes[nodeMeshIndex];
            if (sourceMeshIndex >= context.SourceScene.mNumMeshes || context.SourceScene.mMeshes[sourceMeshIndex] == nullptr)
            {
                throw std::runtime_error(
                    "FBX node references an invalid mesh index " + std::to_string(sourceMeshIndex) + ".");
            }

            const aiMesh& mesh = *context.SourceScene.mMeshes[sourceMeshIndex];
            if ((mesh.mPrimitiveTypes & aiPrimitiveType_TRIANGLE) == 0 ||
                !mesh.HasPositions() || mesh.mNumFaces == 0)
            {
                context.Diagnostics.push_back(
                    "FBX node '" + context.Destination.GetNodes()[nodeIndex].Name +
                    "' references a non-renderable mesh that was skipped.");
                continue;
            }

            SceneObject object;
            const std::string meshName = mesh.mName.length > 0
                ? mesh.mName.C_Str()
                : "Mesh_" + std::to_string(sourceMeshIndex);
            object.Name = sourceNode.mNumMeshes == 1
                ? context.Destination.GetNodes()[nodeIndex].Name
                : context.Destination.GetNodes()[nodeIndex].Name + "/" + meshName;
            object.WorldMatrix = context.Destination.GetNodes()[nodeIndex].WorldMatrix;
            object.Mesh.Kind = SceneMeshKind::ExternalMesh;
            object.Mesh.AssetPath = context.ScenePath;
            object.Mesh.SubmeshName = meshName;
            object.Mesh.SubmeshIndex = sourceMeshIndex;
            object.MaterialIndex = mesh.mMaterialIndex < context.Destination.GetMaterials().size()
                ? mesh.mMaterialIndex
                : 0u;
            object.NodeIndex = nodeIndex;
            context.Destination.AddObject(std::move(object));
            IncludeMeshBounds(bounds, mesh, context.Destination.GetNodes()[nodeIndex].WorldMatrix);
        }

        for (unsigned int childIndex = 0; childIndex < sourceNode.mNumChildren; ++childIndex)
        {
            if (sourceNode.mChildren[childIndex] == nullptr)
            {
                throw std::runtime_error("FBX scene graph contains a null child node.");
            }
            ImportNode(
                context,
                *sourceNode.mChildren[childIndex],
                nodeIndex,
                context.Destination.GetNodes()[nodeIndex].WorldMatrix,
                context.Destination.GetNodes()[nodeIndex].SourceId,
                childIndex,
                bounds);
        }
    }

    float CalculateLightRange(const aiLight& light)
    {
        constexpr float CutoffDenominator = 100.0f;
        const float constant = (std::max)(0.0f, light.mAttenuationConstant);
        const float linear = (std::max)(0.0f, light.mAttenuationLinear);
        const float quadratic = (std::max)(0.0f, light.mAttenuationQuadratic);
        float range = 20.0f;
        if (quadratic > 1.0e-8f)
        {
            const float discriminant = linear * linear - 4.0f * quadratic * (constant - CutoffDenominator);
            if (discriminant >= 0.0f)
            {
                range = (-linear + std::sqrt(discriminant)) / (2.0f * quadratic);
            }
        }
        else if (linear > 1.0e-8f)
        {
            range = (CutoffDenominator - constant) / linear;
        }
        return std::clamp(std::isfinite(range) ? range : 20.0f, 0.1f, 10000.0f);
    }

    XMFLOAT4 ConvertLightColor(const aiColor3D& color)
    {
        return {
            (std::max)(0.0f, color.r),
            (std::max)(0.0f, color.g),
            (std::max)(0.0f, color.b),
            1.0f
        };
    }

    void ImportLights(ImportContext& context)
    {
        SceneSkybox skybox = context.Destination.GetSkybox();
        for (unsigned int lightIndex = 0; lightIndex < context.SourceScene.mNumLights; ++lightIndex)
        {
            const aiLight* sourceLight = context.SourceScene.mLights[lightIndex];
            if (sourceLight == nullptr)
            {
                context.Diagnostics.push_back(
                    "FBX contains a null light at index " + std::to_string(lightIndex) + ".");
                continue;
            }

            uint32_t nodeIndex = SceneNode::InvalidNodeIndex;
            const XMMATRIX worldMatrix = GetNodeWorldMatrix(context, sourceLight->mName.C_Str(), nodeIndex);
            if (nodeIndex == SceneNode::InvalidNodeIndex)
            {
                context.Diagnostics.push_back(
                    "FBX light node was not found for '" + std::string(sourceLight->mName.C_Str()) + "'.");
            }
            const XMFLOAT3 position = TransformPoint(sourceLight->mPosition, worldMatrix);
            const XMFLOAT3 direction = TransformDirection(sourceLight->mDirection, worldMatrix);
            const XMFLOAT4 color = ConvertLightColor(sourceLight->mColorDiffuse);

            switch (sourceLight->mType)
            {
            case aiLightSource_DIRECTIONAL:
            {
                DirectionalLight light;
                light.m_DirectionWs = { direction.x, direction.y, direction.z, 0.009f };
                light.m_Color = color;
                context.Destination.AddDirectionalLight(light);
                break;
            }
            case aiLightSource_POINT:
            {
                PointLight light({ position.x, position.y, position.z, 1.0f }, CalculateLightRange(*sourceLight));
                light.Color = color;
                light.ConstantAttenuation = sourceLight->mAttenuationConstant;
                light.LinearAttenuation = sourceLight->mAttenuationLinear;
                light.QuadraticAttenuation = sourceLight->mAttenuationQuadratic;
                context.Destination.AddPointLight(light);
                break;
            }
            case aiLightSource_SPOT:
            {
                SpotLight light;
                light.PositionWs = { position.x, position.y, position.z, 1.0f };
                light.DirectionWs = { direction.x, direction.y, direction.z, 0.0f };
                light.Color = color;
                light.Intensity = 1.0f;
                light.InnerConeAngle = (std::max)(0.0f, sourceLight->mAngleInnerCone);
                light.OuterConeAngle = (std::max)(light.InnerConeAngle, sourceLight->mAngleOuterCone);
                light.Range = CalculateLightRange(*sourceLight);
                light.ConstantAttenuation = sourceLight->mAttenuationConstant;
                light.LinearAttenuation = sourceLight->mAttenuationLinear;
                light.QuadraticAttenuation = sourceLight->mAttenuationQuadratic;
                context.Destination.AddSpotLight(light);
                break;
            }
            case aiLightSource_AREA:
            {
                const XMFLOAT3 up = TransformDirection(sourceLight->mUp, worldMatrix);
                XMVECTOR axisUVector = XMVector3Cross(XMLoadFloat3(&up), XMLoadFloat3(&direction));
                if (XMVectorGetX(XMVector3LengthSq(axisUVector)) <= 1.0e-12f)
                {
                    axisUVector = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
                }
                XMFLOAT3 axisU{};
                XMStoreFloat3(&axisU, XMVector3Normalize(axisUVector));
                AreaLight light;
                light.PositionWs = { position.x, position.y, position.z, 1.0f };
                light.NormalWs = { direction.x, direction.y, direction.z, 0.0f };
                light.AxisUWsAndExtent = {
                    axisU.x, axisU.y, axisU.z,
                    sourceLight->mSize.x > 0.0f ? sourceLight->mSize.x * 0.5f : 0.5f
                };
                light.AxisVWsAndExtent = {
                    up.x, up.y, up.z,
                    sourceLight->mSize.y > 0.0f ? sourceLight->mSize.y * 0.5f : 0.5f
                };
                light.Color = color;
                light.Range = CalculateLightRange(*sourceLight);
                context.Destination.AddAreaLight(light);
                break;
            }
            case aiLightSource_AMBIENT:
                skybox.AmbientColorAndIntensity.x += color.x;
                skybox.AmbientColorAndIntensity.y += color.y;
                skybox.AmbientColorAndIntensity.z += color.z;
                skybox.AmbientColorAndIntensity.w = 1.0f;
                break;
            default:
                context.Diagnostics.push_back(
                    "FBX light type is unsupported for '" + std::string(sourceLight->mName.C_Str()) + "'.");
                break;
            }
        }
        context.Destination.SetSkybox(skybox);
    }

    void ImportCameras(ImportContext& context)
    {
        if (context.SourceScene.mNumCameras == 0)
        {
            return;
        }
        if (context.SourceScene.mNumCameras > 1)
        {
            context.Diagnostics.push_back(
                "FBX contains multiple cameras; only the first camera is selected as active.");
        }

        const aiCamera* sourceCamera = context.SourceScene.mCameras[0];
        if (sourceCamera == nullptr)
        {
            context.Diagnostics.push_back("FBX primary camera entry is null.");
            return;
        }

        uint32_t nodeIndex = SceneNode::InvalidNodeIndex;
        const XMMATRIX worldMatrix = GetNodeWorldMatrix(context, sourceCamera->mName.C_Str(), nodeIndex);
        if (nodeIndex == SceneNode::InvalidNodeIndex)
        {
            context.Diagnostics.push_back(
                "FBX camera node was not found for '" + std::string(sourceCamera->mName.C_Str()) + "'.");
        }

        const XMFLOAT3 position = TransformPoint(sourceCamera->mPosition, worldMatrix);
        const XMFLOAT3 lookDirection = TransformDirection(sourceCamera->mLookAt, worldMatrix);
        const XMFLOAT3 up = TransformDirection(sourceCamera->mUp, worldMatrix);
        const XMVECTOR eye = XMLoadFloat3(&position);
        const XMVECTOR target = XMVectorAdd(eye, XMLoadFloat3(&lookDirection));

        SceneCamera camera;
        camera.Name = sourceCamera->mName.length > 0 ? sourceCamera->mName.C_Str() : "FBX Camera";
        camera.RuntimeCamera = std::make_shared<Camera>();
        camera.RuntimeCamera->SetLookAt(eye, target, XMLoadFloat3(&up));
        const float aspect = sourceCamera->mAspect > 1.0e-6f ? sourceCamera->mAspect : 16.0f / 9.0f;
        const float verticalFov = 2.0f * std::atan(std::tan(sourceCamera->mHorizontalFOV) / aspect);
        camera.FieldOfView = std::clamp(
            XMConvertToDegrees(std::isfinite(verticalFov) && verticalFov > 1.0e-4f ? verticalFov : XM_PIDIV4),
            1.0f,
            179.0f);
        camera.NearClipPlane = (std::max)(0.001f, sourceCamera->mClipPlaneNear);
        camera.FarClipPlane = (std::max)(camera.NearClipPlane + 0.001f, sourceCamera->mClipPlaneFar);
        camera.SourceBinding.NodeIndex = nodeIndex;
        context.Destination.SetCamera(camera);
    }

    void GenerateFallbackCamera(Scene& scene, const SceneBounds& bounds)
    {
        if (!bounds.Valid)
        {
            return;
        }

        const XMVECTOR minimum = XMLoadFloat3(&bounds.Minimum);
        const XMVECTOR maximum = XMLoadFloat3(&bounds.Maximum);
        const XMVECTOR center = XMVectorScale(XMVectorAdd(minimum, maximum), 0.5f);
        const float radius = (std::max)(0.5f, XMVectorGetX(XMVector3Length(XMVectorSubtract(maximum, minimum))) * 0.5f);
        const XMVECTOR eye = XMVectorAdd(center, XMVectorSet(0.0f, radius * 0.35f, -radius * 2.5f, 0.0f));

        SceneCamera camera;
        camera.Name = "Generated FBX Camera";
        camera.RuntimeCamera = std::make_shared<Camera>();
        camera.RuntimeCamera->SetLookAt(eye, center, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
        camera.FieldOfView = 60.0f;
        camera.NearClipPlane = (std::max)(0.01f, radius * 0.001f);
        camera.FarClipPlane = (std::max)(100.0f, radius * 8.0f);
        scene.SetCamera(camera);
    }
}

SceneImportResult SceneImporter::ImportFbxFromFile(
    const std::filesystem::path& scenePath,
    const SceneImportOptions& options)
{
    if (!std::filesystem::exists(scenePath))
    {
        throw std::runtime_error("FBX scene file does not exist: " + scenePath.string());
    }
    if (ToLower(scenePath.extension().string()) != ".fbx")
    {
        throw std::invalid_argument("ImportFbxFromFile requires an .fbx file.");
    }

    SceneImportResult result;
    result.ScenePath = std::filesystem::weakly_canonical(std::filesystem::absolute(scenePath));
    result.SceneData.SetSourcePaths(
        result.ScenePath,
        result.ScenePath.parent_path(),
        result.ScenePath.parent_path());

    Assimp::Importer importer;
    FrameworkAssimp::ConfigureGeometryImporter(importer);
    const std::string sourcePathUtf8 = PathToUtf8(result.ScenePath);
    const aiScene* sourceScene = importer.ReadFile(
        sourcePathUtf8.c_str(),
        FrameworkAssimp::GeometryImportFlags);
    if (sourceScene == nullptr)
    {
        throw std::runtime_error(
            "Assimp failed to import FBX scene '" + sourcePathUtf8 + "': " + importer.GetErrorString());
    }
    if ((sourceScene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0 || sourceScene->mRootNode == nullptr)
    {
        throw std::runtime_error("Assimp returned an incomplete FBX scene: " + sourcePathUtf8);
    }

    ImportContext context{
        *sourceScene,
        result.ScenePath,
        result.SceneData,
        result.Diagnostics
    };
    ImportMaterials(context);
    SceneBounds bounds;
    ImportNode(
        context,
        *sourceScene->mRootNode,
        SceneNode::InvalidNodeIndex,
        XMMatrixIdentity(),
        std::string(),
        0,
        bounds);
    ImportCameras(context);
    ImportLights(context);

    if (!result.SceneData.HasCamera() && options.GenerateFallbackCamera)
    {
        GenerateFallbackCamera(result.SceneData, bounds);
        if (result.SceneData.HasCamera())
        {
            result.Diagnostics.emplace_back(
                "FBX has no camera; generated a framing camera from renderable scene bounds.");
        }
    }
    if (options.RequireCamera && !result.SceneData.HasCamera())
    {
        throw std::runtime_error("FBX scene has no usable camera: " + sourcePathUtf8);
    }
    if (options.RequireRenderableObject && result.SceneData.GetObjects().empty())
    {
        throw std::runtime_error("FBX scene has no supported renderable objects: " + sourcePathUtf8);
    }

    std::ostringstream summary;
    summary << "Imported FBX scene: nodes=" << result.SceneData.GetNodes().size()
            << ", objects=" << result.SceneData.GetObjects().size()
            << ", materials=" << result.SceneData.GetMaterials().size()
            << ", cameras=" << sourceScene->mNumCameras
            << ", directionalLights=" << result.SceneData.GetDirectionalLights().size()
            << ", pointLights=" << result.SceneData.GetPointLights().size()
            << ", spotLights=" << result.SceneData.GetSpotLights().size()
            << ", areaLights=" << result.SceneData.GetAreaLights().size()
            << ", embeddedTextures=" << context.EmbeddedTextureCount
            << ", missingTextures=" << context.MissingTextureCount;
    result.Diagnostics.push_back(summary.str());
    return result;
}
//Modify End
