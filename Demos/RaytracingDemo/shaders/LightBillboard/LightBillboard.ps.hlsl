struct PixelShaderInput
{
    float2 Uv : TEXCOORD0;
    float4 ColorAndAlpha : COLOR0;
//Modify Begin:2026-07-30 by Hui
    float4 TypeAndParams : TEXCOORD1;
//Modify End
};

float4 main(PixelShaderInput IN) : SV_TARGET
{
    const float2 centeredUv = IN.Uv * 2.0f - 1.0f;
    const float distanceToCenter = length(centeredUv);
//Modify Begin:2026-07-30 by Hui
    const uint lightType = (uint)round(IN.TypeAndParams.x);

    float innerAlpha = 0.0f;
    float shapeAlpha = 0.0f;
    if (lightType == 1u)
    {
        const float2 absUv = abs(centeredUv);
        const float square = 1.0f - smoothstep(0.76f, 0.96f, max(absUv.x, absUv.y));
        const float diamondDistance = abs(centeredUv.x) + abs(centeredUv.y);
        const float diamond = smoothstep(0.54f, 0.66f, diamondDistance) * (1.0f - smoothstep(0.84f, 0.98f, diamondDistance));
        innerAlpha = 1.0f - smoothstep(0.16f, 0.34f, max(absUv.x, absUv.y));
        shapeAlpha = saturate(max(innerAlpha, max(square * 0.45f, diamond)));
    }
    else if (lightType == 2u)
    {
        const float sunCore = 1.0f - smoothstep(0.18f, 0.34f, distanceToCenter);
        const float rayMask = max(abs(centeredUv.x), abs(centeredUv.y));
        const float diagonalMask = abs(abs(centeredUv.x) - abs(centeredUv.y));
        const float axialRays = (1.0f - smoothstep(0.06f, 0.16f, min(abs(centeredUv.x), abs(centeredUv.y)))) * smoothstep(0.34f, 0.52f, rayMask) * (1.0f - smoothstep(0.72f, 0.94f, rayMask));
        const float diagonalRays = (1.0f - smoothstep(0.04f, 0.14f, diagonalMask)) * smoothstep(0.36f, 0.54f, distanceToCenter) * (1.0f - smoothstep(0.76f, 0.98f, distanceToCenter));
        innerAlpha = sunCore;
        shapeAlpha = saturate(max(sunCore, max(axialRays, diagonalRays) * 0.82f));
    }
    else
    {
        const float outerAlpha = 1.0f - smoothstep(0.78f, 1.0f, distanceToCenter);
        innerAlpha = 1.0f - smoothstep(0.22f, 0.42f, distanceToCenter);
        const float ringAlpha = smoothstep(0.50f, 0.62f, distanceToCenter) * (1.0f - smoothstep(0.72f, 0.90f, distanceToCenter));
        shapeAlpha = saturate(max(innerAlpha, ringAlpha * 0.85f) * outerAlpha);
    }
    const float alpha = shapeAlpha * IN.ColorAndAlpha.a;

    clip(alpha - 0.001f);

    const float3 color = lerp(IN.ColorAndAlpha.rgb * 0.65f, float3(1.0f, 1.0f, 1.0f), innerAlpha * 0.35f);
    return float4(color, alpha);
//Modify End
}
