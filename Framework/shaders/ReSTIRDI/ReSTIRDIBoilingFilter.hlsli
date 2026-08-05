#ifndef FRAMEWORK_RESTIR_DI_BOILING_FILTER_HLSLI
#define FRAMEWORK_RESTIR_DI_BOILING_FILTER_HLSLI

float ReSTIRDIBoilingFilterMultiplier(const float strength)
{
    return 10.0f / clamp(strength, 0.000001f, 1.0f) - 9.0f;
}

bool ReSTIRDIIsBoilingOutlier(
    const float reservoirWeight,
    const float averageNonZeroWeight,
    const float strength)
{
    return reservoirWeight > averageNonZeroWeight * ReSTIRDIBoilingFilterMultiplier(strength);
}

#endif
