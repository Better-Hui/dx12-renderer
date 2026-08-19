//Modify Begin:2026-08-19 by Hui
ByteAddressBuffer ActiveRayPixelCount;
RWByteAddressBuffer IndirectArguments;

static const uint DispatchRaysWidthOffset = 88u;
static const uint DispatchRaysHeightOffset = 92u;
static const uint DispatchRaysDepthOffset = 96u;

[numthreads(1, 1, 1)]
void main()
{
    IndirectArguments.Store(DispatchRaysWidthOffset, ActiveRayPixelCount.Load(0u));
    IndirectArguments.Store(DispatchRaysHeightOffset, 1u);
    IndirectArguments.Store(DispatchRaysDepthOffset, 1u);
}
//Modify End
