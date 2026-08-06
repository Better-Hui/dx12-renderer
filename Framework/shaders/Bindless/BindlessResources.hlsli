//Modify Begin:2026-08-06 by BestHui
#ifndef FRAMEWORK_BINDLESS_RESOURCES_HLSLI
#define FRAMEWORK_BINDLESS_RESOURCES_HLSLI

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

uint LoadBindlessIndex(uint descriptorIndex, uint indexNumber)
{
    Buffer<uint> indices = ResourceDescriptorHeap[NonUniformResourceIndex(descriptorIndex)];
    return indices[indexNumber];
}

#endif
//Modify End
