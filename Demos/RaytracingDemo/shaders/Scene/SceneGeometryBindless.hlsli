//Modify Begin:2026-08-06 by BestHui
#ifndef RAYTRACING_DEMO_SCENE_GEOMETRY_BINDLESS_HLSLI
#define RAYTRACING_DEMO_SCENE_GEOMETRY_BINDLESS_HLSLI

#include <Bindless/BindlessResources.hlsli>
#include "SceneGeometry.hlsli"

VertexAttributes LoadBindlessVertex(uint descriptorIndex, uint vertexIndex)
{
    StructuredBuffer<VertexAttributes> vertices = ResourceDescriptorHeap[NonUniformResourceIndex(descriptorIndex)];
    return vertices[vertexIndex];
}

#endif
//Modify End
