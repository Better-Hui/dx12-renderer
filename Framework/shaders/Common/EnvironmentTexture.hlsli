//Modify Begin:2026-08-06 by Hui
#ifndef FRAMEWORK_ENVIRONMENT_TEXTURE_HLSLI
#define FRAMEWORK_ENVIRONMENT_TEXTURE_HLSLI

float2 FrameworkDirectionToHorizontalCubemapStripUv(float3 direction)
{
    const float3 absoluteDirection = abs(direction);
    float2 faceUv;
    uint faceIndex;

    if (absoluteDirection.x >= absoluteDirection.y && absoluteDirection.x >= absoluteDirection.z)
    {
        if (direction.x >= 0.0f)
        {
            faceIndex = 0u;
            faceUv = float2(-direction.z, -direction.y) / absoluteDirection.x;
        }
        else
        {
            faceIndex = 1u;
            faceUv = float2(direction.z, -direction.y) / absoluteDirection.x;
        }
    }
    else if (absoluteDirection.y >= absoluteDirection.z)
    {
        if (direction.y >= 0.0f)
        {
            faceIndex = 2u;
            faceUv = float2(direction.x, direction.z) / absoluteDirection.y;
        }
        else
        {
            faceIndex = 3u;
            faceUv = float2(direction.x, -direction.z) / absoluteDirection.y;
        }
    }
    else if (direction.z >= 0.0f)
    {
        faceIndex = 4u;
        faceUv = float2(direction.x, -direction.y) / absoluteDirection.z;
    }
    else
    {
        faceIndex = 5u;
        faceUv = float2(-direction.x, -direction.y) / absoluteDirection.z;
    }

    faceUv = faceUv * 0.5f + 0.5f;
    return float2((float(faceIndex) + faceUv.x) / 6.0f, faceUv.y);
}

#endif
//Modify End
