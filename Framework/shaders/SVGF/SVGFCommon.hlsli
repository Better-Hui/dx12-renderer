//Modify Begin:2026-10-14 by Hui
#ifndef FRAMEWORK_SVGF_COMMON_HLSLI
#define FRAMEWORK_SVGF_COMMON_HLSLI

float3 DecodeSVGFNormal(float3 encoded)
{
    const float3 n = encoded * 2.0f - 1.0f;
    return n * rsqrt(max(dot(n, n), 1e-10f));
}

float SVGFLuminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

bool SVGFIsValidDepth(float depth)
{
    return isfinite(depth) && depth >= 0.0f && depth < 1.0f;
}

float3 SVGFSanitize(float3 color)
{
    return all(isfinite(color)) ? max(color, 0.0f) : 0.0f;
}

// Combined diffuse/specular lighting has no single physical albedo. Bound
// the diffuse factor and fade to unity for metals, as in the NRD adapter.
float3 SVGFDemodulation(float4 albedo, float4 emissionMetallic)
{
    return lerp(max(saturate(albedo.rgb), 0.05f), 1.0f, saturate(emissionMetallic.a));
}

float2 SVGFSignNotZero(float2 v)
{
    return float2(v.x >= 0.0f ? 1.0f : -1.0f, v.y >= 0.0f ? 1.0f : -1.0f);
}

float2 SVGFEncodeNormal(float3 n)
{
    n /= max(abs(n.x) + abs(n.y) + abs(n.z), 1e-8f);
    return n.z >= 0.0f ? n.xy : (1.0f - abs(n.yx)) * SVGFSignNotZero(n.xy);
}

float3 SVGFDecodeNormal(float2 encoded)
{
    float3 n = float3(encoded, 1.0f - abs(encoded.x) - abs(encoded.y));
    if (n.z < 0.0f) n.xy = (1.0f - abs(n.yx)) * SVGFSignNotZero(n.xy);
    return normalize(n);
}

// Surface data: linear view Z, its pixel footprint, and octahedral world normal.
float SVGFEdgeWeight(float4 center, float4 sample, float offsetLength,
    float stepSize, float phiDepth, float phiNormal, float lumaCenter,
    float lumaSample, float phiLuminance)
{
    if (sample.x <= 0.0f) return 0.0f;
    const float normalWeight = pow(saturate(dot(SVGFDecodeNormal(center.zw),
        SVGFDecodeNormal(sample.zw))), max(phiNormal, 0.001f));
    const float depthScale = max(center.y, 1e-4f) * max(phiDepth, 0.001f)
        * stepSize * max(offsetLength, 1.0f);
    return normalWeight * exp(-abs(center.x - sample.x) / depthScale
        - abs(lumaCenter - lumaSample) / max(phiLuminance, 1e-6f));
}

#endif
//Modify End
