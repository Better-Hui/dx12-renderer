//Modify Begin:2026-07-30 by BestHui
#ifndef RAYTRACING_DEMO_SCENE_BINDLESS_HLSLI
#define RAYTRACING_DEMO_SCENE_BINDLESS_HLSLI

#include "SceneGeometry.hlsli"

//Modify Begin:2026-07-30 by BestHui
Texture2D<float4> SceneTextures[] : register(t31, space0);
//Modify End

float4 SampleBindlessTexture2D(uint descriptorIndex, SamplerState textureSampler, float2 uv)
{
//Modify Begin:2026-07-30 by BestHui
    Texture2D<float4> texture = SceneTextures[NonUniformResourceIndex(descriptorIndex)];
//Modify End
    return texture.Sample(textureSampler, uv);
}

float4 SampleBindlessTexture2DLevel(uint descriptorIndex, SamplerState textureSampler, float2 uv, float mipLevel)
{
//Modify Begin:2026-07-30 by BestHui
    Texture2D<float4> texture = SceneTextures[NonUniformResourceIndex(descriptorIndex)];
//Modify End
    return texture.SampleLevel(textureSampler, uv, mipLevel);
}

VertexAttributes LoadBindlessVertex(uint descriptorIndex, uint vertexIndex)
{
    StructuredBuffer<VertexAttributes> vertices = ResourceDescriptorHeap[NonUniformResourceIndex(descriptorIndex)];
    return vertices[vertexIndex];
}

uint LoadBindlessIndex(uint descriptorIndex, uint indexNumber)
{
    Buffer<uint> indices = ResourceDescriptorHeap[NonUniformResourceIndex(descriptorIndex)];
    return indices[indexNumber];
}

#endif
//Modify End
