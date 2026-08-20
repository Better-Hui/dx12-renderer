#include "ReSTIRGI/ReSTIRGISceneContract.hlsli"
#include "ReSTIRGI/ReSTIRGI.hlsli"
#include "ReSTIRGI/ReSTIRGIConstants.hlsli"
#include <Common/ActivePixelList.hlsli>

//Modify Begin:2026-08-19 by Hui
Texture2D<uint4> ReSTIRGIHistoryCreation : register(t12, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
Texture2D<uint4> ReSTIRGIHistoryHit : register(t13, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
Texture2D<uint4> ReSTIRGIHistoryLight : register(t14, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);

[numthreads(
    FRAMEWORK_RAY_TRACED_PIXEL_THREAD_GROUP_SIZE_X,
    FRAMEWORK_RAY_TRACED_PIXEL_THREAD_GROUP_SIZE_Y,
    1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixel;
    if (!FrameworkResolveRayTracedPixel(
        dispatchThreadId,
        ReSTIRGI_Width,
        ReSTIRGI_Height,
        pixel))
    {
        return;
    }

    const ReSTIRGI_Surface surface = ReSTIRGI_LoadSurface(pixel);
    const ReSTIRGIReservoir reservoir = ReSTIRGIReadReservoir(
        ReSTIRGIHistoryCreation,
        ReSTIRGIHistoryHit,
        ReSTIRGIHistoryLight,
        pixel);
    if (!surface.Valid || !ReSTIRGIIsValid(reservoir) ||
        (!ReSTIRGIHasCreationVisibility(reservoir) && !ReSTIRGI_TestVisibility(surface, reservoir)))
    {
        ReSTIRGI_IndirectLighting[pixel] = 0.0f;
        return;
    }
    ReSTIRGI_IndirectLighting[pixel] = float4(
        max(0.0f, ReSTIRGI_EvaluateContribution(surface, reservoir) * reservoir.AverageWeight),
        0.0f);
}
//Modify End
