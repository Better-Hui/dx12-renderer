//Modify Begin:2026-07-27 by BestHui
#ifndef FRAMEWORK_SVGF_COMMON_HLSLI
#define FRAMEWORK_SVGF_COMMON_HLSLI

float3 DecodeSVGFNormal(float3 encoded)
{
    return normalize(encoded * 2.0f - 1.0f);
}

float SVGFLuminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

float3 SVGFToneMap(float3 color)
{
    color = color / (color + 1.0f);
    return pow(saturate(color), 1.0f / 2.2f);
}

bool SVGFIsValidDepth(float depth)
{
    return depth < 1.0f;
}

float SVGFNormalWeight(float3 normalCenter, float3 normalSample, float phiNormal)
{
    return pow(saturate(dot(normalCenter, normalSample)), max(phiNormal, 0.001f));
}

float SVGFDepthWeight(float depthCenter, float depthSample, float3 positionCenter, float3 positionSample, float phiDepth)
{
    const float positionDistance = length(positionCenter - positionSample);
    const float depthDistance = abs(depthCenter - depthSample);
    const float scale = max(0.0001f, phiDepth * max(depthCenter, 0.001f));
    return exp(-(positionDistance + depthDistance) / scale);
}

float SVGFColorWeight(float3 colorCenter, float3 colorSample, float variance, float phiColor)
{
    const float lumaCenter = SVGFLuminance(colorCenter);
    const float lumaSample = SVGFLuminance(colorSample);
    const float sigma = max(0.0001f, phiColor * sqrt(max(variance, 0.0f)));
    return exp(-abs(lumaCenter - lumaSample) / sigma);
}

#endif
//Modify End
