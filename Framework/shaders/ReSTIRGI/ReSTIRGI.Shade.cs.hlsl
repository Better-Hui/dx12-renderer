#include "ReSTIRGI/ReSTIRGISceneContract.hlsli"
#include "ReSTIRGI/ReSTIRGI.hlsli"
#include "ReSTIRGI/ReSTIRGIConstants.hlsli"

//Modify Begin:2026-08-10 by Hui
Texture2D<uint4> ReSTIRGIHistoryCreation : register(t12, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
Texture2D<uint4> ReSTIRGIHistoryHit : register(t13, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
Texture2D<uint4> ReSTIRGIHistoryLight : register(t14, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    if (pixel.x >= ReSTIRGI_Width || pixel.y >= ReSTIRGI_Height)
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
//Modify Begin:2026-07-30 by Hui
    ReSTIRGI_IndirectLighting[pixel] = float4(
        max(0.0f, ReSTIRGI_EvaluateContribution(surface, reservoir) * reservoir.AverageWeight),
        0.0f);
//Modify End
}
//Modify End
