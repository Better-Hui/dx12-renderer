//Modify Begin:2026-08-25 by Hui
#include <ShaderLibrary/Common/RootSignature.hlsli>
#include <Meshlet/MeshletCommon.hlsli>

struct MeshletVertexOutput
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

StructuredBuffer<MeshletVertexAttributes> MeshletVertices : register(t0, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
ByteAddressBuffer MeshletIndices : register(t1, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
StructuredBuffer<Meshlet> Meshlets : register(t2, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
StructuredBuffer<MeshletTransformData> MeshletTransforms : register(t3, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
StructuredBuffer<MeshletInstanceData> MeshletInstances : register(t4, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);

[outputtopology("triangle")]
[numthreads(MeshletShaderThreadCount, 1, 1)]
void main(
    uint groupThreadId : SV_GroupThreadID,
    uint groupId : SV_GroupID,
    in payload MeshletTaskPayload payload,
    out vertices MeshletVertexOutput vertices[MeshletMaxVertices],
    out indices uint3 primitives[MeshletMaxPrimitives])
{
    const uint instanceIndex = payload.MeshletInstanceIndices[groupId];
    const MeshletInstanceData instance = MeshletInstances[instanceIndex];
    const Meshlet meshlet = Meshlets[instance.MeshletIndex];
    const MeshletTransformData transform = MeshletTransforms[instance.TransformIndex];

    const uint vertexCount = meshlet.VertexCount;
    const uint primitiveCount = meshlet.IndexCount / 3;
    SetMeshOutputCounts(vertexCount, primitiveCount);

    if (groupThreadId < vertexCount)
    {
        const MeshletVertexAttributes input = MeshletVertices[meshlet.VertexOffset + groupThreadId];
        MeshletVertexOutput output;
        const float4 positionWs = mul(float4(input.Position.xyz, 1.0f), transform.Model);
        output.PositionWs = positionWs.xyz;
        output.NormalWs = normalize(mul(input.Normal.xyz, (float3x3)transform.InverseTransposeModel));
        output.TangentWs = normalize(mul(input.Tangent.xyz, (float3x3)transform.Model));
        output.BitangentWs = normalize(mul(input.Bitangent.xyz, (float3x3)transform.Model));
        output.Uv = input.Uv.xy;
        output.PositionCs = mul(g_Pipeline_ViewProjection, positionWs);
        output.CurrentPositionCs = output.PositionCs;
        output.PreviousPositionCs = mul(g_Pipeline_PreviousViewProjection, positionWs);
        output.MeshletDebugId = instance.MeshletIndex;
        output.MaterialIndex = instance.MaterialIndex;
        output.DebugMeshletClusters = g_Pipeline_DebugMeshletClusters;
        vertices[groupThreadId] = output;
    }

    if (groupThreadId < primitiveCount)
    {
        const uint baseIndex = groupThreadId * 3;
        primitives[groupThreadId] = uint3(
            MeshletLoadIndex(MeshletIndices, meshlet.IndexOffset, baseIndex + 0),
            MeshletLoadIndex(MeshletIndices, meshlet.IndexOffset, baseIndex + 1),
            MeshletLoadIndex(MeshletIndices, meshlet.IndexOffset, baseIndex + 2));
    }
}
//Modify End
