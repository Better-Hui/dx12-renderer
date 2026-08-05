//Modify Begin:2026-07-30 by BestHui
#ifndef RAYTRACING_DEMO_SCENE_BINDLESS_HLSLI
#define RAYTRACING_DEMO_SCENE_BINDLESS_HLSLI

#include "SceneGeometry.hlsli"

float4 SampleBindlessTexture2D(uint descriptorIndex, SamplerState textureSampler, float2 uv)
{
    Texture2D<float4> texture = ResourceDescriptorHeap[NonUniformResourceIndex(descriptorIndex)];
    return texture.Sample(textureSampler, uv);
}

float4 SampleBindlessTexture2DLevel(uint descriptorIndex, SamplerState textureSampler, float2 uv, float mipLevel)
{
    Texture2D<float4> texture = ResourceDescriptorHeap[NonUniformResourceIndex(descriptorIndex)];
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
