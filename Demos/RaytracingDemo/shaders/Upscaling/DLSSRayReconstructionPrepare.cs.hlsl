//Modify Begin:2026-08-07 by BestHui
Texture2D<float4> GBufferNormal : register(t0, space0);
Texture2D<float4> GBufferSpecularSmoothness : register(t1, space0);
Texture2D<float> DepthBuffer : register(t2, space0);
RWTexture2D<float4> NormalRoughness : register(u0, space0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    uint width = 0u;
    uint height = 0u;
    NormalRoughness.GetDimensions(width, height);
    if (pixel.x >= width || pixel.y >= height)
    {
        return;
    }

    if (DepthBuffer.Load(int3(pixel, 0)) >= 0.999999f)
    {
        NormalRoughness[pixel] = float4(0.0f, 0.0f, 1.0f, 1.0f);
        return;
    }

    const float3 encodedNormal = GBufferNormal.Load(int3(pixel, 0)).xyz;
    const float3 normal = normalize(encodedNormal * 2.0f - 1.0f);
    const float smoothness = GBufferSpecularSmoothness.Load(int3(pixel, 0)).a;
    NormalRoughness[pixel] = float4(normal, 1.0f - saturate(smoothness));
}
//Modify End
