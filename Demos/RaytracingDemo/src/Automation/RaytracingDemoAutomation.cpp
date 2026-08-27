//Modify Begin:2026-08-26 by Hui
#include <Automation/RaytracingDemoAutomation.h>

#include <utility>

namespace
{
    const char* GetBackendName(const PathTracingBackend backend)
    {
        return backend == PathTracingBackend::InlineRayQuery ? "inline" : "dxr";
    }

    const char* GetLightingName(const RaytracingDemoLightingTechnique technique)
    {
        switch (technique)
        {
        case RaytracingDemoLightingTechnique::PathTracing:
            return "pathtracing";
        case RaytracingDemoLightingTechnique::ReSTIRDI:
            return "restirdi";
        case RaytracingDemoLightingTechnique::ReSTIRGI:
            return "restirgi";
        case RaytracingDemoLightingTechnique::None:
        default:
            return "none";
        }
    }

    const char* GetDlssModeName(const DLSSMode mode)
    {
        switch (mode)
        {
        case DLSSMode::DLAA:
            return "dlaa";
        case DLSSMode::Quality:
            return "quality";
        case DLSSMode::Balanced:
            return "balanced";
        case DLSSMode::Performance:
            return "performance";
        case DLSSMode::UltraPerformance:
            return "ultra-performance";
        case DLSSMode::Disabled:
        default:
            return "off";
        }
    }

    const char* GetMaterialShadingName(const MaterialShadingModel shadingModel)
    {
        switch (shadingModel)
        {
        case MaterialShadingModel::StylizedComic:
            return "stylized-comic";
        case MaterialShadingModel::Pbr:
        default:
            return "pbr";
        }
    }
}

const char* RaytracingDemoAutomation::GetActionControlName(const Action action)
{
    switch (action)
    {
    case Action::SoftShadows: return "render.soft_shadows";
    case Action::StressSpheres: return "scene.stress_spheres";
    case Action::MeshletGBuffer: return "render.gbuffer.meshlet.enabled";
    case Action::MeshletTaskShader: return "render.gbuffer.meshlet.task_shader";
    case Action::PathTracingBackend: return "raytracing.backend";
    case Action::DirectLighting: return "lighting.direct.technique";
    case Action::IndirectLighting: return "lighting.indirect.technique";
    case Action::AsyncCompute: return "render_graph.async_compute";
    case Action::ParallelDirectCommandRecording: return "render_graph.parallel_direct_recording";
    case Action::Skybox: return "render.skybox";
    case Action::Accumulation: return "raytracing.accumulation";
    case Action::GpuTiming: return "profiler.gpu_timing";
    case Action::TimingCapture: return "profiler.capture";
    case Action::ReSTIRGIStageTiming: return "profiler.restirgi_stages";
    case Action::ReSTIRGITemporalResampling: return "restirgi.temporal_resampling";
    case Action::ReSTIRGISpatialResampling: return "restirgi.spatial_resampling";
    case Action::ReSTIRGITemporalJacobian: return "restirgi.temporal_jacobian";
    case Action::ReSTIRGISpatialRayTracedBiasCorrection: return "restirgi.spatial_raytraced_bias";
    case Action::DumpTiming: return "profiler.export";
    case Action::DLSS: return "upscaling.dlss_mode";
    case Action::MaterialShading: return "material.shading_model";
    case Action::ReSTIRDIConfig: return "restirdi.variant_bits";
    case Action::MaxBounces: return "raytracing.max_bounces";
    case Action::Wait: return "automation.wait";
    case Action::VerifyActiveRayTracedPixelCount: return "assert.active_pixel_dispatch";
    case Action::CopyQueueValidation: return "render_graph.copy_queue_validation";
    case Action::VerifyCopyQueueValidation: return "assert.copy_queue_validation";
    case Action::DynamicRayTracingUpdate: return "raytracing.dynamic_rtas_update";
    case Action::VerifyDynamicRayTracingUpdate: return "assert.dynamic_rtas_update";
    case Action::VerifyDynamicSkinnedMeshCapability: return "assert.dynamic_skinned_mesh_capability";
    case Action::Denoiser: return "denoiser.algorithm";
    case Action::OIDNStaticSpp: return "denoiser.oidn_static_spp";
    case Action::VerifyOIDNResult: return "assert.oidn_result";
//Modify Begin:2026-08-25 by Hui
    case Action::OIDNCameraMotion: return "camera.oidn_motion";
    case Action::VerifyOIDNInvalidated: return "assert.oidn_motion_invalidation";
//Modify End
    case Action::CaptureScreenshot: return "capture.screenshot";
    case Action::MatrixCase: return "scenario.matrix_case";
    default: return "unknown";
    }
}

