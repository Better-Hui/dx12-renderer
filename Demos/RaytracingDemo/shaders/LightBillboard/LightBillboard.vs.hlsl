#include <ShaderLibrary/Common/RootSignature.hlsli>

cbuffer PipelineCBuffer : register(b0, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE)
{
    matrix g_Pipeline_View;
    matrix g_Pipeline_Projection;
    matrix g_Pipeline_ViewProjection;
    float4 g_Pipeline_CameraPosition;
    matrix g_Pipeline_InverseView;
    matrix g_Pipeline_InverseProjection;
    float2 g_Pipeline_ScreenResolution;
    float2 g_Pipeline_ScreenTexelSize;
};

cbuffer MaterialCBuffer : register(b0)
{
    float4 g_LightBillboard_PositionAndSize;
    float4 g_LightBillboard_ColorAndAlpha;
    float4 g_LightBillboard_CameraRight;
    float4 g_LightBillboard_CameraUp;
//Modify Begin:2026-07-30 by Hui
    float4 g_LightBillboard_TypeAndParams;
//Modify End
//Modify Begin:2026-08-26 by Hui
    float4 g_LightBillboard_DirectionAndLength;
//Modify End
};

struct VertexAttributes
{
    float3 PositionOs : POSITION;
    float3 NormalOs : NORMAL;
    float2 Uv : TEXCOORD;
    float3 TangentOs : TANGENT;
    float3 BitangentOs : BINORMAL;
};

struct VertexShaderOutput
{
    float2 Uv : TEXCOORD0;
    float4 ColorAndAlpha : COLOR0;
//Modify Begin:2026-07-30 by Hui
    float4 TypeAndParams : TEXCOORD1;
//Modify End
    float4 PositionCs : SV_POSITION;
};

VertexShaderOutput main(VertexAttributes IN)
{
    VertexShaderOutput OUT;
    const uint lightType = (uint)round(g_LightBillboard_TypeAndParams.x);
    float3 positionWs;
    if (lightType == 3u)
    {
        const float3 forward = normalize(g_LightBillboard_DirectionAndLength.xyz);
        const float3 referenceAxis = abs(forward.y) < 0.99f ?
            float3(0.0f, 1.0f, 0.0f) :
            float3(1.0f, 0.0f, 0.0f);
        const float3 right = normalize(cross(referenceAxis, forward));
        const float3 up = cross(forward, right);
        const float length = max(g_LightBillboard_DirectionAndLength.w, 0.001f);
        const float radius = max(g_LightBillboard_TypeAndParams.w, 0.0f);
        const float distanceAlongCone = (0.5f - IN.PositionOs.y) * length;
        const float2 coneOffset = float2(IN.PositionOs.x, IN.PositionOs.z) * (2.0f * radius);
        positionWs =
            g_LightBillboard_PositionAndSize.xyz +
            forward * distanceAlongCone +
            right * coneOffset.x +
            up * coneOffset.y;
    }
    else
    {
        const float2 quadOffset = IN.PositionOs.xy * g_LightBillboard_PositionAndSize.w;
        positionWs =
            g_LightBillboard_PositionAndSize.xyz +
            g_LightBillboard_CameraRight.xyz * quadOffset.x +
            g_LightBillboard_CameraUp.xyz * quadOffset.y;
    }

    OUT.Uv = IN.Uv;
    OUT.ColorAndAlpha = g_LightBillboard_ColorAndAlpha;
//Modify Begin:2026-07-30 by Hui
    OUT.TypeAndParams = g_LightBillboard_TypeAndParams;
//Modify End
    OUT.PositionCs = mul(g_Pipeline_ViewProjection, float4(positionWs, 1.0f));
    return OUT;
}
