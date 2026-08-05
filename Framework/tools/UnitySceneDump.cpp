//Modify Begin:2026-07-29 by BestHui
#include <Framework/Scene/SceneImporter.h>
#include <Framework/Geometry/Mesh.h>
#include <Framework/Geometry/ModelLoader.h>
//Modify End

#include <algorithm>
#include <cfloat>
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
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "Usage: UnitySceneDump <scene.unity>\n";
        return 1;
    }

    try
    {
        const SceneImportResult importResult = SceneImporter::ImportFromFile(argv[1]);
        const Scene& scene = importResult.SceneData;
        std::cout << "Scene: " << scene.GetSourcePath().string() << "\n";
        std::cout << "ProjectRoot: " << scene.GetProjectRoot().string() << "\n";
        for (const std::string& diagnostic : importResult.Diagnostics)
        {
            std::cout << "Diagnostic: " << diagnostic << "\n";
        }

        std::cout << "Camera: " << scene.GetCamera().Name
            << " fov=" << scene.GetCamera().FieldOfView
            << " near=" << scene.GetCamera().NearClipPlane
            << " far=" << scene.GetCamera().FarClipPlane << "\n";

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
                    << " submesh=" << object.Mesh.SubmeshName;
            }
            std::cout << "\n";
        }

        std::cout << "Lights: directional=" << scene.GetDirectionalLights().size()
            << " point=" << scene.GetPointLights().size()
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
                << " baseMap=" << material.BaseMap.AssetPath.string() << "\n";
        }

//Modify Begin:2026-07-29 by BestHui
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
//Modify End

        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "UnitySceneDump failed: " << exception.what() << "\n";
        return 2;
    }
}
//Modify End
