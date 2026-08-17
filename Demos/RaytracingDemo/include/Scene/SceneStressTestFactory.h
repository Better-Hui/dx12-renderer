#pragma once

//Modify Begin:2026-08-06 by Hui
#include <Scene/SceneResourceBuilders.h>

#include <cstdint>
#include <vector>

class CommandList;

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
        uint32_t whiteTextureIndex);
};
//Modify End
