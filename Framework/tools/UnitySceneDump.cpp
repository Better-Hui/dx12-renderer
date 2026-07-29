//Modify Begin:2026-07-29 by BestHui
#include <Framework/UnitySceneParser.h>

#include <iostream>

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
        std::cout << "Objects: " << scene.Objects.size() << "\n";
        for (const UnitySceneObject& object : scene.Objects)
        {
            std::cout << "  Object " << object.Name << " active=" << (object.Active ? "true" : "false")
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
            if (!material.Reference.AssetPath.empty())
            {
                std::cout << " path=" << material.Reference.AssetPath.string();
            }
            std::cout << "\n";
        }

        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "UnitySceneDump failed: " << exception.what() << "\n";
        return 2;
    }
}
//Modify End
