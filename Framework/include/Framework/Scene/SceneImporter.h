//Modify Begin:2026-08-03 by Hui
#pragma once

#include <Framework/Scene/Scene.h>
#include <Framework/Scene/SceneYamlParser.h>

#include <filesystem>
#include <string>
#include <vector>

struct SceneImportOptions
{
    UnitySceneParseOptions ParseOptions;
    bool RequireCamera = true;
    bool RequireRenderableObject = true;
};

struct SceneImportResult
{
    std::filesystem::path ScenePath;
    Scene SceneData;
    std::vector<std::string> Diagnostics;

    size_t GetRenderableObjectCount() const { return SceneData.GetObjects().size(); }
};

class SceneImporter final
{
public:
    // Imports Unity YAML scenes and Unity-style JSON scenes into the shared Scene representation.
    static SceneImportResult ImportFromFile(
        const std::filesystem::path& scenePath,
        const SceneImportOptions& options = {});

    static SceneImportResult ImportJsonFromFile(
        const std::filesystem::path& scenePath,
        const SceneImportOptions& options = {});
    static void ApplyJsonRuntimeState(
        const std::filesystem::path& statePath,
        Scene& scene);
    static void WriteJsonRuntimeState(
        const std::filesystem::path& statePath,
        const Scene& scene);

    static void WriteCameraToSourceFile(
        const std::filesystem::path& scenePath,
        const SceneCamera& camera);
};
//Modify End
