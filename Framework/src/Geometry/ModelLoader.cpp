#include <DX12Library/DX12LibPCH.h>
#include <DX12Library/Texture.h>

#include <Framework/Geometry/ModelLoader.h>
#include <Framework/Geometry/Mesh.h>
#include <DX12Library/Helpers.h>

#include <Framework/Geometry/Model.h>
#include <Framework/Geometry/Bone.h>
#include <Framework/Geometry/Animation.h>
//Modify Begin:2026-08-21 by Hui
#include <Framework/Rendering/Texture/TextureLoader.h>
#include "AssimpImportSettings.h"
//Modify End

#include <assimp/Importer.hpp>      // C++ importer interface
#include <assimp/scene.h>           // Output data structure
#include <assimp/postprocess.h>     // Post processing flags

#include <filesystem>
#include <memory>
//Modify Begin:2026-08-21 by Hui
#include <limits>
#include <stdexcept>
//Modify End

using namespace DirectX;
namespace fs = std::filesystem;

namespace
{
    XMFLOAT3 ToXMFloat3(aiVector3D vector)
    {
        return { vector.x, vector.y, vector.z };
    }

    XMFLOAT4 ToXMFloat4Position(aiVector3D vector)
    {
        return { vector.x, vector.y, vector.z, 1.0f };
    }

    XMFLOAT4 ToXMFloat4Vector(aiVector3D vector)
    {
        return { vector.x, vector.y, vector.z, 0.0f };
    }

    XMFLOAT2 ToXMFloat2(aiVector3D vector)
    {
        return { vector.x, vector.y };
    }

    XMMATRIX ToXMATRIX(aiMatrix4x4 matrix)
    {
        // transpose to convert to the left-handed orientation
        return XMMATRIX(
            matrix.a1, matrix.b1, matrix.c1, matrix.d1,
            matrix.a2, matrix.b2, matrix.c2, matrix.d2,
            matrix.a3, matrix.b3, matrix.c3, matrix.d3,
            matrix.a4, matrix.b4, matrix.c4, matrix.d4
        );
    }

    XMVECTOR ToXMVECTOR(aiVector3D vector)
    {
        return XMVectorSet(vector.x, vector.y, vector.z, 0.0);
    }

    XMVECTOR ToXMVECTOR(aiQuaternion quaternion)
    {
        return XMVectorSet(quaternion.x, quaternion.y, quaternion.z, quaternion.w);
    }

    constexpr auto AI_FLAGS = aiProcess_ConvertToLeftHanded;

    template <typename TKey>
    void BuildKeyFrames(unsigned int numKeys, const TKey* keys, std::vector<Animation::KeyFrame>& result)
    {
        for (unsigned int keyIndex = 0; keyIndex < numKeys; ++keyIndex)
        {
            Animation::KeyFrame keyFrame;
            auto key = keys[keyIndex];
            keyFrame.Value = ToXMVECTOR(key.mValue);
            keyFrame.NormalizedTime = static_cast<float>(key.mTime);
            result.push_back(keyFrame);
        }
    }
}