const std::vector<RaytracingDemoAutomation::MatrixCase>& RaytracingDemoAutomation::GetMatrixCases()
{
    static const std::vector<MatrixCase> cases = []()
    {
        struct MeshletMode
        {
            bool Enabled;
            bool TaskShader;
            const char* Name;
        };

        const std::vector<MeshletMode> meshletModes = {
            { false, true, "raster" },
            { true, true, "task" },
            { true, false, "indirect" },
        };
        const std::vector<PathTracingBackend> backends = {
            PathTracingBackend::InlineRayQuery,
            PathTracingBackend::ShaderTableDxr,
        };
        const std::vector<RaytracingDemoLightingTechnique> directTechniques = {
            RaytracingDemoLightingTechnique::None,
            RaytracingDemoLightingTechnique::PathTracing,
            RaytracingDemoLightingTechnique::ReSTIRDI,
        };
        const std::vector<RaytracingDemoLightingTechnique> indirectTechniques = {
            RaytracingDemoLightingTechnique::None,
            RaytracingDemoLightingTechnique::PathTracing,
            RaytracingDemoLightingTechnique::ReSTIRGI,
        };
        const std::vector<DLSSMode> dlssModes = {
            DLSSMode::Disabled,
            DLSSMode::Quality,
        };
        const std::vector<MaterialShadingModel> materialShadingModels = {
            MaterialShadingModel::Pbr,
            MaterialShadingModel::StylizedComic,
        };

        std::vector<MatrixCase> result;
        uint32_t caseIndex = 1;
        for (const MaterialShadingModel shadingModel : materialShadingModels)
        {
            for (const bool stressSpheres : { false, true })
            {
                for (const bool softShadows : { false, true })
                {
                    for (const PathTracingBackend backend : backends)
                    {
                        for (const MeshletMode& meshletMode : meshletModes)
                        {
                            for (const RaytracingDemoLightingTechnique directLighting : directTechniques)
                            {
                                if (directLighting == RaytracingDemoLightingTechnique::ReSTIRDI &&
                                    backend != PathTracingBackend::InlineRayQuery)
                                {
                                    continue;
                                }

                                for (const RaytracingDemoLightingTechnique indirectLighting : indirectTechniques)
                                {
                                    if (indirectLighting == RaytracingDemoLightingTechnique::ReSTIRGI &&
                                        backend != PathTracingBackend::InlineRayQuery)
                                    {
                                        continue;
                                    }

                                    for (const bool asyncCompute : { false, true })
                                    {
                                        if (asyncCompute && backend != PathTracingBackend::InlineRayQuery)
                                        {
                                            continue;
                                        }

                                        for (const bool parallelDirectRecording : { false, true })
                                        {
                                            for (const bool skybox : { false, true })
                                            {
                                                for (const bool accumulation : { false, true })
                                                {
                                                    for (const DLSSMode dlssMode : dlssModes)
                                                    {
                                                        MatrixCase testCase;
                                                        testCase.Backend = backend;
                                                        testCase.DirectLighting = directLighting;
                                                        testCase.IndirectLighting = indirectLighting;
                                                        testCase.AsyncCompute = asyncCompute;
                                                        testCase.ParallelDirectCommandRecording = parallelDirectRecording;
                                                        testCase.UseMeshletGBuffer = meshletMode.Enabled;
                                                        testCase.UseTaskShaderMeshlets = meshletMode.TaskShader;
                                                        testCase.SoftShadows = softShadows;
                                                        testCase.StressSpheres = stressSpheres;
                                                        testCase.Skybox = skybox;
                                                        testCase.Accumulation = accumulation;
                                                        testCase.DlssMode = dlssMode;
                                                        testCase.ShadingModel = shadingModel;
                                                        testCase.MaxBounces =
                                                            indirectLighting == RaytracingDemoLightingTechnique::None ? 1 : 3;
                                                        testCase.Name =
                                                            "matrix#" + std::to_string(caseIndex++) +
                                                            " shading=" + GetMaterialShadingName(shadingModel) +
                                                            " backend=" + GetBackendName(backend) +
                                                            " meshlet=" + meshletMode.Name +
                                                            " direct=" + GetLightingName(directLighting) +
                                                            " indirect=" + GetLightingName(indirectLighting) +
                                                            " async=" + std::to_string(asyncCompute) +
                                                            " parallelrecording=" + std::to_string(parallelDirectRecording) +
                                                            " soft=" + std::to_string(softShadows) +
                                                            " stress=" + std::to_string(stressSpheres) +
                                                            " skybox=" + std::to_string(skybox) +
                                                            " accumulation=" + std::to_string(accumulation) +
                                                            " bounces=" + std::to_string(testCase.MaxBounces) +
                                                            " dlss=" + GetDlssModeName(dlssMode);
                                                        result.push_back(std::move(testCase));
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        return result;
    }();
    return cases;
}

DemoAutomation::TestSuites RaytracingDemoAutomation::CreateTestSuites()
{
    const auto makeStep = [](const Action action, const uint32_t value, std::string name)
    {
        DemoAutomation::Step step;
        step.Action = static_cast<uint32_t>(action);
        step.Value = value;
        step.Control = GetActionControlName(action);
        step.Name = std::move(name);
        step.Kind = action == Action::Wait
            ? FrameworkDiagnostics::AutomationStepKind::WaitFrames
            : FrameworkDiagnostics::AutomationStepKind::SetControl;
        return step;
    };

    DemoAutomation::TestSuites testSuites;
    testSuites.Core = {
        makeStep(Action::GpuTiming, 1u, "timing=1"),
        makeStep(Action::TimingCapture, 1u, "timingcapture=1"),
        makeStep(Action::CopyQueueValidation, 1u, "copyqueue=1"),
        makeStep(Action::Wait, 0u, "copyqueue-warmup"),
        makeStep(Action::VerifyCopyQueueValidation, 0u, "copyqueue-verify"),
        makeStep(Action::CopyQueueValidation, 0u, "copyqueue=0"),
        makeStep(Action::DynamicRayTracingUpdate, 1u, "rtas-update=1"),
        makeStep(Action::Wait, 0u, "rtas-update-warmup=1"),
        makeStep(Action::Wait, 0u, "rtas-update-warmup=2"),
        makeStep(Action::Wait, 0u, "rtas-update-warmup=3"),
        makeStep(Action::VerifyDynamicRayTracingUpdate, 0u, "rtas-update-verify"),
        makeStep(Action::DynamicRayTracingUpdate, 0u, "rtas-update=0"),
        makeStep(Action::Wait, 0u, "rtas-update-restore"),
        makeStep(Action::VerifyDynamicRayTracingUpdate, 1u, "rtas-update-restore-verify"),
        makeStep(Action::SoftShadows, 0u, "soft=0"),
        makeStep(Action::SoftShadows, 1u, "soft=1"),
        makeStep(Action::StressSpheres, 1u, "stress=1"),
        makeStep(Action::StressSpheres, 0u, "stress=0"),
        makeStep(Action::StressSpheres, 1u, "stress=1"),
        makeStep(Action::StressSpheres, 0u, "stress=0"),
        makeStep(Action::MeshletGBuffer, 0u, "meshlet=0"),
        makeStep(Action::MeshletGBuffer, 1u, "meshlet=1"),
        makeStep(Action::MaterialShading, static_cast<uint32_t>(MaterialShadingModel::Pbr), "shading=pbr"),
        makeStep(Action::DLSS, static_cast<uint32_t>(DLSSMode::Disabled), "dlss=off"),
        makeStep(Action::DLSS, static_cast<uint32_t>(DLSSMode::DLAA), "dlss=dlaa"),
        makeStep(Action::DLSS, static_cast<uint32_t>(DLSSMode::Quality), "dlss=quality"),
        makeStep(Action::DLSS, static_cast<uint32_t>(DLSSMode::Balanced), "dlss=balanced"),
        makeStep(Action::DLSS, static_cast<uint32_t>(DLSSMode::Performance), "dlss=performance"),
        makeStep(Action::DLSS, static_cast<uint32_t>(DLSSMode::UltraPerformance), "dlss=ultra-performance"),
        makeStep(Action::MeshletTaskShader, 0u, "meshletbackend=indirect"),
        makeStep(Action::MeshletTaskShader, 1u, "meshletbackend=task"),
        makeStep(Action::DirectLighting, static_cast<uint32_t>(RaytracingDemoLightingTechnique::PathTracing), "direct=pathtracing"),
        makeStep(Action::PathTracingBackend, static_cast<uint32_t>(PathTracingBackend::ShaderTableDxr), "backend=dxr"),
        makeStep(Action::PathTracingBackend, static_cast<uint32_t>(PathTracingBackend::InlineRayQuery), "backend=inline"),
        makeStep(Action::MaxBounces, 3u, "bounces=3"),
        makeStep(Action::IndirectLighting, static_cast<uint32_t>(RaytracingDemoLightingTechnique::PathTracing), "indirect=pathtracing"),
        makeStep(Action::MaterialShading, static_cast<uint32_t>(MaterialShadingModel::StylizedComic), "shading=stylized-comic"),
        makeStep(Action::Wait, 0u, "shading=stylized-comic-warmup"),
        makeStep(Action::IndirectLighting, static_cast<uint32_t>(RaytracingDemoLightingTechnique::ReSTIRGI), "indirect=restirgi"),
        makeStep(Action::ReSTIRGIStageTiming, 1u, "restirgi-stage-timing=1"),
        makeStep(Action::Wait, 0u, "restirgi-stage-timing-warmup"),
        makeStep(Action::DumpTiming, 1u, "timingdump=restirgi"),
        makeStep(Action::ReSTIRGIStageTiming, 0u, "restirgi-stage-timing=0"),
        makeStep(Action::ParallelDirectCommandRecording, 0u, "parallelrecording=0"),
        makeStep(Action::ParallelDirectCommandRecording, 1u, "parallelrecording=1"),
        makeStep(Action::AsyncCompute, 1u, "async=1"),
        makeStep(Action::AsyncCompute, 0u, "async=0"),
        makeStep(Action::IndirectLighting, static_cast<uint32_t>(RaytracingDemoLightingTechnique::None), "indirect=none"),
        makeStep(Action::MaxBounces, 1u, "bounces=1"),
        makeStep(Action::DirectLighting, static_cast<uint32_t>(RaytracingDemoLightingTechnique::ReSTIRDI), "direct=restirdi"),
        makeStep(Action::Wait, 0u, "restirdi-stylized-comic-warmup"),
        makeStep(Action::MaterialShading, static_cast<uint32_t>(MaterialShadingModel::Pbr), "shading=pbr"),
        makeStep(Action::Skybox, 0u, "skybox=0"),
        makeStep(Action::Skybox, 1u, "skybox=1"),
        makeStep(Action::Accumulation, 0u, "accumulation=0"),
        makeStep(Action::Accumulation, 1u, "accumulation=1"),
        makeStep(Action::DumpTiming, 1u, "timingdump=1"),
        makeStep(Action::GpuTiming, 0u, "timing=0"),
    };
    testSuites.Stress = {
        makeStep(Action::StressSpheres, 1u, "stress=1"),
        makeStep(Action::StressSpheres, 0u, "stress=0"),
        makeStep(Action::StressSpheres, 1u, "stress=1"),
        makeStep(Action::StressSpheres, 0u, "stress=0"),
    };
    testSuites.MeshletIndirect = {
        makeStep(Action::Wait, 0u, "meshlet-indirect-warmup"),
        makeStep(Action::StressSpheres, 1u, "meshlet-indirect-stress=1"),
        makeStep(Action::Wait, 0u, "meshlet-indirect-stress-warmup"),
        makeStep(Action::StressSpheres, 0u, "meshlet-indirect-stress=0"),
        makeStep(Action::Wait, 0u, "meshlet-indirect-restore-warmup"),
    };
    testSuites.CopyQueue = {
        makeStep(Action::CopyQueueValidation, 1u, "copyqueue=1"),
        makeStep(Action::Wait, 0u, "copyqueue-warmup"),
        makeStep(Action::VerifyCopyQueueValidation, 0u, "copyqueue-verify"),
        makeStep(Action::CopyQueueValidation, 0u, "copyqueue=0"),
    };
    testSuites.Rtas = {
        makeStep(Action::DynamicRayTracingUpdate, 1u, "rtas-update=1"),
        makeStep(Action::Wait, 0u, "rtas-update-warmup=1"),
        makeStep(Action::Wait, 0u, "rtas-update-warmup=2"),
        makeStep(Action::Wait, 0u, "rtas-update-warmup=3"),
        makeStep(Action::VerifyDynamicRayTracingUpdate, 0u, "rtas-update-verify"),
        makeStep(Action::DynamicRayTracingUpdate, 0u, "rtas-update=0"),
        makeStep(Action::Wait, 0u, "rtas-update-restore"),
        makeStep(Action::VerifyDynamicRayTracingUpdate, 1u, "rtas-update-restore-verify"),
    };
    testSuites.DynamicScene = {
        makeStep(Action::MeshletGBuffer, 1u, "dynamic-scene-meshlet=1"),
        makeStep(Action::MeshletTaskShader, 1u, "dynamic-scene-backend=task"),
        makeStep(Action::DynamicRayTracingUpdate, 1u, "dynamic-scene-task-update=1"),
        makeStep(Action::Wait, 0u, "dynamic-scene-task-warmup=1"),
        makeStep(Action::Wait, 0u, "dynamic-scene-task-warmup=2"),
        makeStep(Action::Wait, 0u, "dynamic-scene-task-warmup=3"),
        makeStep(Action::VerifyDynamicRayTracingUpdate, 0u, "dynamic-scene-task-verify"),
        makeStep(Action::DynamicRayTracingUpdate, 0u, "dynamic-scene-task-update=0"),
        makeStep(Action::Wait, 0u, "dynamic-scene-task-restore"),
        makeStep(Action::VerifyDynamicRayTracingUpdate, 1u, "dynamic-scene-task-restore-verify"),
        makeStep(Action::MeshletTaskShader, 0u, "dynamic-scene-backend=indirect"),
        makeStep(Action::DynamicRayTracingUpdate, 1u, "dynamic-scene-indirect-update=1"),
        makeStep(Action::Wait, 0u, "dynamic-scene-indirect-warmup=1"),
        makeStep(Action::Wait, 0u, "dynamic-scene-indirect-warmup=2"),
        makeStep(Action::Wait, 0u, "dynamic-scene-indirect-warmup=3"),
        makeStep(Action::VerifyDynamicRayTracingUpdate, 0u, "dynamic-scene-indirect-verify"),
        makeStep(Action::DynamicRayTracingUpdate, 0u, "dynamic-scene-indirect-update=0"),
        makeStep(Action::Wait, 0u, "dynamic-scene-indirect-restore"),
        makeStep(Action::VerifyDynamicRayTracingUpdate, 1u, "dynamic-scene-indirect-restore-verify"),
        makeStep(Action::VerifyDynamicSkinnedMeshCapability, 0u, "dynamic-scene-skinned-explicit-reject"),
    };
//Modify Begin:2026-08-25 by Hui
    testSuites.OIDN = {
        makeStep(Action::Denoiser, static_cast<uint32_t>(DenoiserController::Algorithm::OIDN), "denoiser=oidn"),
        makeStep(Action::Accumulation, 0u, "oidn-manual-accumulation=0"),
        makeStep(Action::OIDNStaticSpp, 2u, "oidn-static-spp=2"),
        makeStep(Action::Wait, 0u, "oidn-warmup=1"),
        makeStep(Action::Wait, 0u, "oidn-warmup=2"),
        makeStep(Action::Wait, 0u, "oidn-warmup=3"),
        makeStep(Action::Wait, 0u, "oidn-warmup=4"),
        makeStep(Action::Wait, 0u, "oidn-warmup=5"),
        makeStep(Action::Wait, 0u, "oidn-warmup=6"),
        makeStep(Action::Wait, 0u, "oidn-warmup=7"),
        makeStep(Action::Wait, 0u, "oidn-warmup=8"),
        makeStep(Action::VerifyOIDNResult, 0u, "oidn-result-verify"),
        makeStep(Action::Wait, 0u, "oidn-result-held=1"),
        makeStep(Action::Wait, 0u, "oidn-result-held=2"),
        makeStep(Action::VerifyOIDNResult, 0u, "oidn-result-held-verify"),
        makeStep(Action::OIDNCameraMotion, 0u, "oidn-camera-motion"),
        makeStep(Action::VerifyOIDNInvalidated, 0u, "oidn-camera-motion-verify"),
        makeStep(Action::Wait, 0u, "oidn-rebuild-warmup=1"),
        makeStep(Action::Wait, 0u, "oidn-rebuild-warmup=2"),
        makeStep(Action::Wait, 0u, "oidn-rebuild-warmup=3"),
        makeStep(Action::Wait, 0u, "oidn-rebuild-warmup=4"),
        makeStep(Action::Wait, 0u, "oidn-rebuild-warmup=5"),
        makeStep(Action::Wait, 0u, "oidn-rebuild-warmup=6"),
        makeStep(Action::Wait, 0u, "oidn-rebuild-warmup=7"),
        makeStep(Action::Wait, 0u, "oidn-rebuild-warmup=8"),
        makeStep(Action::VerifyOIDNResult, 0u, "oidn-rebuild-result-verify"),
        makeStep(Action::Denoiser, static_cast<uint32_t>(DenoiserController::Algorithm::NRD), "denoiser=nrd"),
    };
//Modify End
    testSuites.Visual = {
        makeStep(Action::DLSS, static_cast<uint32_t>(DLSSMode::Disabled), "visual-dlss=off"),
        makeStep(Action::MaterialShading, static_cast<uint32_t>(MaterialShadingModel::Pbr), "visual-shading=pbr"),
        makeStep(Action::PathTracingBackend, static_cast<uint32_t>(PathTracingBackend::InlineRayQuery), "visual-backend=inline"),
        makeStep(Action::AsyncCompute, 0u, "visual-async=0"),
        makeStep(Action::ParallelDirectCommandRecording, 1u, "visual-parallelrecording=1"),
        makeStep(Action::SoftShadows, 0u, "visual-soft=0"),
        makeStep(Action::Skybox, 1u, "visual-skybox=1"),
        makeStep(Action::Accumulation, 0u, "visual-accumulation=0"),

        makeStep(Action::DirectLighting, static_cast<uint32_t>(RaytracingDemoLightingTechnique::PathTracing), "visual-pt-direct-direct=pathtracing"),
        makeStep(Action::IndirectLighting, static_cast<uint32_t>(RaytracingDemoLightingTechnique::None), "visual-pt-direct-indirect=none"),
        makeStep(Action::MaxBounces, 1u, "visual-pt-direct-bounces=1"),
        makeStep(Action::Wait, 0u, "visual-pt-direct-warmup=1"),
        makeStep(Action::Wait, 0u, "visual-pt-direct-warmup=2"),
        makeStep(Action::VerifyActiveRayTracedPixelCount, 0u, "visual-pt-direct-active-pixels"),
        makeStep(Action::CaptureScreenshot, static_cast<uint32_t>(ScreenshotCapture::PathTracingDirect), "visual-capture=pt-direct"),

        makeStep(Action::DirectLighting, static_cast<uint32_t>(RaytracingDemoLightingTechnique::None), "visual-pt-indirect-direct=none"),
        makeStep(Action::IndirectLighting, static_cast<uint32_t>(RaytracingDemoLightingTechnique::PathTracing), "visual-pt-indirect-indirect=pathtracing"),
        makeStep(Action::MaxBounces, 3u, "visual-pt-indirect-bounces=3"),
        makeStep(Action::Wait, 0u, "visual-pt-indirect-warmup=1"),
        makeStep(Action::Wait, 0u, "visual-pt-indirect-warmup=2"),
        makeStep(Action::VerifyActiveRayTracedPixelCount, 0u, "visual-pt-indirect-active-pixels"),
        makeStep(Action::CaptureScreenshot, static_cast<uint32_t>(ScreenshotCapture::PathTracingIndirect), "visual-capture=pt-indirect"),

        makeStep(Action::DirectLighting, static_cast<uint32_t>(RaytracingDemoLightingTechnique::ReSTIRDI), "visual-restirdi-direct=restirdi"),
        makeStep(Action::IndirectLighting, static_cast<uint32_t>(RaytracingDemoLightingTechnique::None), "visual-restirdi-indirect=none"),
        makeStep(Action::MaxBounces, 1u, "visual-restirdi-bounces=1"),
        makeStep(Action::Wait, 0u, "visual-restirdi-warmup=1"),
        makeStep(Action::Wait, 0u, "visual-restirdi-warmup=2"),
        makeStep(Action::VerifyActiveRayTracedPixelCount, 0u, "visual-restirdi-active-pixels"),
        makeStep(Action::CaptureScreenshot, static_cast<uint32_t>(ScreenshotCapture::ReSTIRDI), "visual-capture=restirdi"),

        makeStep(Action::DirectLighting, static_cast<uint32_t>(RaytracingDemoLightingTechnique::None), "visual-restirgi-direct=none"),
        makeStep(Action::IndirectLighting, static_cast<uint32_t>(RaytracingDemoLightingTechnique::ReSTIRGI), "visual-restirgi-indirect=restirgi"),
        makeStep(Action::MaxBounces, 3u, "visual-restirgi-bounces=3"),
        makeStep(Action::Wait, 0u, "visual-restirgi-warmup=1"),
        makeStep(Action::Wait, 0u, "visual-restirgi-warmup=2"),
        makeStep(Action::Wait, 0u, "visual-restirgi-warmup=3"),
        makeStep(Action::VerifyActiveRayTracedPixelCount, 0u, "visual-restirgi-active-pixels"),
        makeStep(Action::CaptureScreenshot, static_cast<uint32_t>(ScreenshotCapture::ReSTIRGI), "visual-capture=restirgi"),

        makeStep(Action::DirectLighting, static_cast<uint32_t>(RaytracingDemoLightingTechnique::ReSTIRDI), "visual-default-direct=restirdi"),
        makeStep(Action::IndirectLighting, static_cast<uint32_t>(RaytracingDemoLightingTechnique::ReSTIRGI), "visual-default-indirect=restirgi"),
        makeStep(Action::MaxBounces, 3u, "visual-default-bounces=3"),
        makeStep(Action::Wait, 0u, "visual-default-warmup=1"),
        makeStep(Action::Wait, 0u, "visual-default-warmup=2"),
        makeStep(Action::Wait, 0u, "visual-default-warmup=3"),
        makeStep(Action::VerifyActiveRayTracedPixelCount, 0u, "visual-default-active-pixels"),
        makeStep(Action::CaptureScreenshot, static_cast<uint32_t>(ScreenshotCapture::ReSTIRDIAndGI), "visual-capture=restirdi-restirgi"),
    };
    testSuites.ReSTIRGIProfile = {
        makeStep(Action::GpuTiming, 1u, "timing=1"),
        makeStep(Action::TimingCapture, 1u, "timingcapture=1"),
        makeStep(Action::ReSTIRGIStageTiming, 1u, "restirgi-stage-timing=1"),
        makeStep(Action::DirectLighting, static_cast<uint32_t>(RaytracingDemoLightingTechnique::None), "direct=none"),
        makeStep(Action::IndirectLighting, static_cast<uint32_t>(RaytracingDemoLightingTechnique::ReSTIRGI), "indirect=restirgi"),
        makeStep(Action::MaxBounces, 2u, "bounces=2"),
        makeStep(Action::Wait, 0u, "warmup-bounces2=1"),
        makeStep(Action::Wait, 0u, "warmup-bounces2=2"),
        makeStep(Action::Wait, 0u, "warmup-bounces2=3"),
        makeStep(Action::Wait, 0u, "warmup-bounces2=4"),
        makeStep(Action::DumpTiming, 1u, "timingdump=bounces2"),
        makeStep(Action::MaxBounces, 3u, "bounces=3"),
        makeStep(Action::TimingCapture, 1u, "timingcapture=clear-bounces3"),
        makeStep(Action::Wait, 0u, "warmup-bounces3=1"),
        makeStep(Action::Wait, 0u, "warmup-bounces3=2"),
        makeStep(Action::Wait, 0u, "warmup-bounces3=3"),
        makeStep(Action::Wait, 0u, "warmup-bounces3=4"),
        makeStep(Action::DumpTiming, 1u, "timingdump=bounces3"),
        makeStep(Action::MaxBounces, 5u, "bounces=5"),
        makeStep(Action::TimingCapture, 1u, "timingcapture=clear-bounces5"),
        makeStep(Action::Wait, 0u, "warmup-bounces5=1"),
        makeStep(Action::Wait, 0u, "warmup-bounces5=2"),
        makeStep(Action::Wait, 0u, "warmup-bounces5=3"),
        makeStep(Action::Wait, 0u, "warmup-bounces5=4"),
        makeStep(Action::DumpTiming, 1u, "timingdump=bounces5"),
        makeStep(Action::ReSTIRGIStageTiming, 0u, "restirgi-stage-timing=0"),
        makeStep(Action::GpuTiming, 0u, "timing=0"),
    };
    testSuites.ReSTIRGIVariants = {
        makeStep(Action::GpuTiming, 1u, "timing=1"),
        makeStep(Action::TimingCapture, 1u, "timingcapture=1"),
        makeStep(Action::ReSTIRGIStageTiming, 1u, "restirgi-stage-timing=1"),
        makeStep(Action::DirectLighting, static_cast<uint32_t>(RaytracingDemoLightingTechnique::None), "direct=none"),
        makeStep(Action::IndirectLighting, static_cast<uint32_t>(RaytracingDemoLightingTechnique::ReSTIRGI), "indirect=restirgi"),
        makeStep(Action::MaxBounces, 2u, "bounces=2"),
        makeStep(Action::ReSTIRGITemporalResampling, 1u, "gi-temporal=1"),
        makeStep(Action::ReSTIRGISpatialResampling, 1u, "gi-spatial=1"),
        makeStep(Action::ReSTIRGITemporalJacobian, 1u, "gi-temporal-jacobian=1"),
        makeStep(Action::ReSTIRGISpatialRayTracedBiasCorrection, 0u, "gi-spatial-raytraced-bias=0"),
        makeStep(Action::Wait, 0u, "warmup-fast=1"),
        makeStep(Action::Wait, 0u, "warmup-fast=2"),
        makeStep(Action::ReSTIRGITemporalResampling, 0u, "gi-temporal=0"),
        makeStep(Action::Wait, 0u, "warmup-temporal-off=1"),
        makeStep(Action::ReSTIRGITemporalResampling, 1u, "gi-temporal=1"),
        makeStep(Action::ReSTIRGITemporalJacobian, 0u, "gi-temporal-jacobian=0"),
        makeStep(Action::Wait, 0u, "warmup-jacobian-off=1"),
        makeStep(Action::ReSTIRGITemporalJacobian, 1u, "gi-temporal-jacobian=1"),
        makeStep(Action::ReSTIRGISpatialResampling, 0u, "gi-spatial=0"),
        makeStep(Action::Wait, 0u, "warmup-spatial-off=1"),
        makeStep(Action::ReSTIRGISpatialResampling, 1u, "gi-spatial=1"),
        makeStep(Action::ReSTIRGISpatialRayTracedBiasCorrection, 1u, "gi-spatial-raytraced-bias=1"),
        makeStep(Action::Wait, 0u, "warmup-raytraced-bias=1"),
        makeStep(Action::Wait, 0u, "warmup-raytraced-bias=2"),
        makeStep(Action::Wait, 0u, "warmup-raytraced-bias=3"),
        makeStep(Action::Wait, 0u, "warmup-raytraced-bias=4"),
        makeStep(Action::ReSTIRGISpatialRayTracedBiasCorrection, 0u, "gi-spatial-raytraced-bias=0"),
        makeStep(Action::Wait, 0u, "warmup-restored=1"),
        makeStep(Action::MaxBounces, 0u, "bounces=0-clamped"),
        makeStep(Action::Wait, 0u, "warmup-bounces0-clamped=1"),
        makeStep(Action::MaxBounces, 3u, "bounces=3-restored"),
        makeStep(Action::Wait, 0u, "warmup-bounces3-restored=1"),
        makeStep(Action::MaxBounces, 2u, "bounces=2-restored"),
        makeStep(Action::Wait, 0u, "warmup-bounces2-restored=1"),
        makeStep(Action::DumpTiming, 1u, "timingdump=restirgi-variants"),
        makeStep(Action::ReSTIRGIStageTiming, 0u, "restirgi-stage-timing=0"),
        makeStep(Action::GpuTiming, 0u, "timing=0"),
    };

    for (uint32_t config = 0u; config < (1u << 10u); ++config)
    {
        testSuites.ReSTIRDIVariants.push_back(makeStep(
            Action::ReSTIRDIConfig,
            config,
            "restirdi-config=" + std::to_string(config)));
    }

    const std::vector<MatrixCase>& matrixCases = GetMatrixCases();
    testSuites.Matrix.reserve(matrixCases.size());
    for (uint32_t caseIndex = 0; caseIndex < matrixCases.size(); ++caseIndex)
    {
        testSuites.Matrix.push_back(makeStep(Action::MatrixCase, caseIndex, matrixCases[caseIndex].Name));
    }
    return testSuites;
}
//Modify End
