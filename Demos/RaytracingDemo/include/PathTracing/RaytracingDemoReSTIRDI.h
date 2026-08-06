#pragma once

//Modify Begin:2026-07-30 by BestHui
#include <FrameworkRenderFeatures/Lighting/ReSTIRDIPass.h>

struct RaytracingDemoPassConfig;
struct RaytracingDemoPassResources;

namespace RaytracingDemoReSTIRDI
{
    FrameworkRenderFeatures::ReSTIRDIPassInputs CreatePassInputs(
        const RaytracingDemoPassResources& resources,
        const RaytracingDemoPassConfig& config);
}
//Modify End
