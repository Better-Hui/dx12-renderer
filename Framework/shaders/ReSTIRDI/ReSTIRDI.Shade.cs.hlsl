//Modify Begin:2026-07-30 by BestHui
#include "ReSTIRDISceneContract.hlsli"
#include "ReSTIRDI/ReSTIRDI.hlsli"
#include "ReSTIRDI/ReSTIRDIConstants.hlsli"

Texture2D<uint4> ReSTIRDIFinalReservoir : register(t12, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
Texture2D<uint4> ReSTIRDIFinalReservoirState : register(t13, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
RWTexture2D<uint4> ReSTIRDICurrentReservoir : register(u3);
RWTexture2D<uint4> ReSTIRDICurrentReservoirState : register(u4);
RWTexture2D<float4> ReSTIRDICurrentPosition : register(u5);
RWTexture2D<float4> ReSTIRDICurrentNormalRoughness : register(u6);
RWTexture2D<float4> ReSTIRDICurrentDiffuseMetallic : register(u7);
RWTexture2D<float4> ReSTIRDICurrentSpecularOcclusion : register(u8);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    if (pixel.x >= ReSTIRDI_ScreenWidth || pixel.y >= ReSTIRDI_ScreenHeight)
    {
        return;
    }

    const ReSTIRDI_Surface surface = ReSTIRDI_LoadSurface(pixel);
    ReSTIRDIReservoir reservoir = ReSTIRDIUnpackReservoir(
        ReSTIRDIFinalReservoir.Load(int3(pixel, 0)), ReSTIRDIFinalReservoirState.Load(int3(pixel, 0)));
    float3 lighting = 0.0f;
    float hitDistance = 0.0f;
    if (surface.Valid && ReSTIRDIIsValid(reservoir) && ReSTIRDIGetLightIndex(reservoir) < ReSTIRDI_GetLightCount())
    {
        const ReSTIRDI_LightSample selectedSample = ReSTIRDI_SampleLight(
            ReSTIRDIGetLightIndex(reservoir), surface, ReSTIRDIGetSampleUv(reservoir));
        if (selectedSample.Valid)
        {
            float visibility = 1.0f;
#if RESTIR_DI_USE_FINAL_VISIBILITY
            bool reusedVisibility = false;
#if RESTIR_DI_USE_FINAL_VISIBILITY_REUSE
            reusedVisibility = ReSTIRDIGetReusedVisibility(
                reservoir,
                ReSTIRDI_FinalVisibilityMaxAge,
                ReSTIRDI_FinalVisibilityMaxDistance,
                visibility);
#endif

            if (!reusedVisibility)
            {
                visibility = ReSTIRDI_TestVisibility(surface, selectedSample) ? 1.0f : 0.0f;
                ReSTIRDIStoreVisibility(
                    reservoir,
                    visibility,
                    RESTIR_DI_DISCARD_INVISIBLE_FINAL_SAMPLES != 0);
            }
#endif

            lighting = selectedSample.UnshadowedContribution * visibility * ReSTIRDIGetFinalWeight(reservoir);
            hitDistance = selectedSample.Distance;
        }
        else
        {
            reservoir = ReSTIRDIEmptyReservoir();
        }
    }
    else
    {
        reservoir = ReSTIRDIEmptyReservoir();
    }

    DirectLighting[pixel] = float4(lighting, hitDistance);
    ReSTIRDICurrentReservoir[pixel] = ReSTIRDIPackReservoirCore(reservoir);
    ReSTIRDICurrentReservoirState[pixel] = ReSTIRDIPackReservoirState(reservoir);
    ReSTIRDICurrentPosition[pixel] = surface.Valid
        ? float4(surface.PositionWs, length(surface.PositionWs - ReSTIRDI_CameraPosition.xyz))
        : 0.0f;
    ReSTIRDICurrentNormalRoughness[pixel] = surface.Valid
        ? float4(normalize(surface.NormalWs), surface.Roughness)
        : 0.0f;
    ReSTIRDICurrentDiffuseMetallic[pixel] = surface.Valid
        ? float4(surface.Diffuse, surface.Metallic)
        : 0.0f;
    ReSTIRDICurrentSpecularOcclusion[pixel] = surface.Valid
        ? float4(surface.Specular, surface.AmbientOcclusion)
        : 0.0f;
}
//Modify End
