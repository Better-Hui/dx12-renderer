#pragma once

//Modify Begin:2026-09-15 by Hui
#include <Scene/SceneResourceBuilders.h>

#include <cstdint>
#include <vector>

class CommandList;
class Camera;

struct StressTestSceneData
{
    uint32_t MaterialIndex = 0;
    std::vector<RaytracingDemoSceneObject> Objects;
};

class SceneStressTestFactory final
{
public:
    static MeshPrototype CreateSpherePrototype(float diameter, size_t tessellation);
    static StressTestSceneData Create(
        CommandList& commandList,
        SceneTextureMaterialResources& textureMaterialResources,
        SceneGeometryResources& geometryResources,
        uint32_t whiteTextureIndex,
        const Camera& camera);
};
//Modify End
