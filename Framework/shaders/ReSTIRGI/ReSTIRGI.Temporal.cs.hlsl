#include "ReSTIRGI/ReSTIRGISceneContract.hlsli"
#include "ReSTIRGI/ReSTIRGI.hlsli"
#include "ReSTIRGI/ReSTIRGIConstants.hlsli"
//Modify Begin:2026-07-30 by BestHui
#include <Common/Noise.hlsli>
//Modify End

//Modify Begin:2026-08-10 by BestHui
Texture2D<uint4> ReSTIRGIInitialCreation : register(t12, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
Texture2D<uint4> ReSTIRGIInitialHit : register(t13, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
Texture2D<uint4> ReSTIRGIInitialLight : register(t14, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
Texture2D<float2> MotionVectorTexture : register(t15, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
Texture2D<uint4> ReSTIRGIHistoryCreation : register(t16, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
Texture2D<uint4> ReSTIRGIHistoryHit : register(t17, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
Texture2D<uint4> ReSTIRGIHistoryLight : register(t18, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
RWTexture2D<uint4> ReSTIRGITemporalCreation : register(u2);
RWTexture2D<uint4> ReSTIRGITemporalHit : register(u3);
RWTexture2D<uint4> ReSTIRGITemporalLight : register(u4);

bool ReSTIRGIIsTemporallyCompatible(
    const ReSTIRGI_Surface surface,
    const ReSTIRGIReservoir history)
{
    return ReSTIRGIHasCandidates(history) &&
        ReSTIRGIGetAge(history) <= ReSTIRGI_MaxSampleAge &&
        dot(surface.NormalWs, history.CreationNormal) >= ReSTIRGI_TemporalNormalSimilarityThreshold &&
        length(surface.PositionWs - history.CreationPosition) <= ReSTIRGI_TemporalPositionSimilarityThreshold;
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    if (pixel.x >= ReSTIRGI_Width || pixel.y >= ReSTIRGI_Height)
    {
        return;
    }

    ReSTIRGIReservoir result = ReSTIRGIEmptyReservoir();
    const ReSTIRGI_Surface surface = ReSTIRGI_LoadSurface(pixel);
    if (surface.Valid)
    {
        const ReSTIRGIReservoir initial = ReSTIRGIReadReservoir(
            ReSTIRGIInitialCreation,
            ReSTIRGIInitialHit,
            ReSTIRGIInitialLight,
            pixel);
        float weightSum = 0.0f;
        const float initialTarget = ReSTIRGIIsValid(initial)
            ? max(0.0f, ReSTIRGI_Luminance(ReSTIRGI_EvaluateContribution(surface, initial)))
            : 0.0f;
//Modify Begin:2026-07-30 by BestHui
        const bool selectedInitial = ReSTIRGIUpdateReservoir(
            result,
            initial,
            initial.AverageWeight * float(initial.M) * initialTarget,
            FrameworkInterleavedGradientNoise2D(pixel, ReSTIRGI_FrameIndex, 0x47524932u).x,
            weightSum);
        bool creationVisibilityKnown = selectedInitial && ReSTIRGIHasCreationVisibility(initial);
//Modify End

        if (ReSTIRGI_TemporalResamplingEnabled != 0u && ReSTIRGI_HistoryValid != 0u)
        {
            const float2 motion = ReSTIRGI_LoadMotionVector(pixel) * float2(ReSTIRGI_Width, ReSTIRGI_Height);
            const int2 reprojectedPixel = int2(round(float2(pixel) + motion));
            if (all(reprojectedPixel >= 0) &&
                reprojectedPixel.x < int(ReSTIRGI_Width) &&
                reprojectedPixel.y < int(ReSTIRGI_Height))
            {
                ReSTIRGIReservoir history = ReSTIRGIReadReservoir(
                    ReSTIRGIHistoryCreation,
                    ReSTIRGIHistoryHit,
                    ReSTIRGIHistoryLight,
                    uint2(reprojectedPixel));
                if (ReSTIRGIIsTemporallyCompatible(surface, history))
                {
                    history.M = min(history.M, ReSTIRGI_TemporalMaxHistoryLength);
                    float historyTarget = max(0.0f, ReSTIRGI_Luminance(
                        ReSTIRGI_EvaluateContribution(surface, history)));
                    if (ReSTIRGI_TemporalJacobianEnabled != 0u)
                    {
                        historyTarget *= ReSTIRGIComputeJacobian(
                            history,
                            surface.PositionWs,
                            surface.NormalWs,
                            ReSTIRGI_MaxJacobian);
                    }
//Modify Begin:2026-07-30 by BestHui
                    if (ReSTIRGIUpdateReservoir(
                        result,
                        history,
                        history.AverageWeight * float(history.M) * historyTarget,
                        FrameworkInterleavedGradientNoise2D(pixel, ReSTIRGI_FrameIndex, 0x22f6d7a1u).x,
                        weightSum))
                    {
                        creationVisibilityKnown = false;
                    }
//Modify End
                }
            }
        }

        const uint unboundedM = result.M;
        result.M = min(result.M, ReSTIRGI_TemporalMaxHistoryLength);
        ReSTIRGISetCreationSurface(result, surface.PositionWs, surface.NormalWs);
//Modify Begin:2026-07-30 by BestHui
        ReSTIRGISetCreationVisibility(result, creationVisibilityKnown);
//Modify End
        const float selectedTarget = ReSTIRGIIsValid(result)
            ? max(0.0f, ReSTIRGI_Luminance(ReSTIRGI_EvaluateContribution(surface, result)))
            : 0.0f;
        result.AverageWeight = selectedTarget > 0.0f && unboundedM > 0u
            ? weightSum / (float(unboundedM) * selectedTarget)
            : 0.0f;
        ReSTIRGISetAge(result, min(ReSTIRGIGetAge(result) + 1u, ReSTIRGI_MaxSampleAge));

    }

    ReSTIRGIWriteReservoir(
        ReSTIRGITemporalCreation,
        ReSTIRGITemporalHit,
        ReSTIRGITemporalLight,
        pixel,
        result);
}
//Modify End
