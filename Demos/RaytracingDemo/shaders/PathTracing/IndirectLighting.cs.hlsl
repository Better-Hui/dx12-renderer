#include "PathTracing.rayquery.hlsli"
#include "PathTracingShared.hlsli"

#if RAYTRACING_DEMO_COMPACTED_DISPATCH
[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint activePixelCount = ActiveRayPixelCount.Load(0u);
    if (dispatchThreadId.x >= activePixelCount)
    {
        return;
    }

    const uint pixelIndex = ActiveRayPixelIndices[dispatchThreadId.x];
    const uint2 pixel = uint2(pixelIndex % Camera_Width, pixelIndex / Camera_Width);
    WriteIndirectLightingOutput(pixel, Camera_Width, Camera_FrameIndex);
}
#else
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    if (pixel.x >= Camera_Width || pixel.y >= Camera_Height)
    {
        return;
    }

    WriteIndirectLightingOutput(pixel, Camera_Width, Camera_FrameIndex);
}
#endif
