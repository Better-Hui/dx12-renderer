//Modify Begin:2026-07-29 by BestHui
#include <Framework/UnitySceneParser.h>
//Modify Begin:2026-07-29 by BestHui
#include <Framework/Mesh.h>
#include <Framework/ModelLoader.h>
//Modify End

#include <algorithm>
#include <cfloat>
#include <iostream>
#include <set>

namespace
{
    const char* ToString(const UnityLightType type)
    {
        switch (type)
        {
        case UnityLightType::Spot:
            return "Spot";
        case UnityLightType::Directional:
            return "Directional";
        case UnityLightType::Point:
            return "Point";
        case UnityLightType::Area:
            return "Area";
        default:
            return "Unknown";
        }
    }

    void PrintVector(const UnityVector3& value)
    {
        std::cout << value.X << ", " << value.Y << ", " << value.Z;
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
        const UnitySceneData scene = UnitySceneParser::ParseFromFile(argv[1]);
        std::cout << "Scene: " << scene.ScenePath.string() << "\n";
        std::cout << "ProjectRoot: " << scene.ProjectRoot.string() << "\n";
//Modify Begin:2026-07-30 by BestHui
        std::cout << "RenderSettings: ambientSky=("
            << scene.RenderSettings.AmbientSkyColor.R << ", "
            << scene.RenderSettings.AmbientSkyColor.G << ", "
            << scene.RenderSettings.AmbientSkyColor.B << ") intensity="
            << scene.RenderSettings.AmbientIntensity << " mode="
            << scene.RenderSettings.AmbientMode << " skyboxFileId="
            << scene.RenderSettings.SkyboxMaterial.FileId << " skyboxGuid="
            << scene.RenderSettings.SkyboxMaterial.Guid << "\n";
//Modify End
        std::cout << "Objects: " << scene.Objects.size() << "\n";
        for (const UnitySceneObject& object : scene.Objects)
        {
            std::cout << "  Object " << object.Name << " active=" << (object.Active ? "true" : "false")
                << " renderer=" << (object.RendererEnabled ? "true" : "false")
                << " worldPos=(";
            PrintVector(object.Transform.WorldPosition);
            std::cout << ") materials=" << object.Materials.size();
            if (!object.Mesh.Guid.empty() || object.Mesh.FileId != 0)
            {
                std::cout << " meshFileId=" << object.Mesh.FileId;
                if (!object.Mesh.Guid.empty())
                {
                    std::cout << " meshGuid=" << object.Mesh.Guid;
                }
            }
            std::cout << "\n";
        }
        std::cout << "Cameras: " << scene.Cameras.size() << "\n";
        for (const UnityCameraInfo& camera : scene.Cameras)
        {
            std::cout << "  Camera " << camera.Name << " enabled=" << (camera.Enabled ? "true" : "false")
                << " fov=" << camera.FieldOfView << " worldPos=(";
            PrintVector(camera.Transform.WorldPosition);
            std::cout << ")\n";
        }

        std::cout << "Lights: " << scene.Lights.size() << "\n";
        for (const UnityLightInfo& light : scene.Lights)
        {
            std::cout << "  Light " << light.Name << " enabled=" << (light.Enabled ? "true" : "false")
                << " type=" << ToString(light.Type)
                << " intensity=" << light.Intensity << " range=" << light.Range << " worldPos=(";
            PrintVector(light.Transform.WorldPosition);
            std::cout << ")\n";
        }

        std::cout << "Materials: " << scene.Materials.size() << "\n";
        for (const UnityMaterialInfo& material : scene.Materials)
        {
            std::cout << "  Material " << material.Name << " guid=" << material.Reference.Guid;
            std::cout << " pbr=" << (material.IsPbrMaterial ? "true" : "false")
                << " base=(" << material.BaseColor.R << ", " << material.BaseColor.G << ", " << material.BaseColor.B << ")"
                << " metallic=" << material.Metallic
                << " smoothness=" << material.Smoothness;
            if (!material.Reference.AssetPath.empty())
            {
                std::cout << " path=" << material.Reference.AssetPath.string();
            }
            std::cout << "\n";
        }

//Modify Begin:2026-07-29 by BestHui
        std::set<std::string> meshAssetPaths;
        for (const UnitySceneObject& object : scene.Objects)
        {
            if (object.Mesh.AssetPath.empty())
            {
                continue;
            }
            meshAssetPaths.insert(object.Mesh.AssetPath.string());
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
