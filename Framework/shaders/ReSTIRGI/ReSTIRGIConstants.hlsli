#ifndef FRAMEWORK_RESTIR_GI_CONSTANTS_HLSLI
#define FRAMEWORK_RESTIR_GI_CONSTANTS_HLSLI

//Modify Begin:2026-08-11 by Hui
#ifndef RESTIR_GI_USE_TEMPORAL_REUSE
#define RESTIR_GI_USE_TEMPORAL_REUSE 1
#endif

#ifndef RESTIR_GI_USE_RAY_TRACED_SPATIAL_BIAS_CORRECTION
#define RESTIR_GI_USE_RAY_TRACED_SPATIAL_BIAS_CORRECTION 0
#endif

#ifndef RESTIR_GI_USE_TEMPORAL_JACOBIAN
#define RESTIR_GI_USE_TEMPORAL_JACOBIAN 1
#endif

#ifndef RESTIR_GI_MAX_PATH_BOUNCES
#define RESTIR_GI_MAX_PATH_BOUNCES 3
#endif
//Modify End

//Modify Begin:2026-08-10 by Hui
cbuffer ReSTIRGIConstants : register(b1)
{
    uint ReSTIRGI_Width;
    uint ReSTIRGI_Height;
    uint ReSTIRGI_FrameIndex;
    uint ReSTIRGI_HistoryValid;

//Modify Begin:2026-07-30 by Hui
    uint ReSTIRGI_InitialCandidateCount;
//Modify End
    uint ReSTIRGI_TemporalMaxHistoryLength;
    uint ReSTIRGI_SpatialMaxHistoryLength;
    uint ReSTIRGI_MaxSampleAge;
    uint ReSTIRGI_SpatialNeighborCount;
//Modify Begin:2026-07-30 by Hui
    uint ReSTIRGI_PaddingUint;
    uint ReSTIRGI_PaddingUint1;
    uint ReSTIRGI_PaddingUint2;
//Modify End

    float ReSTIRGI_TemporalNormalSimilarityThreshold;
    float ReSTIRGI_TemporalPositionSimilarityThreshold;
    float ReSTIRGI_SpatialNormalSimilarityThreshold;
    float ReSTIRGI_SpatialPositionSimilarityThreshold;

    float ReSTIRGI_SpatialSamplingRadius;
    float ReSTIRGI_MaxJacobian;
    float ReSTIRGI_MaxSpatialWeight;
//Modify Begin:2026-07-30 by Hui
    float ReSTIRGI_PaddingFloat;
//Modify End
};
//Modify End

#endif
