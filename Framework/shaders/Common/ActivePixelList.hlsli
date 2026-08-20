//Modify Begin:2026-08-19 by Hui
#ifndef FRAMEWORK_ACTIVE_PIXEL_LIST_HLSLI
#define FRAMEWORK_ACTIVE_PIXEL_LIST_HLSLI

#if !defined(FRAMEWORK_ACTIVE_PIXEL_LIST)
#define FRAMEWORK_ACTIVE_PIXEL_LIST 0
#endif

#if FRAMEWORK_ACTIVE_PIXEL_LIST
#define FRAMEWORK_RAY_TRACED_PIXEL_THREAD_GROUP_SIZE_X 64
#define FRAMEWORK_RAY_TRACED_PIXEL_THREAD_GROUP_SIZE_Y 1
#else
#define FRAMEWORK_RAY_TRACED_PIXEL_THREAD_GROUP_SIZE_X 8
#define FRAMEWORK_RAY_TRACED_PIXEL_THREAD_GROUP_SIZE_Y 8
#endif

#if FRAMEWORK_ACTIVE_PIXEL_LIST

#if !defined(FRAMEWORK_ACTIVE_PIXEL_INDICES_REGISTER)
#define FRAMEWORK_ACTIVE_PIXEL_INDICES_REGISTER register(t26, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE)
#endif

#if !defined(FRAMEWORK_ACTIVE_PIXEL_COUNT_REGISTER)
#define FRAMEWORK_ACTIVE_PIXEL_COUNT_REGISTER register(t27, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE)
#endif

StructuredBuffer<uint> FrameworkActivePixelIndices : FRAMEWORK_ACTIVE_PIXEL_INDICES_REGISTER;
ByteAddressBuffer FrameworkActivePixelCount : FRAMEWORK_ACTIVE_PIXEL_COUNT_REGISTER;

bool FrameworkResolveRayTracedPixel(
    const uint3 physicalDispatchThreadId,
    const uint screenWidth,
    const uint screenHeight,
    out uint2 logicalPixel)
{
    const uint dispatchIndex = physicalDispatchThreadId.x;
    if (dispatchIndex >= FrameworkActivePixelCount.Load(0u))
    {
        logicalPixel = 0u;
        return false;
    }

    const uint pixelIndex = FrameworkActivePixelIndices[dispatchIndex];
    logicalPixel = uint2(pixelIndex % screenWidth, pixelIndex / screenWidth);
    return logicalPixel.y < screenHeight;
}

#else

bool FrameworkResolveRayTracedPixel(
    const uint3 physicalDispatchThreadId,
    const uint screenWidth,
    const uint screenHeight,
    out uint2 logicalPixel)
{
    logicalPixel = physicalDispatchThreadId.xy;
    return logicalPixel.x < screenWidth && logicalPixel.y < screenHeight;
}

#endif
#endif
//Modify End
