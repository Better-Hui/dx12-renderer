//Modify Begin:2026-07-27 by Hui
#include <ShaderLibrary/Common/RootSignature.hlsli>
#include "../../../External/NRD/Shaders/NRD.hlsli"

cbuffer NRDInputAdapterConstants : register(b0)
{
    row_major matrix WorldToView;
    row_major matrix PreviousWorldToView;
    uint Width;
    uint Height;
    uint Padding0;
    uint Padding1;
};

Texture2D<float4> GBufferSpecularSmoothness : register(t0);
Texture2D<float4> GBufferNormal : register(t1);
Texture2D<float4> GBufferPosition : register(t2);
Texture2D<float> DepthTexture : register(t3);
Texture2D<float2> MotionVector : register(t4);

RWTexture2D<float4> NRDNormalRoughness : register(u0);
RWTexture2D<float> NRDViewZ : register(u1);
RWTexture2D<float4> NRDMotion : register(u2);

//Modify Begin:2026-08-18 by Hui
float3 DecodeGBufferNormal(float3 encoded)
{
    return normalize(encoded * 2.0f - 1.0f);
}
//Modify End

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadId.xy;
    if (pixel.x >= Width || pixel.y >= Height)
    {
        return;
    }

    float depth = DepthTexture.Load(int3(pixel, 0));
    if (depth >= 1.0f)
    {
        NRDNormalRoughness[pixel] = NRD_FrontEnd_PackNormalAndRoughness(float3(0.0f, 0.0f, 1.0f), 1.0f, 0.0f);
        NRDViewZ[pixel] = 1000000.0f;
        NRDMotion[pixel] = 0.0f;
        return;
    }

    float4 normalSample = GBufferNormal.Load(int3(pixel, 0));
    float4 specularSmoothness = GBufferSpecularSmoothness.Load(int3(pixel, 0));
    float4 position = GBufferPosition.Load(int3(pixel, 0));

//Modify Begin:2026-08-18 by Hui
    float3 normalWs = DecodeGBufferNormal(normalSample.xyz);
//Modify End
    float roughness = 1.0f - saturate(specularSmoothness.a);
    float viewZ = mul(float4(position.xyz, 1.0f), WorldToView).z;
    float previousViewZ = mul(float4(position.xyz, 1.0f), PreviousWorldToView).z;

    NRDNormalRoughness[pixel] = NRD_FrontEnd_PackNormalAndRoughness(normalWs, roughness, 0.0f);
    NRDViewZ[pixel] = viewZ;
//Modify Begin:2026-07-28 by Hui
    const float2 motionInPixels = MotionVector.Load(int3(pixel, 0)) * float2(Width, Height);
    NRDMotion[pixel] = float4(motionInPixels, previousViewZ - viewZ, 0.0f);
//Modify End
}
//Modify End
