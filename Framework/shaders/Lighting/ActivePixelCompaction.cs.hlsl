//Modify Begin:2026-08-20 by Hui
Texture2D<float> DepthTexture;
RWStructuredBuffer<uint> ActivePixelIndices;
RWByteAddressBuffer ActivePixelCount;

cbuffer ActivePixelCompactionConstants
{
    uint ActivePixelWidth;
    uint ActivePixelHeight;
    uint ActivePixelPadding0;
    uint ActivePixelPadding1;
}

[numthreads(8, 8, 1)]
void main(
    uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    const bool isActive =
        pixel.x < ActivePixelWidth &&
        pixel.y < ActivePixelHeight &&
        DepthTexture.Load(int3(pixel, 0)) < 1.0f;

    if (isActive)
    {
        uint outputIndex = 0u;
        ActivePixelCount.InterlockedAdd(0u, 1u, outputIndex);
        ActivePixelIndices[outputIndex] = pixel.y * ActivePixelWidth + pixel.x;
    }
}
//Modify End
