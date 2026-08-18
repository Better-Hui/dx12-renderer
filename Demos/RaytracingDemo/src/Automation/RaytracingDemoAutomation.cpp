//Modify Begin:2026-08-18 by Hui
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
        return DemoAutomation::Step{ static_cast<uint32_t>(action), value, std::move(name) };
    };

    DemoAutomation::TestSuites testSuites;
    testSuites.Core = {
        makeStep(Action::GpuTiming, 1u, "timing=1"),
        makeStep(Action::TimingCapture, 1u, "timingcapture=1"),
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
