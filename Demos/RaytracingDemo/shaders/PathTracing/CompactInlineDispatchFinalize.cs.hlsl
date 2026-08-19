//Modify Begin:2026-08-19 by Hui
ByteAddressBuffer ActiveRayPixelCount;
RWByteAddressBuffer IndirectArguments;

[numthreads(1, 1, 1)]
void main()
{
    const uint activePixelCount = ActiveRayPixelCount.Load(0u);
    IndirectArguments.Store3(0u, uint3((activePixelCount + 63u) / 64u, 1u, 1u));
}
//Modify End
