//Modify Begin:2026-07-30 by BestHui
#pragma once

#include <Framework/Scene/Scene.h>
#include <Framework/Unity/UnitySceneParser.h>

#include <filesystem>
#include <string>
#include <vector>

struct UnitySceneImportOptions
{
    UnitySceneParseOptions ParseOptions;
    bool RequireCamera = true;
    bool RequireRenderableObject = true;
};

struct UnitySceneImportResult
{
    std::filesystem::path ScenePath;
    Scene SceneData;
    std::vector<std::string> Diagnostics;

    size_t GetRenderableObjectCount() const { return SceneData.GetObjects().size(); }
};

class UnitySceneImporter final
{
public:
    static UnitySceneImportResult ImportFromFile(
        const std::filesystem::path& scenePath,
        const UnitySceneImportOptions& options = {});

    static void WriteCameraToSourceFile(
        const std::filesystem::path& scenePath,
        const SceneCamera& camera);
};
//Modify End
