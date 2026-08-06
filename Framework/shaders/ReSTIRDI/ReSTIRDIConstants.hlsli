#ifndef FRAMEWORK_RESTIR_DI_CONSTANTS_HLSLI
#define FRAMEWORK_RESTIR_DI_CONSTANTS_HLSLI

//Modify Begin:2026-07-30 by BestHui
cbuffer ReSTIRDIConstants : register(b1)
{
    uint ReSTIRDI_CandidateCount;
    uint ReSTIRDI_InitialVisibilityEnabled;
    uint ReSTIRDI_TemporalResamplingEnabled;
    uint ReSTIRDI_TemporalBiasCorrectionMode;

    uint ReSTIRDI_HistoryValid;
    uint ReSTIRDI_TemporalVisibilityShortcutEnabled;
    uint ReSTIRDI_TemporalPermutationSamplingEnabled;
    uint ReSTIRDI_BoilingFilterEnabled;

    uint ReSTIRDI_TemporalMaxHistoryLength;
    uint ReSTIRDI_SpatialResamplingEnabled;
    uint ReSTIRDI_SpatialNeighborCount;
    uint ReSTIRDI_SpatialDisocclusionBoostSampleCount;

    uint ReSTIRDI_SpatialTargetHistoryLength;
    uint ReSTIRDI_SpatialBiasCorrectionMode;
    uint ReSTIRDI_SpatialMaterialSimilarityTestEnabled;
    uint ReSTIRDI_SpatialDiscountNaiveSamples;

    uint ReSTIRDI_FinalVisibilityEnabled;
    uint ReSTIRDI_FinalVisibilityReuseEnabled;
    uint ReSTIRDI_FinalVisibilityDiscardInvisibleSamples;
    uint ReSTIRDI_FinalVisibilityMaxAge;

    float ReSTIRDI_BoilingFilterStrength;
    float ReSTIRDI_TemporalNormalSimilarityThreshold;
    float ReSTIRDI_TemporalDepthSimilarityThreshold;
    float ReSTIRDI_Padding0;

    float ReSTIRDI_SpatialSamplingRadius;
    float ReSTIRDI_SpatialNormalSimilarityThreshold;
    float ReSTIRDI_SpatialDepthSimilarityThreshold;
    float ReSTIRDI_SpatialMaterialSimilarityThreshold;

    float ReSTIRDI_FinalVisibilityMaxDistance;
    float ReSTIRDI_Padding1;
    float ReSTIRDI_Padding2;
    float ReSTIRDI_Padding3;
};
//Modify End

#endif
