# ReSTIR GI integration status

## Reference scope

The reference repository is [DQLin/ReSTIR_PT](https://github.com/DQLin/ReSTIR_PT), checked out locally at `C:\Users\minghuidai\Desktop\coderef\ReSTIRPT`. Its `Source/Falcor/Experimental/ScreenSpaceReSTIR/GIResampling.cs.slang` provides the ReSTIR GI reservoir data flow used as the algorithmic reference. The repository also contains the broader ReSTIR PT research implementation; this renderer currently implements only the one-bounce ReSTIR GI subset.

## Algorithmic flow

The reference separates ReSTIR GI into four logical stages:

1. Initial sampling traces the primary visible point, samples a BSDF or hemisphere direction, traces the secondary point, and stores its emitted radiance, direct-light estimate, bounded continuation-path radiance, and proposal PDF.
2. Temporal resampling reprojects the previous reservoir, rejects dissimilar primary surfaces, merges the previous reservoir, and clamps M to a bounded history length.
3. Spatial resampling merges neighboring reservoirs. It needs donor primary point and normal plus selected secondary point and normal for Jacobian and visibility correction.
4. Final shading evaluates the current-surface BSDF against the selected secondary sample, applies the reservoir contribution weight, and adds primary-surface emission.

The reference reservoir stores the selected sample, accumulated weight, contribution weight W, and sample count M. Its selected GI sample contains primary and secondary positions/normals, radiance, proposal PDF, and a validity flag.

## Implemented Framework API boundary

The renderer now mirrors `ReSTIRDIPass`, not the Falcor pass API:

    struct ReSTIRGIExecutionInputs
    {
        ReSTIRGIFrameState FrameState;
        std::shared_ptr<Texture> IndirectLighting;
        std::shared_ptr<Texture> MotionVector;
        ActivePixelDispatch CompactedDispatch = {};
        std::function<void(CommandContext&)> PrepareCommandContext;
        std::function<void(CommandContext&, ComputeShader&)> BindSceneInputs;
    };

    struct ReSTIRGIGraphInputs
    {
        RenderGraph::ResourceId IndirectLighting;
        RenderGraph::ResourceId InputToken;
        RenderGraph::ResourceId OutputToken;
        std::function<void(RenderGraph::RenderGraphPassBuilder&)> DeclareSharedResources;
        std::function<ReSTIRGIExecutionInputs(const RenderGraph::RenderContext&)> ResolveFrameInputs;
    };

    class ReSTIRGIPass
    {
    public:
        void AddPasses(RenderGraph::RenderGraphBuilder&, ReSTIRGIGraphInputs);
    };

`ReSTIRGIPass` owns `Initial`, `Temporal`, and persistent `History` packed reservoir sets, plus hard/soft-shadow, environment-projection, material-shading, algorithm-setting, and compacted-dispatch shader variants. `AddPasses` imports those persistent allocations and registers Initial/Temporal/Spatial/Shade as distinct graph stages; it never stores the builder. The demo provides the GBuffer, TLAS, bindless scene adapter, direct-light/emission/environment evaluation, optional `ActivePixelDispatch`, and output resource. The RenderGraph constructs exactly one indirect-lighting producer: PathTracing, ReSTIR GI, or a disabled producer; there are never two runtime-gated writers of the same output.

## Current boundaries

The implemented path resamples one secondary transport vertex. Its stored radiance uses the same bounded continuation estimator as ordinary indirect path tracing. `RAYTRACING_DEMO_MAX_BOUNCES` and `RESTIR_GI_MAX_PATH_BOUNCES` are discrete shader variants, so the fixed bounce count can be unrolled without changing the reservoir representation. In compacted mode, all four stages consume the common active-pixel list and indirect compute arguments; `ActivePixelCount` remains a pixel count rather than a ray count. Previous-frame TLAS, reuse of multi-bounce path segments, ReSTIR-N, full source-equivalent unbiased spatial correction, and final quality/performance acceptance remain separate milestones. The packed resources use approximately 144 bytes per pixel before allocator overhead, so memory cost must be measured before enabling it at high resolution.

The GI reservoir cannot reuse the current DI light-index-only reservoir format. It must represent a secondary transport sample and its proposal. Dynamic geometry must not be hidden behind an unconditional history reset; invalidation and GPU resource retirement need explicit lifetime rules.
