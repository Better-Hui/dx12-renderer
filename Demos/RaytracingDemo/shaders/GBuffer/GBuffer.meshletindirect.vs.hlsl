//Modify Begin:2026-07-30 by BestHui
#include <ShaderLibrary/Common/RootSignature.hlsli>
#include <Meshlet/MeshletCommon.hlsli>

struct VertexShaderOutput
{
    float3 PositionWs : POSITION_WS;
    float3 NormalWs : NORMAL;
    float3 TangentWs : TANGENT;
    float3 BitangentWs : BINORMAL;
    float2 Uv : TEXCOORD0;
    float4 CurrentPositionCs : TEXCOORD1;
    float4 PreviousPositionCs : TEXCOORD2;
    nointerpolation uint MeshletDebugId : TEXCOORD3;
    nointerpolation uint MaterialIndex : TEXCOORD4;
    nointerpolation uint DebugMeshletClusters : TEXCOORD5;
    float4 PositionCs : SV_POSITION;
};

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
    matrix g_Pipeline_PreviousViewProjection;
    uint g_Pipeline_DebugMeshletClusters;
    uint3 g_Pipeline_Padding0;
};

cbuffer MeshletDrawCBuffer : register(b4, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE)
{
    uint g_MeshletDraw_InstanceIndex;
    uint g_MeshletDraw_Flags;
    uint2 g_MeshletDraw_Padding0;
};

StructuredBuffer<MeshletVertexAttributes> MeshletVertices : register(t0, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
ByteAddressBuffer MeshletIndices : register(t1, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
StructuredBuffer<Meshlet> Meshlets : register(t2, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
StructuredBuffer<MeshletTransformData> MeshletTransforms : register(t3, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
StructuredBuffer<MeshletInstanceData> MeshletInstances : register(t4, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);

VertexShaderOutput main(uint vertexId : SV_VertexID)
{
    const MeshletInstanceData instance = MeshletInstances[g_MeshletDraw_InstanceIndex];
    const Meshlet meshlet = Meshlets[instance.MeshletIndex];
    const MeshletTransformData transform = MeshletTransforms[instance.TransformIndex];
    const uint localVertexIndex = MeshletLoadIndex(MeshletIndices, meshlet.IndexOffset, vertexId);
    const MeshletVertexAttributes input = MeshletVertices[meshlet.VertexOffset + localVertexIndex];

    VertexShaderOutput output;
    const float4 positionWs = mul(transform.Model, float4(input.Position.xyz, 1.0f));
    output.PositionWs = positionWs.xyz;
    output.NormalWs = normalize(mul((float3x3)transform.InverseTransposeModel, input.Normal.xyz));
    output.TangentWs = normalize(mul((float3x3)transform.Model, input.Tangent.xyz));
    output.BitangentWs = normalize(mul((float3x3)transform.Model, input.Bitangent.xyz));
    output.Uv = input.Uv.xy;
    output.PositionCs = mul(g_Pipeline_ViewProjection, positionWs);
    output.CurrentPositionCs = output.PositionCs;
    output.PreviousPositionCs = mul(g_Pipeline_PreviousViewProjection, positionWs);
    output.MeshletDebugId = instance.MeshletIndex;
    output.MaterialIndex = instance.MaterialIndex;
    output.DebugMeshletClusters = g_Pipeline_DebugMeshletClusters;
    return output;
}
//Modify End
