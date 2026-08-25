//Modify Begin:2026-08-25 by Hui
Texture2D<float4> Source : register(t0);
RWTexture2D<float4> Destination : register(u0);

[numthreads(8, 8, 1)]
void main(const uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint width = 0;
    uint height = 0;
    Destination.GetDimensions(width, height);
    if (dispatchThreadId.x >= width || dispatchThreadId.y >= height)
    {
        return;
    }

    Destination[dispatchThreadId.xy] = Source.Load(int3(dispatchThreadId.xy, 0));
}
//Modify End
