//Modify Begin:2026-08-20 by Hui
ByteAddressBuffer ActivePixelCount;
RWByteAddressBuffer IndirectArguments;

[numthreads(1, 1, 1)]
void main()
{
    const uint activePixelCount = ActivePixelCount.Load(0u);
    IndirectArguments.Store4(
        0u,
        uint4(activePixelCount, (activePixelCount + 63u) / 64u, 1u, 1u));
}
//Modify End
