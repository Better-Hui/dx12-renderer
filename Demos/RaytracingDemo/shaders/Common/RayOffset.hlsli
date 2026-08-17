#ifndef RAYTRACING_DEMO_RAY_OFFSET_HLSLI
#define RAYTRACING_DEMO_RAY_OFFSET_HLSLI

#include "PathTracingConstants.hlsli"

float NextFloatUp(float value)
{
    const uint bitsValue = asuint(value);
    if (bitsValue == 0x7f800000u)
    {
        return value;
    }

    if (value == -0.0f)
    {
        value = 0.0f;
    }

    uint bits = asuint(value);
    bits += value >= 0.0f ? 1u : uint(-1);
    return asfloat(bits);
}

float NextFloatDown(float value)
{
    const uint bitsValue = asuint(value);
    if (bitsValue == 0xff800000u)
    {
        return value;
    }

    if (value == 0.0f)
    {
        value = -0.0f;
    }

    uint bits = asuint(value);
    bits += value > 0.0f ? uint(-1) : 1u;
    return asfloat(bits);
}

float3 NextFloatAwayFrom(float3 value, float3 offset)
{
    float3 result = value;
    result.x = offset.x > 0.0f ? NextFloatUp(result.x) : (offset.x < 0.0f ? NextFloatDown(result.x) : result.x);
    result.y = offset.y > 0.0f ? NextFloatUp(result.y) : (offset.y < 0.0f ? NextFloatDown(result.y) : result.y);
    result.z = offset.z > 0.0f ? NextFloatUp(result.z) : (offset.z < 0.0f ? NextFloatDown(result.z) : result.z);
    return result;
}

float3 ComputePositionError(float3 positionWs)
{
//Modify Begin:2026-07-30 by Hui
    return max(Gamma(7u) * abs(positionWs), float3(0.0001f, 0.0001f, 0.0001f));
//Modify End
}

float3 ComputeTrianglePositionError(float3 p0Ws, float3 p1Ws, float3 p2Ws, float3 barycentrics)
{
    const float3 interpolatedAbsPosition =
        abs(p0Ws) * barycentrics.x +
        abs(p1Ws) * barycentrics.y +
        abs(p2Ws) * barycentrics.z;
//Modify Begin:2026-07-30 by Hui
    return max(Gamma(7u) * interpolatedAbsPosition, float3(0.0001f, 0.0001f, 0.0001f));
//Modify End
}

float3 OffsetRayOrigin(float3 positionWs, float3 positionError, float3 normalWs, float3 rayDirectionWs)
{
    const float3 normal = normalize(normalWs);
    const float distance = dot(abs(normal), abs(positionError));
    float3 offset = distance * normal;
    if (dot(rayDirectionWs, normal) < 0.0f)
    {
        offset = -offset;
    }

    const float3 offsetPosition = positionWs + offset;
    return NextFloatAwayFrom(offsetPosition, offset);
}

#endif
