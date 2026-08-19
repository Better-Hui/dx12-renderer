//Modify Begin:2026-08-19 by Hui
Texture2D<float> DepthTexture;
RWStructuredBuffer<uint> ActiveRayPixelIndices;
RWByteAddressBuffer ActiveRayPixelCount;

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint width = 0u;
    uint height = 0u;
    DepthTexture.GetDimensions(width, height);
    const uint2 pixel = dispatchThreadId.xy;
    if (pixel.x >= width || pixel.y >= height || DepthTexture.Load(int3(pixel, 0)) >= 1.0f)
    {
        return;
    }

    uint outputIndex = 0u;
    ActiveRayPixelCount.InterlockedAdd(0u, 1u, outputIndex);
    ActiveRayPixelIndices[outputIndex] = pixel.y * width + pixel.x;
}
//Modify End
