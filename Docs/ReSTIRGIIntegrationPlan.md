# ReSTIR GI integration plan

## Reference scope

The reference repository at C:\Users\minghuidai\Desktop\coderef\ReSTIRGI is a 2023 Mitsuba3 and Dr.Jit teaching implementation. It is not a production real-time renderer. It is valuable for validating the algorithmic data flow, but its author explicitly states that the Jacobian bias correction is incomplete. Do not copy that implementation line by line as final renderer code.

## Algorithmic flow

The reference separates ReSTIR GI into four logical stages:

1. Initial sampling traces the primary visible point, samples a BSDF or hemisphere direction, traces the secondary point, and stores radiance plus proposal PDF.
2. Temporal resampling reprojects the previous reservoir, rejects dissimilar primary surfaces, merges the previous reservoir, and clamps M to a bounded history length.
3. Spatial resampling merges neighboring reservoirs. It needs donor primary point and normal plus selected secondary point and normal for Jacobian and visibility correction.
4. Final shading evaluates the current-surface BSDF against the selected secondary sample, applies the reservoir contribution weight, and adds primary-surface emission.

The reference reservoir stores the selected sample, accumulated weight, contribution weight W, and sample count M. Its selected GI sample contains primary and secondary positions/normals, radiance, proposal PDF, and a validity flag.

## Framework API boundary

The future renderer API should mirror ReSTIRDIPass, not the Python integrator:

    struct ReSTIRGIExecutionInputs
    {
        GBufferInputs Surface;
        std::shared_ptr<RayTracingAccelerationStructure> TopLevelAS;
        std::shared_ptr<Texture> DirectLighting;
        std::shared_ptr<Texture> IndirectLighting;
        std::shared_ptr<Texture> MotionVectors;
        ReSTIRGIFrameState FrameState;
        std::function<void(CommandList&, ComputeShader&)> BindSceneInputs;
    };

    class ReSTIRGIPass
    {
    public:
        void Execute(CommandList&, const ReSTIRGIExecutionInputs&);
    };

ReSTIRGIPass owns ping-pong reservoirs, reprojection metadata, intermediate resources, and shader variants. The demo provides only the GBuffer, TLAS, bindless scene adapter, light and emission domain, and output resource. The RenderGraph constructs exactly one indirect-lighting producer: PathTracing or ReSTIR GI, never two runtime-gated writers of the same output.

## Implementation milestones

The first implementation should target one-bounce GI with RIS, temporal reuse, spatial reuse, and final shading. Previous-frame TLAS support, multi-bounce reuse, exact Jacobian correction, and configurable visibility/bias policies are separate correctness milestones.

The GI reservoir cannot reuse the current DI light-index-only reservoir format. It must represent a secondary transport sample and its proposal. Dynamic geometry must not be hidden behind an unconditional history reset; invalidation and GPU resource retirement need explicit lifetime rules.