//Modify Begin:2026-08-21 by Hui
std::vector<MeshPrototype> ModelLoader::LoadAsMeshPrototypes(
    const std::string& path,
    const bool flipNormals) const
{
    Assimp::Importer importer;
    FrameworkAssimp::ConfigureGeometryImporter(importer);
    const aiScene* scene = importer.ReadFile(path.c_str(), FrameworkAssimp::GeometryImportFlags);

    if (scene == nullptr)
    {
        throw std::runtime_error(
            "Assimp failed to import mesh file '" + path + "': " + importer.GetErrorString());
    }
    if ((scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0)
    {
        throw std::runtime_error("Assimp returned an incomplete mesh scene for '" + path + "'.");
    }

    std::vector<MeshPrototype> outputMeshes;
    outputMeshes.reserve(scene->mNumMeshes);

    for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
    {
        const auto mesh = scene->mMeshes[meshIndex];
        if (mesh == nullptr)
        {
            throw std::runtime_error(
                "Assimp returned a null mesh at index " + std::to_string(meshIndex) +
                " for '" + path + "'.");
        }
        if ((mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE) == 0)
        {
            continue;
        }
        if (!mesh->HasPositions() || mesh->mNumVertices == 0 || mesh->mNumFaces == 0)
        {
            throw std::runtime_error(
                "Renderable mesh #" + std::to_string(meshIndex) +
                " has no positions or triangle faces in '" + path + "'.");
        }
        if (mesh->mNumVertices > static_cast<uint64_t>((std::numeric_limits<uint16_t>::max)()) + 1ull)
        {
            throw std::runtime_error(
                "Assimp did not split mesh #" + std::to_string(meshIndex) +
                " to the 16-bit index limit for '" + path + "'.");
        }

        VertexCollectionType outputVertices;
        outputVertices.reserve(mesh->mNumVertices);

        for (unsigned int vertexIndex = 0; vertexIndex < mesh->mNumVertices; ++vertexIndex)
        {
            VertexAttributes vertexAttributes;

            if (mesh->HasPositions())
            {
                vertexAttributes.Position = ToXMFloat4Position(mesh->mVertices[vertexIndex]);
            }

            if (mesh->HasNormals())
            {
                vertexAttributes.Normal = ToXMFloat4Vector(mesh->mNormals[vertexIndex] * (flipNormals ? -1.0f : 1.0f));
            }

            constexpr unsigned int uvIndex = 0;
            if (mesh->HasTextureCoords(uvIndex))
            {
                vertexAttributes.Uv = ToXMFloat4Vector(mesh->mTextureCoords[uvIndex][vertexIndex]);
            }

            if (mesh->HasTangentsAndBitangents())
            {
                vertexAttributes.Tangent = ToXMFloat4Vector(mesh->mTangents[vertexIndex]);
                vertexAttributes.Bitangent = ToXMFloat4Vector(mesh->mBitangents[vertexIndex]);
            }

            outputVertices.push_back(vertexAttributes);
        }

        IndexCollectionType outputIndices;
        constexpr unsigned int indicesInTriangle = 3;
        outputIndices.reserve(static_cast<size_t>(mesh->mNumFaces) * indicesInTriangle);

        for (unsigned int faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex)
        {
            const aiFace& face = mesh->mFaces[faceIndex];
            if (face.mNumIndices != indicesInTriangle || face.mIndices == nullptr)
            {
                throw std::runtime_error(
                    "Mesh #" + std::to_string(meshIndex) + " face #" +
                    std::to_string(faceIndex) + " is not a valid triangle in '" + path + "'.");
            }
            for (unsigned int corner = 0; corner < indicesInTriangle; ++corner)
            {
                const unsigned int vertexIndex = face.mIndices[corner];
                if (vertexIndex >= mesh->mNumVertices ||
                    vertexIndex > (std::numeric_limits<uint16_t>::max)())
                {
                    throw std::runtime_error(
                        "Mesh #" + std::to_string(meshIndex) + " face #" +
                        std::to_string(faceIndex) + " has an out-of-range index in '" + path + "'.");
                }
                outputIndices.push_back(static_cast<uint16_t>(vertexIndex));
            }
        }

        MeshPrototype& meshPrototype = outputMeshes.emplace_back(std::move(outputVertices), std::move(outputIndices), true, false);
        meshPrototype.m_Name = mesh->mName.length > 0
            ? mesh->mName.C_Str()
            : "Mesh_" + std::to_string(meshIndex);
        meshPrototype.m_SourceMeshIndex = meshIndex;

        if (mesh->HasBones())
        {
            std::vector<Bone> bones;
            bones.reserve(mesh->mNumBones);

            SkinningVertexCollectionType outputSkinningVertices;
            outputSkinningVertices.resize(mesh->mNumVertices);

            for (unsigned int boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex)
            {
                auto meshBone = mesh->mBones[boneIndex];

                Bone bone;
                bone.Offset = ToXMATRIX(meshBone->mOffsetMatrix);
                bone.Name = meshBone->mName.C_Str();
                bone.IsDirty = true;

                bones.push_back(bone);

                for (unsigned int weightIndex = 0; weightIndex < meshBone->mNumWeights; ++weightIndex)
                {
                    const auto& weight = meshBone->mWeights[weightIndex];
                    if (weight.mVertexId >= outputSkinningVertices.size())
                    {
                        throw std::runtime_error(
                            "Mesh #" + std::to_string(meshIndex) +
                            " has a bone weight with an invalid vertex index in '" + path + "'.");
                    }
                    auto& vertexAttributes = outputSkinningVertices[weight.mVertexId];

                    bool inserted = false;
                    for (uint32_t vertexAttributeId = 0; vertexAttributeId < SkinningVertexAttributes::BONES_PER_VERTEX; vertexAttributeId++)
                    {
                        if (vertexAttributes.Weights[vertexAttributeId] == 0.0)
                        {
                            vertexAttributes.BoneIds[vertexAttributeId] = boneIndex;
                            vertexAttributes.Weights[vertexAttributeId] = weight.mWeight;
                            inserted = true;
                            break;
                        }
                    }
                    if (!inserted)
                    {
                        throw std::runtime_error(
                            "Assimp did not limit bone influences to four for mesh #" +
                            std::to_string(meshIndex) + " in '" + path + "'.");
                    }
                }
            }

            for (auto& vertexAttributes : outputSkinningVertices)
            {
                vertexAttributes.NormalizeWeights();
            }

            Armature& armature = meshPrototype.m_Armature;
            armature.SetBones(bones);

            for (unsigned int boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex)
            {
                std::vector<size_t> childrenIndices;

                const auto meshBoneNode = mesh->mBones[boneIndex]->mNode;
                if (meshBoneNode == nullptr)
                {
                    throw std::runtime_error(
                        "Mesh #" + std::to_string(meshIndex) +
                        " has a bone without an armature node in '" + path + "'.");
                }
                auto& bone = armature.GetBone(boneIndex);
                bone.LocalTransform = ToXMATRIX(meshBoneNode->mTransformation);

                for (unsigned int childIndex = 0; childIndex < meshBoneNode->mNumChildren; ++childIndex)
                {
                    const auto child = meshBoneNode->mChildren[childIndex];
                    auto childName = std::string(child->mName.C_Str());
                    if (!armature.HasBone(childName))
                    {
                        continue;
                    }

                    size_t armatureBoneIndex = armature.GetBoneIndex(childName);
                    childrenIndices.push_back(armatureBoneIndex);
                }

                armature.SetBoneChildren(boneIndex, childrenIndices);
            }

            meshPrototype.m_SkinningVertexAttributes = std::move(outputSkinningVertices);
        }
    }

    if (outputMeshes.empty())
    {
        throw std::runtime_error("Mesh file contains no triangle meshes: '" + path + "'.");
    }
    return outputMeshes;
}
//Modify End

std::shared_ptr<Model> ModelLoader::Load(CommandList& commandList, const std::string& path, bool flipNormals) const
{
    const auto meshPrototypes = LoadAsMeshPrototypes(path, flipNormals);
    return Load(commandList, meshPrototypes);
}

std::shared_ptr<Model> ModelLoader::Load(CommandList& commandList, const std::vector<MeshPrototype>& meshPrototypes) const
{
    std::vector<std::shared_ptr<Mesh>> outputMeshes;

    for (const auto& meshPrototype : meshPrototypes)
    {
        auto outputMesh = Mesh::CreateMesh(commandList, meshPrototype);
        outputMeshes.push_back(outputMesh);
    }

    auto model = std::make_shared<Model>(outputMeshes);

    return model;
}

std::shared_ptr<Model> ModelLoader::LoadExisting(std::shared_ptr<Mesh> mesh) const
{
    auto model = std::make_shared<Model>(mesh);
    return model;
}

std::shared_ptr<Animation> ModelLoader::LoadAnimation(const std::string& path, const std::string& animationName) const
{
    Assimp::Importer importer;

    auto flags = AI_FLAGS;

    const aiScene* scene = importer.ReadFile(path.c_str(), flags);

    if (scene == nullptr)
    {
        std::string errorString = importer.GetErrorString();
        throw std::exception(errorString.c_str());
    }

    if (!scene->HasAnimations())
    {
        throw std::exception("Specified file does not contain animations");
    }

    for (unsigned int i = 0; i < scene->mNumAnimations; ++i)
    {
        auto animation = scene->mAnimations[i];
        if (strcmp(animation->mName.C_Str(), animationName.c_str()) != 0) continue;

        auto duration = static_cast<float>(animation->mDuration);
        auto ticksPerSecond = static_cast<float>(animation->mTicksPerSecond);

        std::vector<Animation::Channel> resultingChannels;

        for (unsigned int channelIndex = 0; channelIndex < animation->mNumChannels; ++channelIndex)
        {
            auto channel = animation->mChannels[channelIndex];

            Animation::Channel resultingChannel;
            resultingChannel.NodeName = channel->mNodeName.C_Str();

            BuildKeyFrames(channel->mNumPositionKeys, channel->mPositionKeys, resultingChannel.PositionKeyFrames);
            BuildKeyFrames(channel->mNumRotationKeys, channel->mRotationKeys, resultingChannel.RotationKeyFrames);
            BuildKeyFrames(channel->mNumScalingKeys, channel->mScalingKeys, resultingChannel.ScalingKeyFrames);

            resultingChannels.push_back(resultingChannel);
        }

        auto resultingAnimation = std::make_shared<Animation>(duration, ticksPerSecond, resultingChannels);
        return resultingAnimation;
    }

    throw std::exception("The requested animation was not found.");
}

std::shared_ptr<Texture> ModelLoader::LoadTexture(CommandList& commandList, const std::wstring& path, TextureUsageType usage /*= TextureUsageType::Albedo*/) const
{
//Modify Begin:2026-08-12 by Hui
    auto texture = std::make_shared<Texture>(
        TextureUsageType::Other,
        L"",
        commandList.GetDeviceContext());
//Modify End
//Modify Begin:2026-08-18 by Hui
    TextureLoader(commandList.GetDeviceContext()).Load(commandList, *texture, path, usage);
//Modify End
    return texture;
}
