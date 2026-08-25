//Modify Begin:2026-08-25 by Hui
cbuffer OIDNCompositeConstants : register(b0)
{
    uint Width;
    uint Height;
    uint ResultValid;
    uint Padding0;
};

Texture2D<float4> DenoisedResult : register(t0);
RWTexture2D<float4> SceneColor : register(u0);

[numthreads(8, 8, 1)]
void main(const uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= Width || dispatchThreadId.y >= Height || ResultValid == 0u)
    {
        return;
    }

    SceneColor[dispatchThreadId.xy] = DenoisedResult.Load(int3(dispatchThreadId.xy, 0));
}
//Modify End
