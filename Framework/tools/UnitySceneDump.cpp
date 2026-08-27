//Modify Begin:2026-08-26 by Hui
#include <Framework/Scene/SceneImporter.h>
#include <Framework/Geometry/Mesh.h>
#include <Framework/Geometry/ModelLoader.h>

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <iostream>
#include <set>

namespace
{
    const char* ToString(const SceneMeshKind kind)
    {
        switch (kind)
        {
        case SceneMeshKind::BuiltinPlane:
            return "BuiltinPlane";
        case SceneMeshKind::BuiltinCube:
            return "BuiltinCube";
        case SceneMeshKind::ExternalMesh:
            return "ExternalMesh";
        default:
            return "Unknown";
        }
    }

    void PrintFloat4(const DirectX::XMFLOAT4& value)
    {
        std::cout << value.x << ", " << value.y << ", " << value.z << ", " << value.w;
    }

    void PrintFloat3(const DirectX::XMFLOAT3& value)
    {
        std::cout << value.x << ", " << value.y << ", " << value.z;
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "Usage: UnitySceneDump <scene.{unity,json,fbx,xml}> [--allow-missing-camera]\n";
        return 1;
    }

    try
    {
        SceneImportOptions options;
        if (argc >= 3 && std::strcmp(argv[2], "--allow-missing-camera") == 0)
        {
            options.RequireCamera = false;
        }
        const SceneImportResult importResult = SceneImporter::ImportFromFile(argv[1], options);
        const Scene& scene = importResult.SceneData;
        std::cout << "Scene: " << scene.GetSourcePath().string() << "\n";
        std::cout << "ProjectRoot: " << scene.GetProjectRoot().string() << "\n";
        for (const std::string& diagnostic : importResult.Diagnostics)
        {
            std::cout << "Diagnostic: " << diagnostic << "\n";
        }

        std::cout << "Camera: " << (scene.HasCamera() ? scene.GetCamera().Name : "<none>")
            << " fov=" << scene.GetCamera().FieldOfView
            << " near=" << scene.GetCamera().NearClipPlane
            << " far=" << scene.GetCamera().FarClipPlane
            << " node=" << scene.GetCamera().SourceBinding.NodeIndex << "\n";
        if (scene.HasCamera() && scene.GetCamera().RuntimeCamera != nullptr)
        {
            const std::shared_ptr<Camera>& camera = scene.GetCamera().RuntimeCamera;
            DirectX::XMFLOAT3 position{};
            DirectX::XMFLOAT3 forward{};
            DirectX::XMStoreFloat3(&position, camera->GetTranslation());
            DirectX::XMStoreFloat3(
                &forward,
                DirectX::XMVector3Normalize(DirectX::XMVector3Rotate(
                    DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f),
                    camera->GetRotation())));
            std::cout << "CameraPose: position=(";
            PrintFloat3(position);
            std::cout << ") forward=(";
            PrintFloat3(forward);
            std::cout << ")\n";
        }

        std::cout << "Nodes: " << scene.GetNodes().size() << "\n";
        for (size_t nodeIndex = 0; nodeIndex < scene.GetNodes().size(); ++nodeIndex)
        {
            const SceneNode& node = scene.GetNodes()[nodeIndex];
            std::cout << "  Node #" << nodeIndex
                << " name=" << node.Name
                << " source=" << node.SourceId
                << " parent=" << node.ParentIndex
                << " children=" << node.Children.size() << "\n";
        }

        std::cout << "Skybox: ambient=(";
        PrintFloat4(scene.GetSkybox().AmbientColorAndIntensity);
        std::cout << ") texture=" << scene.GetSkybox().Texture.AssetPath.string() << "\n";

        std::cout << "Objects: " << scene.GetObjects().size() << "\n";
        for (const SceneObject& object : scene.GetObjects())
        {
            std::cout << "  Object " << object.Name
                << " meshKind=" << ToString(object.Mesh.Kind)
                << " material=" << object.MaterialIndex;
            if (!object.Mesh.AssetPath.empty())
            {
                std::cout << " asset=" << object.Mesh.AssetPath.string()
                    << " submesh=" << object.Mesh.SubmeshName
                    << " submeshIndex=" << object.Mesh.SubmeshIndex;
            }
            std::cout << " node=" << object.NodeIndex;
            std::cout << "\n";
        }

        std::cout << "Lights: directional=" << scene.GetDirectionalLights().size()
            << " point=" << scene.GetPointLights().size()
            << " spot=" << scene.GetSpotLights().size()
            << " area=" << scene.GetAreaLights().size() << "\n";

        std::cout << "Materials: " << scene.GetMaterials().size() << "\n";
        for (const SceneMaterial& material : scene.GetMaterials())
        {
            std::cout << "  Material " << material.Name
                << " source=" << material.SourceId
                << " pbr=" << (material.IsPbrMaterial ? "true" : "false")
                << " base=(";
            PrintFloat4(material.BaseColor);
            std::cout << ") metallic=" << material.Metallic
                << " roughness=" << material.Roughness
                << " baseMap=" << material.BaseMap.AssetPath.string()
                << " embedded=" << (material.BaseMap.EmbeddedTexture != nullptr ? "true" : "false");
            if (material.BaseMap.EmbeddedTexture != nullptr)
            {
                std::cout << " bytes=" << material.BaseMap.EmbeddedTexture->Data.size()
                    << " format=" << material.BaseMap.EmbeddedTexture->FormatHint;
            }
            std::cout << "\n";
        }

        std::set<std::string> meshAssetPaths;
        for (const SceneObject& object : scene.GetObjects())
        {
            if (!object.Mesh.AssetPath.empty())
            {
                meshAssetPaths.insert(object.Mesh.AssetPath.string());
            }
        }

        ModelLoader modelLoader;
        for (const std::string& meshAssetPath : meshAssetPaths)
        {
            const std::vector<MeshPrototype> prototypes = modelLoader.LoadAsMeshPrototypes(meshAssetPath);
            std::cout << "MeshAsset: " << meshAssetPath << " meshes=" << prototypes.size() << "\n";
            for (size_t meshIndex = 0; meshIndex < prototypes.size(); ++meshIndex)
            {
                const MeshPrototype& prototype = prototypes[meshIndex];
                DirectX::XMFLOAT3 minValue = { FLT_MAX, FLT_MAX, FLT_MAX };
                DirectX::XMFLOAT3 maxValue = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
                for (const VertexAttributes& vertex : prototype.m_Vertices)
                {
                    minValue.x = (std::min)(minValue.x, vertex.Position.x);
                    minValue.y = (std::min)(minValue.y, vertex.Position.y);
                    minValue.z = (std::min)(minValue.z, vertex.Position.z);
                    maxValue.x = (std::max)(maxValue.x, vertex.Position.x);
                    maxValue.y = (std::max)(maxValue.y, vertex.Position.y);
                    maxValue.z = (std::max)(maxValue.z, vertex.Position.z);
                }
                std::cout << "  Mesh #" << meshIndex << " name=" << prototype.m_Name
                    << " vertices=" << prototype.m_Vertices.size()
                    << " boundsMin=(" << minValue.x << ", " << minValue.y << ", " << minValue.z << ")"
                    << " boundsMax=(" << maxValue.x << ", " << maxValue.y << ", " << maxValue.z << ")\n";
            }
        }

        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Scene dump failed: " << exception.what() << "\n";
        return 2;
    }
}
//Modify End
