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

cbuffer ModelCBuffer : register(b0, COMMON_ROOT_SIGNATURE_MODEL_SPACE)
{
    matrix g_Model_Model;
    matrix g_Model_ModelViewProjection;
    matrix g_Model_InverseTransposeModel;
    matrix g_Model_PreviousModelViewProjection;
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
    float3 PositionWs : POSITION_WS;
    float3 NormalWs : NORMAL;
    float3 TangentWs : TANGENT;
    float3 BitangentWs : BINORMAL;
    float2 Uv : TEXCOORD0;
    float4 CurrentPositionCs : TEXCOORD1;
    float4 PreviousPositionCs : TEXCOORD2;
//Modify Begin:2026-07-30 by Hui
    nointerpolation uint MeshletDebugId : TEXCOORD3;
//Modify End
    float4 PositionCs : SV_POSITION;
};

VertexShaderOutput main(VertexAttributes IN)
{
    VertexShaderOutput OUT;
    const float4 positionWs = mul(g_Model_Model, float4(IN.PositionOs, 1.0f));
    OUT.PositionWs = positionWs.xyz;
    OUT.NormalWs = normalize(mul((float3x3)g_Model_InverseTransposeModel, IN.NormalOs));
    OUT.TangentWs = normalize(mul((float3x3)g_Model_Model, IN.TangentOs));
    OUT.BitangentWs = normalize(mul((float3x3)g_Model_Model, IN.BitangentOs));
    OUT.Uv = IN.Uv;
    OUT.PositionCs = mul(g_Model_ModelViewProjection, float4(IN.PositionOs, 1.0f));
    OUT.CurrentPositionCs = OUT.PositionCs;
    OUT.PreviousPositionCs = mul(g_Model_PreviousModelViewProjection, float4(IN.PositionOs, 1.0f));
//Modify Begin:2026-07-30 by Hui
    OUT.MeshletDebugId = 0;
//Modify End
    return OUT;
}
