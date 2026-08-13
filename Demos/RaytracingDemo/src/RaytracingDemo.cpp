#include <RaytracingDemo.h>

#include <DX12Library/Application.h>
#include <DX12Library/CommandList.h>
#include <DX12Library/CommandQueue.h>
#include <DX12Library/Events.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/Window.h>

#include <Framework/Core/GraphicsSettings.h>
#include <Framework/Geometry/Mesh.h>
#include <Framework/Geometry/ModelLoader.h>
#include <Framework/Rendering/Pipeline/RasterPipelineStateBuilder.h>
#include <Framework/Rendering/Pipeline/ShaderBlob.h>
//Modify Begin:2026-07-30 by BestHui
#include <Framework/Rendering/Pipeline/ShaderTargetProfile.h>
//Modify End
//Modify Begin:2026-07-30 by BestHui
#include <Framework/Scene/SceneImporter.h>
//Modify End

#include <RenderGraph/RaytracingDemoGraphResources.h>
#include <RenderGraph/RenderMetadata.h>

#include <DirectXMath.h>
#include <d3dx12.h>
#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <RenderGraph/RaytracingDemoRenderGraphBuilder.h>

#if defined(min)
#undef min
#endif

#if defined(max)
#undef max
#endif

using namespace DirectX;

namespace
{
    //Modify Begin:2026-07-30 by BestHui
    FrameworkDeviceContext CreateFrameworkDeviceContext(Application& application)
    {
        FrameworkDeviceContextDesc desc;
//Modify Begin:2026-08-07 by BestHui
        desc.DeviceContext = application.GetD3D12DeviceContext();
//Modify End
        desc.DirectQueue = application.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
        desc.ComputeQueue = application.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE);
        desc.CopyQueue = application.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COPY);
        desc.FrameFeatures = application.GetFrameFeaturesRuntime();
        desc.FrameGeneration = &application;
        return FrameworkDeviceContext(std::move(desc));
    }
    //Modify End

    uint32_t ComputeDescriptorArrayCapacity(const size_t resourceCount, const size_t resourceCapacity)
    {
        return static_cast<uint32_t>(std::max<size_t>(
            RayTracingSceneResourceLayout::MinDescriptorArrayCapacity,
            std::max(resourceCount, resourceCapacity)));
    }

//Modify Begin:2026-08-07 by BestHui
    DLSSMode ParseDLSSMode(const char* value)
    {
        if (value == nullptr || std::strcmp(value, "0") == 0 || std::strcmp(value, "off") == 0)
        {
            return DLSSMode::Disabled;
        }
        if (std::strcmp(value, "dlaa") == 0)
        {
            return DLSSMode::DLAA;
        }
        if (std::strcmp(value, "balanced") == 0)
        {
            return DLSSMode::Balanced;
        }
        if (std::strcmp(value, "performance") == 0)
        {
            return DLSSMode::Performance;
        }
        if (std::strcmp(value, "ultra-performance") == 0)
        {
            return DLSSMode::UltraPerformance;
        }
        return DLSSMode::Quality;
    }
//Modify End

//Modify Begin:2026-08-12 by BestHui
    MaterialShadingModel ParseMaterialShadingModel(const char* value)
    {
        if (value != nullptr &&
            (std::strcmp(value, "stylized-comic") == 0 ||
                std::strcmp(value, "stylized") == 0))
        {
            return MaterialShadingModel::StylizedComic;
        }

        return MaterialShadingModel::Pbr;
    }
//Modify End

//Modify Begin:2026-08-10 by BestHui
    bool TryGetEnvironmentBoolean(const char* variableName, bool& value)
    {
        char* environmentValue = nullptr;
        size_t environmentValueLength = 0;
        _dupenv_s(&environmentValue, &environmentValueLength, variableName);
        if (environmentValue == nullptr)
        {
            return false;
        }

        value = std::strcmp(environmentValue, "0") != 0;
        std::free(environmentValue);
        return true;
    }

    bool TryGetEnvironmentLightingTechnique(
        const char* variableName,
        RaytracingDemoLightingTechnique& technique)
    {
        char* environmentValue = nullptr;
        size_t environmentValueLength = 0;
        _dupenv_s(&environmentValue, &environmentValueLength, variableName);
        if (environmentValue == nullptr)
        {
            return false;
        }

        const std::string value(environmentValue);
        std::free(environmentValue);
        if (value == "none")
        {
            technique = RaytracingDemoLightingTechnique::None;
            return true;
        }
        if (value == "pathtracing")
        {
            technique = RaytracingDemoLightingTechnique::PathTracing;
            return true;
        }
        if (value == "restirdi")
        {
            technique = RaytracingDemoLightingTechnique::ReSTIRDI;
            return true;
        }
        if (value == "restirgi")
        {
            technique = RaytracingDemoLightingTechnique::ReSTIRGI;
            return true;
        }
        return false;
    }
//Modify End

//Modify Begin:2026-08-03 by BestHui
    std::filesystem::path GetScenePath()
    {
        char* scenePath = nullptr;
        size_t scenePathLength = 0;
        _dupenv_s(&scenePath, &scenePathLength, "RAYTRACING_DEMO_SCENE");
        if (scenePath != nullptr)
        {
            std::filesystem::path result(scenePath);
            std::free(scenePath);
            scenePath = nullptr;
            if (!result.empty() && std::filesystem::exists(result))
            {
                return result;
            }
            throw std::runtime_error("RAYTRACING_DEMO_SCENE is set but the scene file does not exist.");
        }
        std::free(scenePath);

        _dupenv_s(&scenePath, &scenePathLength, "RAYTRACING_DEMO_UNITY_SCENE");
        if (scenePath != nullptr)
        {
            std::filesystem::path result(scenePath);
            std::free(scenePath);
            if (!result.empty() && std::filesystem::exists(result))
            {
                return result;
            }
            throw std::runtime_error("RAYTRACING_DEMO_UNITY_SCENE is set but the scene file does not exist.");
        }
        std::free(scenePath);

        constexpr const char* DefaultSponzaRelativePath =
            "Assets/Scenes/Sponza.unity";
        std::filesystem::path searchDirectory = std::filesystem::current_path();
        while (!searchDirectory.empty())
        {
            const std::filesystem::path defaultScene = searchDirectory / DefaultSponzaRelativePath;
            if (std::filesystem::exists(defaultScene))
            {
                return defaultScene;
            }

            const std::filesystem::path parentDirectory = searchDirectory.parent_path();
            if (parentDirectory == searchDirectory)
            {
                break;
            }
            searchDirectory = parentDirectory;
        }

        throw std::runtime_error("Default Sponza scene file does not exist. Set RAYTRACING_DEMO_SCENE to a .json or .unity file.");
    }
//Modify End

//Modify Begin:2026-08-07 by BestHui
    enum class RuntimeAutomationAction : uint32_t
    {
        SoftShadows,
        StressSpheres,
        MeshletGBuffer,
        MeshletTaskShader,
        PathTracingBackend,
        DirectLighting,
        IndirectLighting,
        AsyncCompute,
        ParallelDirectCommandRecording,
        Skybox,
        Accumulation,
        GpuTiming,
        TimingCapture,
//Modify Begin:2026-08-11 by BestHui
        ReSTIRGIStageTiming,
        ReSTIRGITemporalResampling,
        ReSTIRGISpatialResampling,
        ReSTIRGITemporalJacobian,
        ReSTIRGISpatialRayTracedBiasCorrection,
//Modify End
        DumpTiming,
//Modify Begin:2026-08-07 by BestHui
        DLSS,
//Modify End
//Modify Begin:2026-08-12 by BestHui
        MaterialShading,
//Modify End
//Modify Begin:2026-07-30 by BestHui
        MaxBounces,
        Wait,
//Modify End
        MatrixCase
    };

    struct RuntimeAutomationMatrixCase
    {
        PathTracingBackend Backend = PathTracingBackend::InlineRayQuery;
        RaytracingDemoLightingTechnique DirectLighting = RaytracingDemoLightingTechnique::None;
        RaytracingDemoLightingTechnique IndirectLighting = RaytracingDemoLightingTechnique::None;
        bool AsyncCompute = false;
        bool ParallelDirectCommandRecording = true;
        bool UseMeshletGBuffer = false;
        bool UseTaskShaderMeshlets = true;
        bool SoftShadows = false;
        bool StressSpheres = false;
        bool Skybox = false;
        bool Accumulation = false;
        DLSSMode DlssMode = DLSSMode::Disabled;
//Modify Begin:2026-08-12 by BestHui
        MaterialShadingModel ShadingModel = MaterialShadingModel::Pbr;
//Modify End
//Modify Begin:2026-07-30 by BestHui
        int MaxBounces = 1;
//Modify End
        std::string Name;
    };

    const char* GetRuntimeAutomationBackendName(const PathTracingBackend backend)
    {
        return backend == PathTracingBackend::InlineRayQuery ? "inline" : "dxr";
    }

    const char* GetRuntimeAutomationLightingName(const RaytracingDemoLightingTechnique technique)
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

    const char* GetRuntimeAutomationDLSSModeName(const DLSSMode mode)
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

//Modify Begin:2026-08-12 by BestHui
    const char* GetRuntimeAutomationMaterialShadingName(const MaterialShadingModel shadingModel)
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
//Modify End

    const std::vector<RuntimeAutomationMatrixCase>& GetRuntimeAutomationMatrixCases()
    {
        static const std::vector<RuntimeAutomationMatrixCase> cases = []()
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
//Modify Begin:2026-08-12 by BestHui
            const std::vector<MaterialShadingModel> materialShadingModels = {
                MaterialShadingModel::Pbr,
                MaterialShadingModel::StylizedComic,
            };
//Modify End

            std::vector<RuntimeAutomationMatrixCase> result;
            uint32_t caseIndex = 1;
//Modify Begin:2026-08-12 by BestHui
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

                                            for (const bool parallelDirectCommandRecording : { false, true })
                                            {
                                                for (const bool skybox : { false, true })
                                                {
                                                    for (const bool accumulation : { false, true })
                                                    {
                                                        for (const DLSSMode dlssMode : dlssModes)
                                                        {
                                                            RuntimeAutomationMatrixCase testCase;
                                                            testCase.Backend = backend;
                                                            testCase.DirectLighting = directLighting;
                                                            testCase.IndirectLighting = indirectLighting;
                                                            testCase.AsyncCompute = asyncCompute;
                                                            testCase.ParallelDirectCommandRecording = parallelDirectCommandRecording;
                                                            testCase.UseMeshletGBuffer = meshletMode.Enabled;
                                                            testCase.UseTaskShaderMeshlets = meshletMode.TaskShader;
                                                            testCase.SoftShadows = softShadows;
                                                            testCase.StressSpheres = stressSpheres;
                                                            testCase.Skybox = skybox;
                                                            testCase.Accumulation = accumulation;
                                                            testCase.DlssMode = dlssMode;
                                                            testCase.ShadingModel = shadingModel;
//Modify Begin:2026-07-30 by BestHui
                                                            testCase.MaxBounces = indirectLighting == RaytracingDemoLightingTechnique::None ? 1 : 3;
//Modify End
                                                            testCase.Name =
                                                                "matrix#" + std::to_string(caseIndex++) +
                                                                " shading=" + GetRuntimeAutomationMaterialShadingName(shadingModel) +
                                                                " backend=" + GetRuntimeAutomationBackendName(backend) +
                                                                " meshlet=" + meshletMode.Name +
                                                                " direct=" + GetRuntimeAutomationLightingName(directLighting) +
                                                                " indirect=" + GetRuntimeAutomationLightingName(indirectLighting) +
                                                                " async=" + std::to_string(asyncCompute) +
                                                                " parallelrecording=" + std::to_string(parallelDirectCommandRecording) +
                                                                " soft=" + std::to_string(softShadows) +
                                                                " stress=" + std::to_string(stressSpheres) +
                                                                " skybox=" + std::to_string(skybox) +
                                                                " accumulation=" + std::to_string(accumulation) +
//Modify Begin:2026-07-30 by BestHui
                                                                " bounces=" + std::to_string(testCase.MaxBounces) +
//Modify End
                                                                " dlss=" + GetRuntimeAutomationDLSSModeName(dlssMode);
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
//Modify End
            return result;
        }();
        return cases;
    }

    DemoAutomation::TestSuites CreateRuntimeAutomationTestSuites()
    {
        using Action = RuntimeAutomationAction;
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
//Modify Begin:2026-08-12 by BestHui
            makeStep(Action::MaterialShading, static_cast<uint32_t>(MaterialShadingModel::Pbr), "shading=pbr"),
//Modify End
//Modify Begin:2026-08-07 by BestHui
            makeStep(Action::DLSS, static_cast<uint32_t>(DLSSMode::Disabled), "dlss=off"),
            makeStep(Action::DLSS, static_cast<uint32_t>(DLSSMode::DLAA), "dlss=dlaa"),
            makeStep(Action::DLSS, static_cast<uint32_t>(DLSSMode::Quality), "dlss=quality"),
            makeStep(Action::DLSS, static_cast<uint32_t>(DLSSMode::Balanced), "dlss=balanced"),
            makeStep(Action::DLSS, static_cast<uint32_t>(DLSSMode::Performance), "dlss=performance"),
            makeStep(Action::DLSS, static_cast<uint32_t>(DLSSMode::UltraPerformance), "dlss=ultra-performance"),
//Modify End
            makeStep(Action::MeshletTaskShader, 0u, "meshletbackend=indirect"),
            makeStep(Action::MeshletTaskShader, 1u, "meshletbackend=task"),
            makeStep(Action::DirectLighting, static_cast<uint32_t>(RaytracingDemoLightingTechnique::PathTracing), "direct=pathtracing"),
            makeStep(Action::PathTracingBackend, static_cast<uint32_t>(PathTracingBackend::ShaderTableDxr), "backend=dxr"),
            makeStep(Action::PathTracingBackend, static_cast<uint32_t>(PathTracingBackend::InlineRayQuery), "backend=inline"),
//Modify Begin:2026-07-30 by BestHui
            makeStep(Action::MaxBounces, 3u, "bounces=3"),
//Modify End
            makeStep(Action::IndirectLighting, static_cast<uint32_t>(RaytracingDemoLightingTechnique::PathTracing), "indirect=pathtracing"),
//Modify Begin:2026-08-12 by BestHui
            makeStep(Action::MaterialShading, static_cast<uint32_t>(MaterialShadingModel::StylizedComic), "shading=stylized-comic"),
            makeStep(Action::Wait, 0u, "shading=stylized-comic-warmup"),
//Modify End
            makeStep(Action::IndirectLighting, static_cast<uint32_t>(RaytracingDemoLightingTechnique::ReSTIRGI), "indirect=restirgi"),
//Modify Begin:2026-08-11 by BestHui
            makeStep(Action::ReSTIRGIStageTiming, 1u, "restirgi-stage-timing=1"),
            makeStep(Action::Wait, 0u, "restirgi-stage-timing-warmup"),
            makeStep(Action::DumpTiming, 1u, "timingdump=restirgi"),
            makeStep(Action::ReSTIRGIStageTiming, 0u, "restirgi-stage-timing=0"),
//Modify End
            makeStep(Action::ParallelDirectCommandRecording, 0u, "parallelrecording=0"),
            makeStep(Action::ParallelDirectCommandRecording, 1u, "parallelrecording=1"),
            makeStep(Action::AsyncCompute, 1u, "async=1"),
            makeStep(Action::AsyncCompute, 0u, "async=0"),
            makeStep(Action::IndirectLighting, static_cast<uint32_t>(RaytracingDemoLightingTechnique::None), "indirect=none"),
//Modify Begin:2026-07-30 by BestHui
            makeStep(Action::MaxBounces, 1u, "bounces=1"),
//Modify End
            makeStep(Action::DirectLighting, static_cast<uint32_t>(RaytracingDemoLightingTechnique::ReSTIRDI), "direct=restirdi"),
//Modify Begin:2026-08-12 by BestHui
            makeStep(Action::Wait, 0u, "restirdi-stylized-comic-warmup"),
            makeStep(Action::MaterialShading, static_cast<uint32_t>(MaterialShadingModel::Pbr), "shading=pbr"),
//Modify End
            makeStep(Action::Skybox, 0u, "skybox=0"),
            makeStep(Action::Skybox, 1u, "skybox=1"),
            makeStep(Action::Accumulation, 0u, "accumulation=0"),
            makeStep(Action::Accumulation, 1u, "accumulation=1"),
            makeStep(Action::DumpTiming, 1u, "timingdump=1"),
            makeStep(Action::GpuTiming, 0u, "timing=0")
        };
        testSuites.Stress = {
            makeStep(Action::StressSpheres, 1u, "stress=1"),
            makeStep(Action::StressSpheres, 0u, "stress=0"),
            makeStep(Action::StressSpheres, 1u, "stress=1"),
            makeStep(Action::StressSpheres, 0u, "stress=0")
        };
//Modify Begin:2026-07-30 by BestHui
        testSuites.ReSTIRGIProfile = {
            makeStep(Action::GpuTiming, 1u, "timing=1"),
            makeStep(Action::TimingCapture, 1u, "timingcapture=1"),
//Modify Begin:2026-08-11 by BestHui
            makeStep(Action::ReSTIRGIStageTiming, 1u, "restirgi-stage-timing=1"),
//Modify End
            makeStep(Action::DirectLighting, static_cast<uint32_t>(RaytracingDemoLightingTechnique::None), "direct=none"),
            makeStep(Action::IndirectLighting, static_cast<uint32_t>(RaytracingDemoLightingTechnique::ReSTIRGI), "indirect=restirgi"),
            makeStep(Action::MaxBounces, 2u, "bounces=2"),
            makeStep(Action::Wait, 0u, "warmup-bounces2=1"),
            makeStep(Action::Wait, 0u, "warmup-bounces2=2"),
            makeStep(Action::Wait, 0u, "warmup-bounces2=3"),
            makeStep(Action::Wait, 0u, "warmup-bounces2=4"),
            makeStep(Action::DumpTiming, 1u, "timingdump=bounces2"),
//Modify Begin:2026-08-11 by BestHui
            makeStep(Action::MaxBounces, 3u, "bounces=3"),
            makeStep(Action::TimingCapture, 1u, "timingcapture=clear-bounces3"),
            makeStep(Action::Wait, 0u, "warmup-bounces3=1"),
            makeStep(Action::Wait, 0u, "warmup-bounces3=2"),
            makeStep(Action::Wait, 0u, "warmup-bounces3=3"),
            makeStep(Action::Wait, 0u, "warmup-bounces3=4"),
            makeStep(Action::DumpTiming, 1u, "timingdump=bounces3"),
//Modify End
            makeStep(Action::MaxBounces, 5u, "bounces=5"),
//Modify Begin:2026-08-11 by BestHui
            makeStep(Action::TimingCapture, 1u, "timingcapture=clear-bounces5"),
//Modify End
            makeStep(Action::Wait, 0u, "warmup-bounces5=1"),
            makeStep(Action::Wait, 0u, "warmup-bounces5=2"),
            makeStep(Action::Wait, 0u, "warmup-bounces5=3"),
            makeStep(Action::Wait, 0u, "warmup-bounces5=4"),
            makeStep(Action::DumpTiming, 1u, "timingdump=bounces5"),
//Modify Begin:2026-08-11 by BestHui
            makeStep(Action::ReSTIRGIStageTiming, 0u, "restirgi-stage-timing=0"),
//Modify End
            makeStep(Action::GpuTiming, 0u, "timing=0"),
        };
//Modify End
//Modify Begin:2026-08-11 by BestHui
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
//Modify Begin:2026-08-11 by BestHui
            makeStep(Action::MaxBounces, 0u, "bounces=0-clamped"),
            makeStep(Action::Wait, 0u, "warmup-bounces0-clamped=1"),
            makeStep(Action::MaxBounces, 3u, "bounces=3-restored"),
            makeStep(Action::Wait, 0u, "warmup-bounces3-restored=1"),
            makeStep(Action::MaxBounces, 2u, "bounces=2-restored"),
            makeStep(Action::Wait, 0u, "warmup-bounces2-restored=1"),
//Modify End
            makeStep(Action::DumpTiming, 1u, "timingdump=restirgi-variants"),
            makeStep(Action::ReSTIRGIStageTiming, 0u, "restirgi-stage-timing=0"),
            makeStep(Action::GpuTiming, 0u, "timing=0"),
        };
//Modify End
        const auto& matrixCases = GetRuntimeAutomationMatrixCases();
        testSuites.Matrix.reserve(matrixCases.size());
        for (uint32_t caseIndex = 0; caseIndex < matrixCases.size(); ++caseIndex)
        {
            testSuites.Matrix.push_back(makeStep(Action::MatrixCase, caseIndex, matrixCases[caseIndex].Name));
        }
        return testSuites;
    }
//Modify End

    XMFLOAT3 RotateCameraVector(const XMVECTOR rotation, const XMFLOAT3& value)
    {
        const XMVECTOR vector = XMVectorSet(value.x, value.y, value.z, 0.0f);
        XMFLOAT3 result{};
        XMStoreFloat3(&result, XMVector3Rotate(vector, rotation));
        return result;
    }

    void ApplySceneCamera(Camera& camera, const SceneCamera& sceneCamera, const int width, const int height)
    {
        if (sceneCamera.RuntimeCamera != nullptr)
        {
            camera.SetTranslation(sceneCamera.RuntimeCamera->GetTranslation());
            camera.SetRotation(sceneCamera.RuntimeCamera->GetRotation());
        }
        camera.SetProjection(sceneCamera.FieldOfView, static_cast<float>(width) / static_cast<float>(height), sceneCamera.NearClipPlane, sceneCamera.FarClipPlane);
    }

//Modify Begin:2026-07-29 by BestHui
    void CalculateCameraControllerFromLookDirection(
        const XMVECTOR lookDirection,
        float& yaw,
        float& pitch)
    {
        const XMVECTOR forward = XMVector3Normalize(lookDirection);
        yaw = XMConvertToDegrees(std::atan2(XMVectorGetX(forward), XMVectorGetZ(forward)));
        pitch = -XMConvertToDegrees(std::asin(std::clamp(XMVectorGetY(forward), -1.0f, 1.0f)));
    }
//Modify End

}

RaytracingDemo::RaytracingDemo(const std::wstring& name, const int width, const int height, GraphicsSettings graphicsSettings)
    : Base(name, width, height, false)
    , m_Width(width)
    , m_Height(height)
    , m_FrameworkDeviceContext(CreateFrameworkDeviceContext(Application::Get()))
//Modify Begin:2026-08-07 by BestHui
    , m_DLSS(m_FrameworkDeviceContext)
//Modify End
    , m_PathTracingPipelines(m_FrameworkDeviceContext)
//Modify Begin:2026-07-30 by BestHui
    , m_DirectLightingReSTIRDIPass(
        m_FrameworkDeviceContext,
        {
            L"Demos/RaytracingDemo/shaders/ReSTIRDI/ReSTIRDI.RIS.cs.hlsl",
            L"Demos/RaytracingDemo/shaders/ReSTIRDI/ReSTIRDI.Temporal.cs.hlsl",
            L"Demos/RaytracingDemo/shaders/ReSTIRDI/ReSTIRDI.Spatial.cs.hlsl",
            L"Demos/RaytracingDemo/shaders/ReSTIRDI/ReSTIRDI.Shade.cs.hlsl",
            { { "RAYTRACING_DEMO_SOFT_SHADOWS", "1" } },
//Modify Begin:2026-08-06 by BestHui
            "RAYTRACING_DEMO_ENVIRONMENT_PROJECTION",
//Modify End
//Modify Begin:2026-08-12 by BestHui
            { PipelineStaticSamplers::LinearWrap(1u) },
//Modify End
        })
//Modify End
//Modify Begin:2026-08-10 by BestHui
    , m_IndirectLightingReSTIRGIPass(
        m_FrameworkDeviceContext,
        {
            L"Demos/RaytracingDemo/shaders/ReSTIRGI/ReSTIRGI.Initial.cs.hlsl",
            L"Demos/RaytracingDemo/shaders/ReSTIRGI/ReSTIRGI.Temporal.cs.hlsl",
            L"Demos/RaytracingDemo/shaders/ReSTIRGI/ReSTIRGI.Spatial.cs.hlsl",
            L"Demos/RaytracingDemo/shaders/ReSTIRGI/ReSTIRGI.Shade.cs.hlsl",
            { { "RAYTRACING_DEMO_SOFT_SHADOWS", "1" } },
            "RAYTRACING_DEMO_ENVIRONMENT_PROJECTION",
//Modify Begin:2026-08-12 by BestHui
            { PipelineStaticSamplers::LinearWrap(1u) },
//Modify End
        })
//Modify End
//Modify Begin:2026-07-30 by BestHui
    , m_CudaBloom(m_FrameworkDeviceContext)
//Modify Begin:2026-07-30 by BestHui
    , m_SceneResources(m_FrameworkDeviceContext.GetD3D12DeviceContext())
//Modify End
//Modify End
    , m_Lights(m_FrameworkDeviceContext)
{
    (void)graphicsSettings;

    const XMVECTOR cameraPos = XMVectorSet(0, 8, -35, 1);
    const XMVECTOR cameraTarget = XMVectorSet(0, 5, 18, 1);
    const XMVECTOR cameraUp = XMVectorSet(0, 1, 0, 0);
    GetSceneCamera().SetLookAt(cameraPos, cameraTarget, cameraUp);
//Modify Begin:2026-07-28 by BestHui
    const XMVECTOR initialForward = XMVector3Normalize(cameraTarget - cameraPos);
    m_CameraController.Yaw = XMConvertToDegrees(std::atan2(XMVectorGetX(initialForward), XMVectorGetZ(initialForward)));
    m_CameraController.Pitch = XMConvertToDegrees(std::asin(std::clamp(XMVectorGetY(initialForward), -1.0f, 1.0f)));
//Modify End
    GetSceneCamera().SetProjection(m_CameraFov, static_cast<float>(m_Width) / static_cast<float>(m_Height), 0.1f, 1000.0f);

    char* mode = nullptr;
    size_t modeLength = 0;
    _dupenv_s(&mode, &modeLength, "RAYTRACING_DEMO_MODE");
    if (mode != nullptr && std::strcmp(mode, "shader-table") == 0)
    {
        m_PathTracingBackend = PathTracingBackend::ShaderTableDxr;
    }
    std::free(mode);

//Modify Begin:2026-08-10 by BestHui
    char* bounceCount = nullptr;
    size_t bounceCountLength = 0;
    _dupenv_s(&bounceCount, &bounceCountLength, "RAYTRACING_DEMO_BOUNCES");
    if (bounceCount != nullptr)
    {
        char* parseEnd = nullptr;
        const long parsedBounces = std::strtol(bounceCount, &parseEnd, 10);
        if (parseEnd != bounceCount && *parseEnd == '\0')
        {
            m_MaxBounces = std::clamp(static_cast<int>(parsedBounces), 1, 5);
        }
    }
    std::free(bounceCount);
//Modify End

//Modify Begin:2026-08-12 by BestHui
    char* materialShadingModel = nullptr;
    size_t materialShadingModelLength = 0;
    _dupenv_s(&materialShadingModel, &materialShadingModelLength, "RAYTRACING_DEMO_MATERIAL_SHADING");
    m_MaterialShadingModel = ParseMaterialShadingModel(materialShadingModel);
    std::free(materialShadingModel);
//Modify End

//Modify Begin:2026-08-10 by BestHui
    TryGetEnvironmentLightingTechnique("RAYTRACING_DEMO_DIRECT_LIGHTING", m_DirectLightingTechnique);
    TryGetEnvironmentLightingTechnique("RAYTRACING_DEMO_INDIRECT_LIGHTING", m_IndirectLightingTechnique);
    TryGetEnvironmentBoolean("RAYTRACING_DEMO_ACCUMULATION", m_AccumulationEnabled);

    ReSTIRGISettings restirGISettings = m_IndirectLightingReSTIRGI.GetSettings();
    bool restirGISettingsOverridden = false;
    restirGISettingsOverridden |= TryGetEnvironmentBoolean(
        "RAYTRACING_DEMO_RESTIRGI_TEMPORAL",
        restirGISettings.EnableTemporalResampling);
    restirGISettingsOverridden |= TryGetEnvironmentBoolean(
        "RAYTRACING_DEMO_RESTIRGI_SPATIAL",
        restirGISettings.EnableSpatialResampling);
    if (restirGISettingsOverridden)
    {
        m_IndirectLightingReSTIRGI.SetSettings(restirGISettings);
    }
//Modify End

//Modify Begin:2026-07-30 by BestHui
    char* softShadows = nullptr;
    size_t softShadowsLength = 0;
    _dupenv_s(&softShadows, &softShadowsLength, "RAYTRACING_DEMO_SOFT_SHADOWS");
    if (softShadows != nullptr)
    {
        m_SoftShadowsEnabled = std::strcmp(softShadows, "0") != 0;
    }
    std::free(softShadows);
//Modify End

    char* nrdMode = nullptr;
    size_t nrdModeLength = 0;
    _dupenv_s(&nrdMode, &nrdModeLength, "RAYTRACING_DEMO_NRD");
    if (nrdMode != nullptr)
    {
        m_Denoisers.SetAlgorithm(std::strcmp(nrdMode, "0") != 0 ? DenoiserController::Algorithm::NRD : DenoiserController::Algorithm::Off);
    }
    std::free(nrdMode);

    char* denoiserMode = nullptr;
    size_t denoiserModeLength = 0;
    _dupenv_s(&denoiserMode, &denoiserModeLength, "RAYTRACING_DEMO_DENOISER");
    if (denoiserMode != nullptr)
    {
        m_Denoisers.SetAlgorithmFromName(denoiserMode);
    }
    std::free(denoiserMode);

//Modify Begin:2026-08-07 by BestHui
    char* dlssMode = nullptr;
    size_t dlssModeLength = 0;
    _dupenv_s(&dlssMode, &dlssModeLength, "RAYTRACING_DEMO_DLSS");
    if (dlssMode != nullptr)
    {
        m_DLSS.SetMode(ParseDLSSMode(dlssMode));
    }
    std::free(dlssMode);

    char* rayReconstruction = nullptr;
    size_t rayReconstructionLength = 0;
    _dupenv_s(&rayReconstruction, &rayReconstructionLength, "RAYTRACING_DEMO_DLSS_RR");
    if (rayReconstruction != nullptr)
    {
        m_DLSS.SetRayReconstructionEnabled(std::strcmp(rayReconstruction, "0") != 0);
    }
    std::free(rayReconstruction);

    char* frameGeneration = nullptr;
    size_t frameGenerationLength = 0;
    _dupenv_s(&frameGeneration, &frameGenerationLength, "RAYTRACING_DEMO_DLSS_FRAME_GENERATION");
    if (frameGeneration != nullptr)
    {
        m_DLSS.SetFrameGenerationEnabled(std::strcmp(frameGeneration, "0") != 0);
    }
    std::free(frameGeneration);
//Modify End

//Modify Begin:2026-07-30 by BestHui
    char* asyncCompute = nullptr;
    size_t asyncComputeLength = 0;
    _dupenv_s(&asyncCompute, &asyncComputeLength, "RAYTRACING_DEMO_ASYNC_COMPUTE");
    if (asyncCompute != nullptr)
    {
        m_AsyncComputeEnabled = std::strcmp(asyncCompute, "0") != 0;
    }
    std::free(asyncCompute);

    char* debugSerializeAsyncCompute = nullptr;
    size_t debugSerializeAsyncComputeLength = 0;
    _dupenv_s(
        &debugSerializeAsyncCompute,
        &debugSerializeAsyncComputeLength,
        "RAYTRACING_DEMO_ASYNC_CPU_SERIALIZE");
    if (debugSerializeAsyncCompute != nullptr)
    {
        m_DebugSerializeAsyncCompute = std::strcmp(debugSerializeAsyncCompute, "0") != 0;
    }
    std::free(debugSerializeAsyncCompute);
//Modify End

    char* cudaBloom = nullptr;
    size_t cudaBloomLength = 0;
    _dupenv_s(&cudaBloom, &cudaBloomLength, "RAYTRACING_DEMO_CUDA_BLOOM");
    if (cudaBloom != nullptr)
    {
        m_CudaBloom.SetEnabled(std::strcmp(cudaBloom, "0") != 0);
    }
    std::free(cudaBloom);

//Modify End

//Modify Begin:2026-07-30 by BestHui
    char* meshletGBuffer = nullptr;
    size_t meshletGBufferLength = 0;
    _dupenv_s(&meshletGBuffer, &meshletGBufferLength, "RAYTRACING_DEMO_MESHLET_GBUFFER");
    if (meshletGBuffer != nullptr)
    {
        m_UseMeshletGBuffer = std::strcmp(meshletGBuffer, "0") != 0;
    }
    std::free(meshletGBuffer);

    char* meshletDebug = nullptr;
    size_t meshletDebugLength = 0;
    _dupenv_s(&meshletDebug, &meshletDebugLength, "RAYTRACING_DEMO_MESHLET_DEBUG");
    if (meshletDebug != nullptr)
    {
        m_DebugMeshletClusters = std::strcmp(meshletDebug, "0") != 0;
        if (m_DebugMeshletClusters)
        {
            m_UseMeshletGBuffer = true;
        }
    }
    std::free(meshletDebug);

//Modify Begin:2026-07-31 by BestHui
    char* meshletBackend = nullptr;
    size_t meshletBackendLength = 0;
    _dupenv_s(&meshletBackend, &meshletBackendLength, "RAYTRACING_DEMO_MESHLET_BACKEND");
    if (meshletBackend != nullptr)
    {
        m_UseTaskShaderMeshlets = std::strcmp(meshletBackend, "indirect") != 0;
    }
    std::free(meshletBackend);
//Modify End
//Modify End

}

//Modify Begin:2026-07-30 by BestHui
//Modify Begin:2026-08-03 by BestHui
void RaytracingDemo::LoadSceneContent(CommandList& commandList, const std::filesystem::path& scenePath)
{
    const SceneImportResult sceneImport = SceneImporter::ImportFromFile(scenePath);
    m_Scene = sceneImport.SceneData;
    std::filesystem::path runtimeStatePath = scenePath;
    runtimeStatePath += ".runtime.json";
    if (std::filesystem::exists(runtimeStatePath))
    {
        SceneImporter::ApplyJsonRuntimeState(runtimeStatePath, m_Scene);
    }
    const SceneCamera& sceneCamera = m_Scene.GetCamera();

    if (!m_SceneResources.LoadScene(commandList, m_Scene, m_StressTestSpheresEnabled))
    {
        throw std::runtime_error("Scene has no supported renderable objects.");
    }

    SceneSkybox sceneSkybox = m_Scene.GetSkybox();
    std::filesystem::path skyboxTexturePath = sceneSkybox.Texture.AssetPath;
    if (skyboxTexturePath.empty())
    {
        skyboxTexturePath = L"Assets/Textures/skybox/skybox.dds";
        sceneSkybox.Texture.AssetPath = skyboxTexturePath;
    }
    if (sceneSkybox.AmbientColorAndIntensity.x <= 0.0f &&
        sceneSkybox.AmbientColorAndIntensity.y <= 0.0f &&
        sceneSkybox.AmbientColorAndIntensity.z <= 0.0f)
    {
        sceneSkybox.AmbientColorAndIntensity = { 1.0f, 1.0f, 1.0f, std::max(0.001f, sceneSkybox.AmbientColorAndIntensity.w) };
    }
    m_Scene.SetSkybox(sceneSkybox);

    m_Lights.CreateFromScene(m_Scene);
//Modify Begin:2026-08-10 by BestHui
    bool directionalLightsEnabled = m_Lights.AreDirectionalLightsEnabled();
//Modify Begin:2026-08-11 by BestHui
    bool pointLightsEnabled = false;
//Modify End
    bool areaLightsEnabled = m_Lights.AreAreaLightsEnabled();
//Modify Begin:2026-08-11 by BestHui
    TryGetEnvironmentBoolean("RAYTRACING_DEMO_DIRECTIONAL_LIGHTS", directionalLightsEnabled);
    TryGetEnvironmentBoolean("RAYTRACING_DEMO_POINT_LIGHTS", pointLightsEnabled);
    TryGetEnvironmentBoolean("RAYTRACING_DEMO_AREA_LIGHTS", areaLightsEnabled);
    m_Lights.SetLightGroupSettings(directionalLightsEnabled, pointLightsEnabled, areaLightsEnabled);
//Modify End

    bool skyLightEnabled = true;
    if (TryGetEnvironmentBoolean("RAYTRACING_DEMO_SKY_LIGHT", skyLightEnabled) && !skyLightEnabled)
    {
        SkyLightData skyLight = m_Lights.GetSkyLight();
        skyLight.ColorAndIntensity.w = 0.0f;
        m_Lights.SetSkyLight(skyLight);
    }
//Modify End
//Modify Begin:2026-08-06 by BestHui
    m_Lights.SetEmissiveMeshSurfaceEmitters(m_SceneResources.CollectEmissiveMeshSurfaceEmitters());
//Modify End
    m_SkyboxEnabled = !skyboxTexturePath.empty() && std::filesystem::exists(skyboxTexturePath);
    m_HasSceneCamera = sceneCamera.RuntimeCamera != nullptr;

    ApplySceneCamera(GetSceneCamera(), sceneCamera, m_Width, m_Height);
    const XMFLOAT3 forward = RotateCameraVector(GetSceneCamera().GetRotation(), { 0.0f, 0.0f, 1.0f });
    CalculateCameraControllerFromLookDirection(
        XMVectorSet(forward.x, forward.y, forward.z, 0.0f),
        m_CameraController.Yaw,
        m_CameraController.Pitch);
    m_CameraFov = sceneCamera.FieldOfView;
    m_CameraNearClipPlane = sceneCamera.NearClipPlane;
    m_CameraFarClipPlane = sceneCamera.FarClipPlane;
//Modify Begin:2026-07-30 by BestHui
    XMStoreFloat3(&m_InitialSceneCameraTranslation, GetSceneCamera().GetTranslation());
    XMStoreFloat4(&m_InitialSceneCameraRotation, GetSceneCamera().GetRotation());
    m_InitialSceneCameraYaw = m_CameraController.Yaw;
    m_InitialSceneCameraPitch = m_CameraController.Pitch;
    m_HasInitialSceneCameraState = true;
//Modify End

    m_SkyboxTexture.reset();
    if (m_SkyboxEnabled)
    {
//Modify Begin:2026-08-12 by BestHui
        m_SkyboxTexture = std::make_shared<Texture>(
            TextureUsageType::Other,
            L"",
            commandList.GetDeviceContext());
//Modify End
        commandList.LoadTextureFromFile(*m_SkyboxTexture, skyboxTexturePath.wstring(), TextureUsageType::Albedo);
    }
}
//Modify End

//Modify Begin:2026-07-30 by BestHui
void RaytracingDemo::ResetCameraToInitialSceneState()
{
    if (!m_HasInitialSceneCameraState)
    {
        return;
    }

    GetSceneCamera().SetTranslation(XMLoadFloat3(&m_InitialSceneCameraTranslation));
    GetSceneCamera().SetRotation(XMLoadFloat4(&m_InitialSceneCameraRotation));
    m_CameraController.Yaw = m_InitialSceneCameraYaw;
    m_CameraController.Pitch = m_InitialSceneCameraPitch;
    m_HasPreviousViewProjection = false;
    ResetAccumulation();
}

//Modify Begin:2026-08-07 by BestHui
void RaytracingDemo::InitializeRuntimeAutomation()
{
    m_RuntimeAutomation.Initialize(CreateRuntimeAutomationTestSuites());
}

void RaytracingDemo::UpdateRuntimeAutomation(const double totalTime)
{
    m_RuntimeAutomation.Update(
        totalTime,
        [this](const uint32_t action, const uint32_t value)
        {
            ApplyRuntimeAutomationAction(action, value);
        },
        []()
        {
            Application::Get().Quit(0);
        });
}

void RaytracingDemo::ApplyRuntimeAutomationMatrixCase(const uint32_t caseIndex)
{
    const auto& matrixCases = GetRuntimeAutomationMatrixCases();
    if (caseIndex >= matrixCases.size())
    {
        throw std::out_of_range("Runtime automation matrix case index is out of range.");
    }

    const RuntimeAutomationMatrixCase& testCase = matrixCases[caseIndex];
    const bool stressSpheresChanged = m_StressTestSpheresEnabled != testCase.StressSpheres;

    m_PathTracingBackend = testCase.Backend;
    m_DirectLightingTechnique = testCase.DirectLighting;
    m_IndirectLightingTechnique = testCase.IndirectLighting;
    m_AsyncComputeEnabled = testCase.Backend == PathTracingBackend::InlineRayQuery && testCase.AsyncCompute;
    m_ParallelDirectCommandRecordingEnabled = testCase.ParallelDirectCommandRecording;
    m_UseMeshletGBuffer = testCase.UseMeshletGBuffer;
    m_UseTaskShaderMeshlets = testCase.UseTaskShaderMeshlets;
    m_SoftShadowsEnabled = testCase.SoftShadows;
    m_StressTestSpheresEnabled = testCase.StressSpheres;
    m_SkyboxEnabled = testCase.Skybox;
    m_AccumulationEnabled = testCase.Accumulation;
    m_DLSS.SetMode(testCase.DlssMode);
//Modify Begin:2026-08-12 by BestHui
    m_MaterialShadingModel = testCase.ShadingModel;
//Modify End
//Modify Begin:2026-07-30 by BestHui
    SetMaxBounces(testCase.MaxBounces);
//Modify End
    m_DebugMeshletClusters = false;
    m_DebugLightingTextureTarget = 0;
    m_DebugTextureTarget = 0;
    m_DebugSerializeAsyncCompute = false;
    m_StressTestSpheresStateDirty = stressSpheresChanged;
    EnsureRayTracingPipelines();
    ResetAccumulation();
}

void RaytracingDemo::ApplyRuntimeAutomationAction(const uint32_t actionValue, const uint32_t value)
{
    const bool enabled = value != 0u;
    const auto action = static_cast<RuntimeAutomationAction>(actionValue);
    switch (action)
    {
    case RuntimeAutomationAction::SoftShadows:
        m_SoftShadowsEnabled = enabled;
        EnsureRayTracingPipelines();
        BindRayTracingShaderResources();
        ResetAccumulation();
        break;
    case RuntimeAutomationAction::StressSpheres:
        m_StressTestSpheresEnabled = enabled;
        m_StressTestSpheresStateDirty = true;
        break;
    case RuntimeAutomationAction::MeshletGBuffer:
        m_UseMeshletGBuffer = enabled;
        ResetAccumulation();
        break;
    case RuntimeAutomationAction::MeshletTaskShader:
        m_UseTaskShaderMeshlets = enabled;
        m_UseMeshletGBuffer = true;
        ResetAccumulation();
        break;
    case RuntimeAutomationAction::PathTracingBackend:
        m_PathTracingBackend = static_cast<PathTracingBackend>(value);
        if (m_PathTracingBackend != PathTracingBackend::InlineRayQuery)
        {
            m_AsyncComputeEnabled = false;
        }
        ResetAccumulation();
        break;
    case RuntimeAutomationAction::DirectLighting:
        m_DirectLightingTechnique = static_cast<RaytracingDemoLightingTechnique>(value);
        ResetAccumulation();
        break;
    case RuntimeAutomationAction::IndirectLighting:
        m_IndirectLightingTechnique = static_cast<RaytracingDemoLightingTechnique>(value);
        ResetAccumulation();
        break;
//Modify Begin:2026-07-30 by BestHui
    case RuntimeAutomationAction::MaxBounces:
        SetMaxBounces(static_cast<int>(value));
        break;
//Modify End
//Modify Begin:2026-07-30 by BestHui
    case RuntimeAutomationAction::Wait:
        break;
//Modify End
    case RuntimeAutomationAction::AsyncCompute:
        if (m_PathTracingBackend == PathTracingBackend::InlineRayQuery)
        {
            m_AsyncComputeEnabled = enabled;
            ResetAccumulation();
        }
        break;
    case RuntimeAutomationAction::ParallelDirectCommandRecording:
        m_ParallelDirectCommandRecordingEnabled = enabled;
        break;
    case RuntimeAutomationAction::Skybox:
        m_SkyboxEnabled = enabled;
        ResetAccumulation();
        break;
    case RuntimeAutomationAction::Accumulation:
        m_AccumulationEnabled = enabled;
        ResetAccumulation();
        break;
    case RuntimeAutomationAction::GpuTiming:
        m_GpuTimingEnabled = enabled;
        m_GpuTimestampSamples.clear();
        m_GpuTimestampDisplaySamples.clear();
        m_AsyncComputeGpuTimestampSamples.clear();
        m_AsyncComputeGpuTimestampDisplaySamples.clear();
        if (!enabled)
        {
            m_RenderGraphTimingCaptureEnabled = false;
        }
        m_RenderGraphTimingHistory.Clear();
        break;
    case RuntimeAutomationAction::TimingCapture:
        m_RenderGraphTimingCaptureEnabled = enabled;
        m_RenderGraphTimingHistory.Clear();
        break;
//Modify Begin:2026-08-11 by BestHui
    case RuntimeAutomationAction::ReSTIRGIStageTiming:
        m_ReSTIRGIStageTimingEnabled = enabled && m_GpuTimingEnabled;
        break;
//Modify End
//Modify Begin:2026-08-11 by BestHui
    case RuntimeAutomationAction::ReSTIRGITemporalResampling:
    case RuntimeAutomationAction::ReSTIRGISpatialResampling:
    case RuntimeAutomationAction::ReSTIRGITemporalJacobian:
    case RuntimeAutomationAction::ReSTIRGISpatialRayTracedBiasCorrection:
    {
        ReSTIRGISettings restirGISettings = m_IndirectLightingReSTIRGI.GetSettings();
        switch (action)
        {
        case RuntimeAutomationAction::ReSTIRGITemporalResampling:
            restirGISettings.EnableTemporalResampling = enabled;
            break;
        case RuntimeAutomationAction::ReSTIRGISpatialResampling:
            restirGISettings.EnableSpatialResampling = enabled;
            break;
        case RuntimeAutomationAction::ReSTIRGITemporalJacobian:
            restirGISettings.EnableTemporalJacobian = enabled;
            break;
        case RuntimeAutomationAction::ReSTIRGISpatialRayTracedBiasCorrection:
            restirGISettings.EnableRayTracedSpatialBiasCorrection = enabled;
            break;
        default:
            break;
        }
        m_IndirectLightingReSTIRGI.SetSettings(restirGISettings);
        EnsureRayTracingPipelines();
        ResetAccumulation();
        break;
    }
//Modify End
    case RuntimeAutomationAction::DumpTiming:
        m_RenderGraphTimingHistory.DumpCsv();
        break;
//Modify Begin:2026-08-07 by BestHui
    case RuntimeAutomationAction::DLSS:
        m_DLSS.SetMode(static_cast<DLSSMode>(value));
        ResetAccumulation();
        break;
//Modify End
//Modify Begin:2026-08-12 by BestHui
    case RuntimeAutomationAction::MaterialShading:
        SetMaterialShadingModel(static_cast<MaterialShadingModel>(value));
        break;
//Modify End
    case RuntimeAutomationAction::MatrixCase:
        ApplyRuntimeAutomationMatrixCase(value);
        break;
    }
}
//Modify End
//Modify End

//Modify Begin:2026-07-30 by BestHui
void RaytracingDemo::ApplyStressTestSpheresState()
{
    if (!m_StressTestSpheresStateDirty)
    {
        return;
    }

    if (m_SceneResources.AreStressTestSpheresEnabled() == m_StressTestSpheresEnabled)
    {
        m_StressTestSpheresStateDirty = false;
        return;
    }

//Modify Begin:2026-08-07 by BestHui
    m_RuntimeAutomation.AppendDiagnosticLog("Stress transition: flush queues.");
//Modify End
    m_FrameworkDeviceContext.Flush();
    const auto commandQueue = m_FrameworkDeviceContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
    const auto commandList = commandQueue->GetCommandList();
//Modify Begin:2026-08-07 by BestHui
    m_RuntimeAutomation.AppendDiagnosticLog("Stress transition: update scene resources.");
//Modify End
    if (m_SceneResources.SetStressTestSpheresEnabled(*commandList, m_StressTestSpheresEnabled))
    {
//Modify Begin:2026-08-07 by BestHui
        m_RuntimeAutomation.AppendDiagnosticLog("Stress transition: submit resource update.");
//Modify End
        const uint64_t fenceValue = commandQueue->ExecuteCommandList(commandList);
        commandQueue->WaitForFenceValue(fenceValue);
//Modify Begin:2026-08-07 by BestHui
        m_RuntimeAutomation.AppendDiagnosticLog("Stress transition: rebuild lights.");
//Modify End
        m_Lights.SetEmissiveMeshSurfaceEmitters(m_SceneResources.CollectEmissiveMeshSurfaceEmitters());
//Modify Begin:2026-08-07 by BestHui
        m_RuntimeAutomation.AppendDiagnosticLog("Stress transition: rebuild render graph.");
//Modify End
        RebuildRenderGraph();

        m_RenderGraphTimingHistory.Clear();
        ResetAccumulation();
//Modify Begin:2026-08-07 by BestHui
        m_RuntimeAutomation.AppendDiagnosticLog("Stress transition: complete.");
//Modify End
    }

    m_StressTestSpheresStateDirty = false;
}
//Modify End

//Modify Begin:2026-07-30 by BestHui
std::shared_ptr<ShaderBlob> RaytracingDemo::LoadShaderVariant(
    std::wstring compiledFileName,
    std::wstring sourceFileName,
    std::string targetProfile,
    std::vector<ShaderVariantDefine> defines)
{
    ShaderVariantDesc desc;
    desc.CompiledFileName = std::move(compiledFileName);
    desc.SourceFileName = std::move(sourceFileName);
    desc.TargetProfile = std::move(targetProfile);
    desc.Defines = std::move(defines);
    desc.DebugName = "RaytracingDemo";
    return m_ShaderVariants.GetOrCompile(desc);
}
//Modify End

bool RaytracingDemo::LoadContent()
{
    Assert(RayTracingShader::IsSupported(m_FrameworkDeviceContext), "DirectX Raytracing is not supported by the selected adapter.");

    const auto commandQueue = m_FrameworkDeviceContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
    const auto commandList = commandQueue->GetCommandList();

//Modify Begin:2026-08-03 by BestHui
    LoadSceneContent(*commandList, GetScenePath());
//Modify End

    m_ImGui = std::make_unique<ImGuiImpl>(m_FrameworkDeviceContext, *commandList, *PWindow);

    m_LightBillboardMesh = Mesh::CreateVerticalQuad(*commandList);
//Modify Begin:2026-07-28 by BestHui
    m_DisplayBlitMesh = Mesh::CreateBlitTriangle(*commandList);
//Modify End

//Modify Begin:2026-07-30 by BestHui
    const auto withShaderCreateContext = [](const char* name, const auto& createShader)
    {
        try
        {
            createShader();
        }
        catch (const std::exception& exception)
        {
            throw std::runtime_error(std::string("Failed to create shader pipeline '") + name + "': " + exception.what());
        }
    };

    withShaderCreateContext("GBuffer", [&]()
    {
//Modify End
    const auto vertexShader = LoadShaderVariant(
        L"GBuffer.vs.cso",
        L"Demos/RaytracingDemo/shaders/GBuffer/GBuffer.vs.hlsl",
        ShaderTargetProfile::Vertex());
    const auto pixelShader = LoadShaderVariant(
        L"GBuffer.ps.cso",
        L"Demos/RaytracingDemo/shaders/GBuffer/GBuffer.ps.hlsl",
        ShaderTargetProfile::Pixel());
    PipelineLayoutReflectionOptions gBufferLayoutOptions;
    gBufferLayoutOptions.MaxDescriptorCount = 4096u;
    gBufferLayoutOptions.ShaderStages = PipelineShaderStageFlags::AllGraphics;
    gBufferLayoutOptions.StaticSamplerContracts = {
        PipelineStaticSamplers::PointWrap(0u),
        PipelineStaticSamplers::LinearWrap(1u),
        PipelineStaticSamplers::PointClamp(2u),
        PipelineStaticSamplers::LinearClamp(3u),
        PipelineStaticSamplers::ShadowCompareClamp(4u)
    };
    m_GBufferShader = std::make_shared<Shader>(
        m_FrameworkDeviceContext,
        *vertexShader,
        *pixelShader,
        std::move(gBufferLayoutOptions),
        [](RasterPipelineStateBuilder& builder)
        {
            builder.WithNoCull();
        });
//Modify Begin:2026-07-30 by BestHui
    });
//Modify End

//Modify Begin:2026-07-31 by BestHui
    withShaderCreateContext("GBufferTaskMeshShader", [&]()
    {
    const auto amplificationShader = LoadShaderVariant(
        L"GBuffer.task.as.cso",
        L"Demos/RaytracingDemo/shaders/GBuffer/GBuffer.task.as.hlsl",
        ShaderTargetProfile::Amplification());
    const auto meshShader = LoadShaderVariant(
        L"GBuffer.task.ms.cso",
        L"Demos/RaytracingDemo/shaders/GBuffer/GBuffer.task.ms.hlsl",
        ShaderTargetProfile::Mesh());
    const auto pixelShader = LoadShaderVariant(
        L"GBuffer.meshletindirect.ps.cso",
        L"Demos/RaytracingDemo/shaders/GBuffer/GBuffer.meshletindirect.ps.hlsl",
        ShaderTargetProfile::Pixel());
    PipelineLayoutReflectionOptions taskMeshLayoutOptions;
    taskMeshLayoutOptions.MaxDescriptorCount = 4096u;
    taskMeshLayoutOptions.ShaderStages = PipelineShaderStageFlags::AllGraphics;
    taskMeshLayoutOptions.StaticSamplerContracts = {
        PipelineStaticSamplers::PointWrap(0u),
        PipelineStaticSamplers::LinearWrap(1u),
        PipelineStaticSamplers::PointClamp(2u),
        PipelineStaticSamplers::LinearClamp(3u),
        PipelineStaticSamplers::ShadowCompareClamp(4u)
    };
    m_GBufferTaskMeshShader = std::make_shared<MeshShader>(
        m_FrameworkDeviceContext,
        *amplificationShader,
        *meshShader,
        *pixelShader,
        std::move(taskMeshLayoutOptions),
        [](RasterPipelineStateBuilder& builder)
        {
            builder.WithNoCull();
        });
    });
//Modify End

    withShaderCreateContext("GBufferMeshletIndirect", [&]()
    {
    const auto vertexShader = LoadShaderVariant(
        L"GBuffer.meshletindirect.vs.cso",
        L"Demos/RaytracingDemo/shaders/GBuffer/GBuffer.meshletindirect.vs.hlsl",
        ShaderTargetProfile::Vertex());
    const auto pixelShader = LoadShaderVariant(
        L"GBuffer.meshletindirect.ps.cso",
        L"Demos/RaytracingDemo/shaders/GBuffer/GBuffer.meshletindirect.ps.hlsl",
        ShaderTargetProfile::Pixel());
    PipelineLayoutReflectionOptions meshletIndirectLayoutOptions;
    meshletIndirectLayoutOptions.MaxDescriptorCount = 4096u;
    meshletIndirectLayoutOptions.ShaderStages = PipelineShaderStageFlags::AllGraphics;
    meshletIndirectLayoutOptions.RootConstantBufferNames.push_back("MeshletDrawCBuffer");
    meshletIndirectLayoutOptions.StaticSamplerContracts = {
        PipelineStaticSamplers::PointWrap(0u),
        PipelineStaticSamplers::LinearWrap(1u),
        PipelineStaticSamplers::PointClamp(2u),
        PipelineStaticSamplers::LinearClamp(3u),
        PipelineStaticSamplers::ShadowCompareClamp(4u)
    };
    m_GBufferMeshletIndirectShader = std::make_shared<Shader>(
        m_FrameworkDeviceContext,
        *vertexShader,
        *pixelShader,
        meshletIndirectLayoutOptions,
        [](RasterPipelineStateBuilder& builder)
        {
            builder.WithNoCull();
        });
    });

    withShaderCreateContext("MeshletCull", [&]()
    {
    const auto meshletCullShaderBlob = LoadShaderVariant(
        L"MeshletCull.cs.cso",
        L"Demos/RaytracingDemo/shaders/GBuffer/MeshletCull.cs.hlsl",
        ShaderTargetProfile::Compute());
    m_MeshletCullShader = std::make_shared<ComputeShader>(
        m_FrameworkDeviceContext,
        *meshletCullShaderBlob,
        ComputePipelineDescBuilder::ReflectedDefault(*meshletCullShaderBlob).Build());
    m_MeshletDrawCommandSignature = m_GBufferMeshletIndirectShader->CreateIndirectDrawCommandSignature(
        "MeshletDrawCBuffer",
        sizeof(MeshletIndirectCommand));
    });
//Modify End

//Modify Begin:2026-07-28 by BestHui
    withShaderCreateContext("DisplayComposite", [&]()
    {
    const auto vertexShader = LoadShaderVariant(
        L"DisplayComposite.vs.cso",
        L"Demos/RaytracingDemo/shaders/PostProcessing/DisplayComposite.vs.hlsl",
        ShaderTargetProfile::Vertex());
    const auto pixelShader = LoadShaderVariant(
        L"DisplayComposite.ps.cso",
        L"Demos/RaytracingDemo/shaders/PostProcessing/DisplayComposite.ps.hlsl",
        ShaderTargetProfile::Pixel());
    PipelineLayoutReflectionOptions displayCompositeLayoutOptions;
    displayCompositeLayoutOptions.MaxDescriptorCount = 4096u;
    displayCompositeLayoutOptions.ShaderStages = PipelineShaderStageFlags::AllGraphics;
    displayCompositeLayoutOptions.StaticSamplerContracts = { PipelineStaticSamplers::LinearClamp(3u) };
    m_DisplayCompositeShader = std::make_shared<Shader>(
        m_FrameworkDeviceContext,
        *vertexShader,
        *pixelShader,
        std::move(displayCompositeLayoutOptions),
        [](RasterPipelineStateBuilder&) {});
    });
//Modify End

//Modify Begin:2026-07-28 by BestHui
    withShaderCreateContext("SkyboxCompute", [&]()
    {
    const auto skyboxComputeShader = LoadShaderVariant(
        L"Skybox.cs.cso",
        L"Demos/RaytracingDemo/shaders/Skybox/Skybox.cs.hlsl",
        ShaderTargetProfile::Compute());
    m_SkyboxComputeShader = std::make_shared<ComputeShader>(
        m_FrameworkDeviceContext,
        *skyboxComputeShader,
        ComputePipelineDescBuilder::ReflectedDefault(*skyboxComputeShader)
            .WithCommonRootSignatureStaticSamplers()
            .Build());

    const auto equirectangularSkyboxComputeShader = LoadShaderVariant(
        L"SkyboxEquirectangular.cs.cso",
        L"Demos/RaytracingDemo/shaders/Skybox/SkyboxEquirectangular.cs.hlsl",
        ShaderTargetProfile::Compute());
    m_SkyboxEquirectangularComputeShader = std::make_shared<ComputeShader>(
        m_FrameworkDeviceContext,
        *equirectangularSkyboxComputeShader,
        ComputePipelineDescBuilder::ReflectedDefault(*equirectangularSkyboxComputeShader)
            .WithCommonRootSignatureStaticSamplers()
            .Build());

//Modify Begin:2026-08-06 by BestHui
    const auto cubemapStripSkyboxComputeShader = LoadShaderVariant(
        L"SkyboxCubemapStrip.cs.cso",
        L"Demos/RaytracingDemo/shaders/Skybox/SkyboxCubemapStrip.cs.hlsl",
        ShaderTargetProfile::Compute());
    m_SkyboxCubemapStripComputeShader = std::make_shared<ComputeShader>(
        m_FrameworkDeviceContext,
        *cubemapStripSkyboxComputeShader,
        ComputePipelineDescBuilder::ReflectedDefault(*cubemapStripSkyboxComputeShader)
            .WithCommonRootSignatureStaticSamplers()
            .Build());
//Modify End
    });
//Modify End

//Modify Begin:2026-08-07 by BestHui
    withShaderCreateContext("DLSSRayReconstructionPrepare", [&]()
    {
        const auto rayReconstructionPrepareShader = LoadShaderVariant(
            L"DLSSRayReconstructionPrepare.cs.cso",
            L"Demos/RaytracingDemo/shaders/Upscaling/DLSSRayReconstructionPrepare.cs.hlsl",
            ShaderTargetProfile::Compute());
        m_DLSSRayReconstructionPrepareShader = std::make_shared<ComputeShader>(
            m_FrameworkDeviceContext,
            *rayReconstructionPrepareShader,
            ComputePipelineDescBuilder::ReflectedDefault(*rayReconstructionPrepareShader).Build());
    });
//Modify End

    withShaderCreateContext("LightBillboard", [&]()
    {
    const auto vertexShader = LoadShaderVariant(
        L"LightBillboard.vs.cso",
        L"Demos/RaytracingDemo/shaders/LightBillboard/LightBillboard.vs.hlsl",
        ShaderTargetProfile::Vertex());
    const auto pixelShader = LoadShaderVariant(
        L"LightBillboard.ps.cso",
        L"Demos/RaytracingDemo/shaders/LightBillboard/LightBillboard.ps.hlsl",
        ShaderTargetProfile::Pixel());
    m_LightBillboardShader = std::make_shared<Shader>(
        m_FrameworkDeviceContext,
        *vertexShader,
        *pixelShader,
        [](RasterPipelineStateBuilder& builder)
        {
            builder.WithAlphaBlend().WithDepthTestNoWrite().WithNoCull();
        });
    });

    m_Denoisers.Initialize(m_FrameworkDeviceContext);
    RayTracingAccelerationStructureBuildSettings accelerationStructureSettings{};
    accelerationStructureSettings.AllowUpdate = true;
    m_SceneResources.BuildRayTracingAccelerationStructure(*commandList, accelerationStructureSettings);
    m_Lights.InitializeGpuBuffers(*commandList);
//Modify Begin:2026-07-27 by BestHui
    EnsureRayTracingPipelines();
//Modify End
//Modify Begin:2026-08-06 by BestHui
    PrewarmRuntimeShadowVariants();
//Modify End
    BindRayTracingShaderResources();

//Modify Begin:2026-07-28 by BestHui
//Modify Begin:2026-07-29 by BestHui
    m_GpuTimestampProfiler.Initialize(
        m_FrameworkDeviceContext.GetDevice(),
        m_FrameworkDeviceContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT),
        128);
//Modify Begin:2026-08-03 by BestHui
    m_AsyncComputeGpuTimestampProfiler.Initialize(
        m_FrameworkDeviceContext.GetDevice(),
        m_FrameworkDeviceContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE),
        128,
        D3D12_COMMAND_LIST_TYPE_COMPUTE);
//Modify End
//Modify End
    RebuildRenderGraph();
//Modify End

    const uint64_t fenceValue = commandQueue->ExecuteCommandList(commandList);
    commandQueue->WaitForFenceValue(fenceValue);
//Modify Begin:2026-07-30 by BestHui
    InitializeRuntimeAutomation();
//Modify End
    return true;
}

void RaytracingDemo::UnloadContent()
{
//Modify Begin:2026-07-28 by BestHui
    m_CudaBloom.ReleaseInteropResource();
//Modify End
    m_RenderGraph.reset();
    m_LightBillboardShader.reset();
//Modify Begin:2026-07-28 by BestHui
    m_SkyboxComputeShader.reset();
    m_SkyboxEquirectangularComputeShader.reset();
//Modify Begin:2026-08-06 by BestHui
    m_SkyboxCubemapStripComputeShader.reset();
//Modify End
//Modify Begin:2026-08-07 by BestHui
    m_DLSSRayReconstructionPrepareShader.reset();
//Modify End
    m_DisplayCompositeShader.reset();
//Modify End
//Modify Begin:2026-07-30 by BestHui
//Modify Begin:2026-07-31 by BestHui
    m_GBufferTaskMeshShader.reset();
//Modify End
    m_GBufferMeshletIndirectShader.reset();
    m_MeshletCullShader.reset();
    m_MeshletDrawCommandSignature.reset();
//Modify End
    m_GBufferShader.reset();
    m_LightBillboardMesh.reset();
//Modify Begin:2026-07-28 by BestHui
    m_DisplayBlitMesh.reset();
//Modify End
    m_ImGui.reset();
    m_Denoisers.Shutdown();
//Modify Begin:2026-07-27 by BestHui
    m_CudaBloom.Shutdown();
//Modify End
    m_SkyboxTexture.reset();
//Modify Begin:2026-07-27 by BestHui
    m_PathTracingPipelines.Reset();
//Modify End
//Modify Begin:2026-07-29 by BestHui
    m_GpuTimestampProfiler.Shutdown();
//Modify Begin:2026-08-03 by BestHui
    m_AsyncComputeGpuTimestampProfiler.Shutdown();
//Modify End
//Modify End
    m_SceneResources.Clear();
}

RayTracingSceneResourceLayout RaytracingDemo::BuildRayTracingSceneResourceLayout() const
{
    const RayTracingAccelerationStructure& accelerationStructure = m_SceneResources.GetRayTracingAccelerationStructure();
    const std::vector<std::shared_ptr<Mesh>>& rayTracingMeshes = accelerationStructure.GetMeshes();

    RayTracingSceneResourceLayout layout;
    layout.TextureDescriptorCapacity = ComputeDescriptorArrayCapacity(m_SceneResources.GetTextureCount(), m_SceneResources.GetTextureCapacity());
    layout.GeometryDescriptorCapacity = ComputeDescriptorArrayCapacity(rayTracingMeshes.size(), rayTracingMeshes.capacity());
//Modify Begin:2026-08-06 by BestHui
    layout.EnvironmentProjection = m_SkyboxTexture != nullptr
        ? ShaderResourceView::GetEnvironmentTextureProjection(*m_SkyboxTexture)
        : EnvironmentTextureProjection::Cubemap;
//Modify End
    return layout;
}

void RaytracingDemo::EnsureRayTracingPipelines()
{
    const RayTracingSceneResourceLayout layout = BuildRayTracingSceneResourceLayout();
    const ReSTIRDIFrameConstants restirDIConstants = m_DirectLightingReSTIRDI.GetFrameConstants(false);
//Modify Begin:2026-07-27 by BestHui
    m_PathTracingPipelines.EnsurePipelines(
        m_PathTracingBackend,
        m_SoftShadowsEnabled ? PathTracingShadowMode::SoftShadows : PathTracingShadowMode::HardShadows,
        m_MaterialShadingModel,
        layout,
        static_cast<uint32_t>(m_MaxBounces));
//Modify Begin:2026-07-30 by BestHui
    m_DirectLightingReSTIRDIPass.EnsurePipelines(
        m_SoftShadowsEnabled,
        static_cast<uint32_t>(layout.EnvironmentProjection),
        restirDIConstants,
        m_MaterialShadingModel);
//Modify End
//Modify Begin:2026-08-10 by BestHui
    const bool restirGIActive =
        m_PathTracingBackend == PathTracingBackend::InlineRayQuery &&
        m_IndirectLightingTechnique == RaytracingDemoLightingTechnique::ReSTIRGI &&
        m_MaxBounces > 1;
//Modify Begin:2026-08-11 by BestHui
    const ReSTIRGIVariantConfig restirGIVariantConfig = m_IndirectLightingReSTIRGI.GetVariantConfig(
        static_cast<uint32_t>(m_MaxBounces));
//Modify End
    if (restirGIActive)
    {
        m_IndirectLightingReSTIRGIPass.EnsurePipelines(
            m_SoftShadowsEnabled,
            static_cast<uint32_t>(layout.EnvironmentProjection),
            restirGIVariantConfig,
            m_MaterialShadingModel);
    }
//Modify End
    if (m_SceneResources.GetRayTracingAccelerationStructure().GetInstanceCount() > 0)
    {
        BindRayTracingShaderResources();
    }
//Modify End
}

//Modify Begin:2026-07-30 by BestHui
void RaytracingDemo::SetMaterialShadingModel(const MaterialShadingModel shadingModel)
{
    if (m_MaterialShadingModel == shadingModel)
    {
        return;
    }

    m_MaterialShadingModel = shadingModel;
    if (m_RenderGraph != nullptr)
    {
        EnsureRayTracingPipelines();
    }
    ResetAccumulation();
}
//Modify End

//Modify Begin:2026-08-11 by BestHui
void RaytracingDemo::SetMaxBounces(const int maxBounces)
{
    const int clampedMaxBounces = std::clamp(maxBounces, 1, 5);
    if (m_MaxBounces == clampedMaxBounces)
    {
        return;
    }

    m_MaxBounces = clampedMaxBounces;
    if (m_RenderGraph != nullptr)
    {
        EnsureRayTracingPipelines();
        EnsureRenderGraphTopology();
    }
    ResetAccumulation();
}
//Modify End

//Modify Begin:2026-07-30 by BestHui
void RaytracingDemo::PrewarmRuntimeShadowVariants()
{
    const RayTracingSceneResourceLayout layout = BuildRayTracingSceneResourceLayout();
    const ReSTIRDIFrameConstants restirDIConstants = m_DirectLightingReSTIRDI.GetFrameConstants(false);
    const PathTracingShadowMode currentShadowMode = m_SoftShadowsEnabled
        ? PathTracingShadowMode::SoftShadows
        : PathTracingShadowMode::HardShadows;
    const PathTracingShadowMode alternateShadowMode = currentShadowMode == PathTracingShadowMode::SoftShadows
        ? PathTracingShadowMode::HardShadows
        : PathTracingShadowMode::SoftShadows;

    m_PathTracingPipelines.EnsurePipelines(
        m_PathTracingBackend,
        alternateShadowMode,
        m_MaterialShadingModel,
        layout,
        static_cast<uint32_t>(m_MaxBounces));
    m_DirectLightingReSTIRDIPass.EnsurePipelines(
        alternateShadowMode == PathTracingShadowMode::SoftShadows,
        static_cast<uint32_t>(layout.EnvironmentProjection),
        restirDIConstants,
        m_MaterialShadingModel);
//Modify Begin:2026-08-10 by BestHui
    const bool restirGIActive =
        m_PathTracingBackend == PathTracingBackend::InlineRayQuery &&
        m_IndirectLightingTechnique == RaytracingDemoLightingTechnique::ReSTIRGI &&
        m_MaxBounces > 1;
//Modify Begin:2026-08-11 by BestHui
    const ReSTIRGIVariantConfig restirGIVariantConfig = m_IndirectLightingReSTIRGI.GetVariantConfig(
        static_cast<uint32_t>(m_MaxBounces));
//Modify End
    if (restirGIActive)
    {
        m_IndirectLightingReSTIRGIPass.EnsurePipelines(
            alternateShadowMode == PathTracingShadowMode::SoftShadows,
            static_cast<uint32_t>(layout.EnvironmentProjection),
            restirGIVariantConfig,
            m_MaterialShadingModel);
    }
//Modify End
    m_PathTracingPipelines.EnsurePipelines(
        m_PathTracingBackend,
        currentShadowMode,
        m_MaterialShadingModel,
        layout,
        static_cast<uint32_t>(m_MaxBounces));
    m_DirectLightingReSTIRDIPass.EnsurePipelines(
        currentShadowMode == PathTracingShadowMode::SoftShadows,
        static_cast<uint32_t>(layout.EnvironmentProjection),
        restirDIConstants,
        m_MaterialShadingModel);
//Modify Begin:2026-08-10 by BestHui
    if (restirGIActive)
    {
        m_IndirectLightingReSTIRGIPass.EnsurePipelines(
            currentShadowMode == PathTracingShadowMode::SoftShadows,
            static_cast<uint32_t>(layout.EnvironmentProjection),
            restirGIVariantConfig,
            m_MaterialShadingModel);
    }
//Modify End
    BindRayTracingShaderResources();
}
//Modify End

void RaytracingDemo::BindRayTracingShaderResources()
{
//Modify Begin:2026-07-27 by BestHui
    m_PathTracingPipelines.BindRayTracingResources(
        m_SceneResources.GetRayTracingAccelerationStructure(),
        m_SceneResources,
        m_Lights,
        m_SkyboxTexture);
//Modify End
}

RaytracingDemo::PipelineConstants RaytracingDemo::BuildPipelineConstants() const
{
    PipelineConstants pipeline{};
    pipeline.View = GetSceneCamera().GetViewMatrix();
    pipeline.Projection = GetSceneCamera().GetProjectionMatrix();
    pipeline.ViewProjection = pipeline.View * pipeline.Projection;
    XMStoreFloat4(&pipeline.CameraPosition, GetSceneCamera().GetTranslation());
    pipeline.InverseView = XMMatrixInverse(nullptr, pipeline.View);
    pipeline.InverseProjection = XMMatrixInverse(nullptr, pipeline.Projection);
    pipeline.ScreenResolution = { static_cast<float>(m_Width), static_cast<float>(m_Height) };
    pipeline.ScreenTexelSize = { 1.0f / pipeline.ScreenResolution.x, 1.0f / pipeline.ScreenResolution.y };
//Modify Begin:2026-07-30 by BestHui
    pipeline.PreviousViewProjection = m_HasPreviousViewProjection ? m_PreviousViewProjection : pipeline.ViewProjection;
    pipeline.DebugMeshletClusters = m_DebugMeshletClusters ? 1u : 0u;
//Modify End
    return pipeline;
}

//Modify Begin:2026-07-28 by BestHui
void RaytracingDemo::RebuildRenderGraph()
{
//Modify Begin:2026-07-30 by BestHui
    UpdateRenderGraphFrameState();
//Modify End
    if (m_RenderGraph != nullptr)
    {
        m_FrameworkDeviceContext.Flush();
//Modify Begin:2026-07-28 by BestHui
        m_CudaBloom.ReleaseInteropResource();
//Modify End
//Modify Begin:2026-08-07 by BestHui
        m_DLSS.OnResourcesRecreated();
//Modify End
    }
//Modify Begin:2026-07-30 by BestHui
    EnsureRayTracingPipelines();
//Modify End
    m_RenderGraph = RaytracingDemoRenderGraphBuilder::Create(*this);
//Modify Begin:2026-07-29 by BestHui
    m_RenderGraph->SetGpuTimestampProfiler(m_GpuTimingEnabled ? &m_GpuTimestampProfiler : nullptr);
//Modify Begin:2026-08-03 by BestHui
    m_RenderGraph->SetAsyncComputeGpuTimestampProfiler(
        m_GpuTimingEnabled ? &m_AsyncComputeGpuTimestampProfiler : nullptr);
//Modify Begin:2026-07-30 by BestHui
    m_RenderGraph->SetDebugSerializeAsyncCompute(m_DebugSerializeAsyncCompute);
//Modify End
//Modify Begin:2026-08-07 by BestHui
    m_RenderGraph->SetParallelDirectCommandRecording(m_ParallelDirectCommandRecordingEnabled);
//Modify End
//Modify End
//Modify End
    m_RenderGraphDenoiserEnabled = IsDenoiserEnabled();
    m_RenderGraphCudaBloomEnabled = m_CudaBloom.IsEnabled();
//Modify Begin:2026-08-07 by BestHui
    m_RenderGraphDLSSEnabled = m_DLSS.IsEnabled();
    m_RenderGraphRayReconstructionEnabled = m_DLSS.IsEnabled() && m_DLSS.IsRayReconstructionEnabled();
    m_RenderGraphFrameGenerationEnabled = m_DLSS.IsFrameGenerationEnabled();
//Modify End
//Modify Begin:2026-08-03 by BestHui
    m_RenderGraphAsyncComputeEnabled = m_AsyncComputeEnabled;
    m_RenderGraphPathTracingBackend = m_PathTracingBackend;
//Modify Begin:2026-08-06 by BestHui
    m_RenderGraphDirectLightingTechnique = m_DirectLightingTechnique;
//Modify End
//Modify Begin:2026-08-10 by BestHui
    m_RenderGraphIndirectLightingTechnique = m_IndirectLightingTechnique;
    m_RenderGraphIndirectLightingEnabled = m_MaxBounces > 1;
//Modify End
//Modify Begin:2026-07-30 by BestHui
    m_RenderGraphLightingDebugTextureTarget = m_DebugLightingTextureTarget;
//Modify End
//Modify End
//Modify Begin:2026-07-31 by BestHui
    m_RenderGraphMeshletGBufferEnabled = m_UseMeshletGBuffer;
    m_RenderGraphTaskMeshletEnabled = m_UseTaskShaderMeshlets;
    m_RenderGraphMeshletDebugEnabled = m_UseMeshletGBuffer && m_DebugMeshletClusters;
    m_RenderGraphDebugTextureTarget = m_DebugTextureTarget;
//Modify End
}

void RaytracingDemo::EnsureRenderGraphTopology()
{
    if (m_RenderGraph == nullptr ||
        m_RenderGraphDenoiserEnabled != IsDenoiserEnabled() ||
        m_RenderGraphCudaBloomEnabled != m_CudaBloom.IsEnabled()
//Modify Begin:2026-08-07 by BestHui
        || m_RenderGraphDLSSEnabled != m_DLSS.IsEnabled()
        || m_RenderGraphRayReconstructionEnabled != (m_DLSS.IsEnabled() && m_DLSS.IsRayReconstructionEnabled())
        || m_RenderGraphFrameGenerationEnabled != m_DLSS.IsFrameGenerationEnabled()
//Modify End
//Modify Begin:2026-08-03 by BestHui
        || m_RenderGraphAsyncComputeEnabled != m_AsyncComputeEnabled
        || m_RenderGraphPathTracingBackend != m_PathTracingBackend
//Modify Begin:2026-08-06 by BestHui
        || m_RenderGraphDirectLightingTechnique != m_DirectLightingTechnique
//Modify End
//Modify Begin:2026-08-10 by BestHui
        || m_RenderGraphIndirectLightingTechnique != m_IndirectLightingTechnique
        || m_RenderGraphIndirectLightingEnabled != (m_MaxBounces > 1)
//Modify End
//Modify Begin:2026-07-30 by BestHui
        || m_RenderGraphLightingDebugTextureTarget != m_DebugLightingTextureTarget
//Modify End
//Modify End
//Modify Begin:2026-07-31 by BestHui
        || m_RenderGraphMeshletGBufferEnabled != m_UseMeshletGBuffer
        || m_RenderGraphTaskMeshletEnabled != m_UseTaskShaderMeshlets
        || m_RenderGraphMeshletDebugEnabled != (m_UseMeshletGBuffer && m_DebugMeshletClusters)
        || m_RenderGraphDebugTextureTarget != m_DebugTextureTarget
//Modify End
        )
    {
        RebuildRenderGraph();
        ResetAccumulation();
    }
}
//Modify End

void RaytracingDemo::ResetAccumulation(bool resetDenoiserHistory, bool resetReSTIRHistory)
{
    m_AccumulationFrameIndex = 0;
//Modify Begin:2026-08-05 by BestHui
    if (resetReSTIRHistory)
    {
        m_ReSTIRDIHistoryValid = false;
//Modify Begin:2026-08-10 by BestHui
        m_ReSTIRGIHistoryValid = false;
//Modify End
    }
//Modify End
    if (resetDenoiserHistory)
    {
        m_Denoisers.ResetHistory();
//Modify Begin:2026-08-07 by BestHui
        m_DLSS.InvalidateHistory();
        m_HasPreviousViewProjection = false;
//Modify End
    }
}

//Modify Begin:2026-07-30 by BestHui
void RaytracingDemo::SaveCurrentCameraToUnityScene()
{
//Modify Begin:2026-08-03 by BestHui
    if (m_Scene.GetSourcePath().extension() != ".unity")
    {
        throw std::runtime_error("Saving a JSON scene camera is not implemented yet.");
    }
//Modify End
    if (!m_HasSceneCamera || m_Scene.GetSourcePath().empty())
    {
        throw std::runtime_error("No source scene camera is loaded.");
    }
    if (m_Scene.GetCamera().SourceBinding.ParentTransformId != 0)
    {
        throw std::runtime_error("Saving camera with a parent transform is not supported yet.");
    }

    m_Scene.UpdateCamera(GetSceneCamera(), m_CameraFov, m_CameraNearClipPlane, m_CameraFarClipPlane);
    SceneImporter::WriteCameraToSourceFile(m_Scene.GetSourcePath(), m_Scene.GetCamera());
    m_CameraSaveStatus = "Camera saved to Unity scene.";
}

void RaytracingDemo::SaveCurrentScene()
{
    if (!m_HasSceneCamera || m_Scene.GetSourcePath().empty())
    {
        throw std::runtime_error("No source scene camera is loaded.");
    }

    m_Scene.UpdateCamera(GetSceneCamera(), m_CameraFov, m_CameraNearClipPlane, m_CameraFarClipPlane);
    m_Lights.ApplyToScene(m_Scene);
    std::filesystem::path runtimeStatePath = m_Scene.GetSourcePath();
    runtimeStatePath += ".runtime.json";
    SceneImporter::WriteJsonRuntimeState(runtimeStatePath, m_Scene);
    m_CameraSaveStatus = "Runtime scene state saved to " + runtimeStatePath.filename().string() + ".";
}
//Modify End
//Modify End

//Modify Begin:2026-07-30 by BestHui
RaytracingDemoPassResources RaytracingDemo::CreatePassResources()
{
    return {
        m_SceneResources,
        m_Lights,
        m_PathTracingPipelines,
//Modify Begin:2026-08-05 by BestHui
        m_DirectLightingReSTIRDI,
//Modify Begin:2026-07-30 by BestHui
        m_DirectLightingReSTIRDIPass,
//Modify End
//Modify End
//Modify Begin:2026-08-10 by BestHui
        m_IndirectLightingReSTIRGI,
        m_IndirectLightingReSTIRGIPass,
//Modify End
//Modify Begin:2026-08-07 by BestHui
        m_DLSS,
        m_DLSSRayReconstructionPrepareShader,
//Modify End
        m_Denoisers,
        m_CudaBloom,
        m_GBufferShader,
        m_GBufferMeshletIndirectShader,
        m_GBufferTaskMeshShader,
        m_MeshletCullShader,
        m_MeshletDrawCommandSignature.get(),
        m_DisplayCompositeShader,
        m_SkyboxComputeShader,
        m_SkyboxEquirectangularComputeShader,
//Modify Begin:2026-08-06 by BestHui
        m_SkyboxCubemapStripComputeShader,
//Modify End
        m_SkyboxTexture,
        m_DisplayBlitMesh,
        GetSceneCamera(),
        m_FrameworkDeviceContext.GetDevice(),
//Modify Begin:2026-08-07 by BestHui
        m_FrameworkDeviceContext.GetD3D12DeviceContext(),
//Modify End
        m_FrameworkDeviceContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT),
        m_FrameworkDeviceContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE),
//Modify Begin:2026-08-11 by BestHui
        &m_GpuTimestampProfiler
//Modify End
    };
}

RaytracingDemoPassConfig RaytracingDemo::CreatePassConfig() const
{
    return {
        m_RenderGraphFrameState,
    };
}
//Modify End

//Modify Begin:2026-07-30 by BestHui
void RaytracingDemo::UpdateRenderGraphFrameState()
{
    RaytracingDemoFrameState& state = *m_RenderGraphFrameState;
    state.Backend = m_PathTracingBackend;
//Modify Begin:2026-07-30 by BestHui
    state.ShadingModel = m_MaterialShadingModel;
//Modify End
    state.DirectLightingTechnique = m_DirectLightingTechnique;
    state.IndirectLightingTechnique = m_IndirectLightingTechnique;
    state.AsyncComputeEnabled = m_AsyncComputeEnabled;
    state.UseMeshletGBuffer = m_UseMeshletGBuffer;
    state.UseTaskShaderMeshlets = m_UseTaskShaderMeshlets;
    state.DebugMeshletClusters = m_DebugMeshletClusters;
    state.SkyboxEnabled = m_SkyboxEnabled;
    state.DebugLightingTextureTarget = m_DebugLightingTextureTarget;
    state.DebugTextureTarget = m_DebugTextureTarget;
    state.MaxBounces = m_MaxBounces;
//Modify Begin:2026-08-07 by BestHui
    const uint32_t displayWidth = static_cast<uint32_t>((std::max)(m_Width, 1));
    const uint32_t displayHeight = static_cast<uint32_t>((std::max)(m_Height, 1));
    const DLSSOptimalSettings dlssSettings = m_DLSS.GetOptimalSettings(displayWidth, displayHeight);
    state.DLSSEnabled = m_DLSS.IsEnabled();
    state.RayReconstructionEnabled = state.DLSSEnabled && m_DLSS.IsRayReconstructionEnabled();
    state.FrameGenerationEnabled = m_DLSS.IsFrameGenerationEnabled();
    state.DlssMode = m_DLSS.GetMode();
    state.Width = dlssSettings.RenderWidth;
    state.Height = dlssSettings.RenderHeight;
    state.DisplayWidth = displayWidth;
    state.DisplayHeight = displayHeight;
    state.DLSSSharpness = dlssSettings.Sharpness;
    state.DLSSJitterOffset = m_DLSS.GetJitterOffset(m_FrameIndex);
    state.View = GetSceneCamera().GetViewMatrix();
    state.Projection = GetSceneCamera().GetProjectionMatrix();
    if (state.DLSSEnabled)
    {
        state.Projection.r[2].m128_f32[0] += 2.0f * state.DLSSJitterOffset.x / static_cast<float>(state.Width);
        state.Projection.r[2].m128_f32[1] -= 2.0f * state.DLSSJitterOffset.y / static_cast<float>(state.Height);
    }
    state.ViewProjection = state.View * state.Projection;
//Modify End
    state.AccumulationEnabled = m_AccumulationEnabled;
    state.FrameIndex = m_FrameIndex;
    state.AccumulationFrameIndex = m_AccumulationFrameIndex;
    state.ReSTIRDIHistoryValid = m_ReSTIRDIHistoryValid;
//Modify Begin:2026-08-10 by BestHui
    state.ReSTIRGIHistoryValid = m_ReSTIRGIHistoryValid;
//Modify End
//Modify Begin:2026-08-11 by BestHui
    state.ReSTIRGIStageTimingEnabled = m_GpuTimingEnabled && m_ReSTIRGIStageTimingEnabled;
//Modify End
    state.HasPreviousViewProjection = m_HasPreviousViewProjection;
    state.PreviousViewProjection = m_PreviousViewProjection;
}
//Modify End

void RaytracingDemo::OnRender(RenderEventArgs& e)
{
    Base::OnRender(e);

//Modify Begin:2026-07-29 by BestHui
    const auto directCommandQueue = m_FrameworkDeviceContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
//Modify Begin:2026-08-03 by BestHui
    const auto asyncComputeCommandQueue = m_FrameworkDeviceContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE);
//Modify End
//Modify Begin:2026-08-03 by BestHui
    const auto collectGpuTimingFrames = [this](
        GpuTimestampProfiler& profiler,
        CommandQueue& commandQueue,
        std::vector<GpuTimestampSample>& latestSamples,
        const char* queueName)
    {
        bool collectedAnyFrame = false;
        std::vector<GpuTimestampSample> completedSamples;
        while (profiler.CollectCompletedFrame(commandQueue, completedSamples))
        {
            latestSamples = completedSamples;
            if (m_RenderGraphTimingCaptureEnabled)
            {
                m_RenderGraphTimingHistory.Record(
                    profiler.GetLastCollectedFrameNumber(),
                    queueName,
                    completedSamples);
            }
            collectedAnyFrame = true;
        }
        return collectedAnyFrame;
    };
    const bool collectedDirectGpuTimingFrame = m_GpuTimingEnabled && collectGpuTimingFrames(
        m_GpuTimestampProfiler,
        *directCommandQueue,
        m_GpuTimestampSamples,
        "Direct");
    const bool collectedAsyncComputeGpuTimingFrame = m_GpuTimingEnabled && collectGpuTimingFrames(
        m_AsyncComputeGpuTimestampProfiler,
        *asyncComputeCommandQueue,
        m_AsyncComputeGpuTimestampSamples,
        "AsyncCompute");
//Modify End
    if ((collectedDirectGpuTimingFrame || collectedAsyncComputeGpuTimingFrame) &&
        e.TotalTime - m_LastGpuTimingUiUpdateTime >= 0.25)
    {
        if (collectedDirectGpuTimingFrame)
        {
            m_GpuTimestampDisplaySamples = m_GpuTimestampSamples;
        }
        if (collectedAsyncComputeGpuTimingFrame)
        {
            m_AsyncComputeGpuTimestampDisplaySamples = m_AsyncComputeGpuTimestampSamples;
        }
        m_LastGpuTimingUiUpdateTime = e.TotalTime;
    }
//Modify End

    if (m_ImGui != nullptr)
    {
        m_ImGui->BeginFrame();
        OnImGui();
        m_ImGui->Render();
    }

//Modify Begin:2026-07-30 by BestHui
    ApplyStressTestSpheresState();
//Modify End

//Modify Begin:2026-08-07 by BestHui
    if (m_DLSS.IsFrameGenerationEnabled())
    {
        m_DLSS.BeginFrameGeneration(m_FrameIndex);
    }
//Modify End

//Modify Begin:2026-08-07 by BestHui
    UpdateRenderGraphFrameState();
    RenderGraph::RenderMetadata metadata;
    metadata.m_ScreenWidth = m_RenderGraphFrameState->Width;
    metadata.m_ScreenHeight = m_RenderGraphFrameState->Height;
    metadata.m_DisplayWidth = m_RenderGraphFrameState->DisplayWidth;
    metadata.m_DisplayHeight = m_RenderGraphFrameState->DisplayHeight;
//Modify End
    metadata.m_FrameIndex = m_FrameIndex;
    metadata.m_Time = e.TotalTime;

//Modify Begin:2026-07-28 by BestHui
    EnsureRenderGraphTopology();
//Modify End
//Modify Begin:2026-08-07 by BestHui
    if (m_RenderGraphFrameState->FrameGenerationEnabled)
    {
        m_FrameGenerationInputs = {};
        m_FrameGenerationInputs.Depth = m_RenderGraph->GetTexture(RaytracingDemoRenderGraph::ResourceIds::DepthBuffer);
        m_FrameGenerationInputs.MotionVectors = m_RenderGraph->GetTexture(RaytracingDemoRenderGraph::ResourceIds::MotionVector);
        m_FrameGenerationInputs.HudLessColor = m_RenderGraph->GetTexture(RaytracingDemoRenderGraph::ResourceIds::FrameGenerationHudLess);
        m_FrameGenerationInputs.RenderWidth = m_RenderGraphFrameState->Width;
        m_FrameGenerationInputs.RenderHeight = m_RenderGraphFrameState->Height;
        m_FrameGenerationInputs.DisplayWidth = m_RenderGraphFrameState->DisplayWidth;
        m_FrameGenerationInputs.DisplayHeight = m_RenderGraphFrameState->DisplayHeight;
        m_FrameGenerationInputs.FrameIndex = m_FrameIndex;
        m_FrameGenerationInputs.HasPreviousViewProjection = m_RenderGraphFrameState->HasPreviousViewProjection;
        m_FrameGenerationInputs.JitterOffset = m_RenderGraphFrameState->DLSSJitterOffset;
        m_FrameGenerationInputs.View = m_RenderGraphFrameState->View;
        m_FrameGenerationInputs.Projection = m_RenderGraphFrameState->Projection;
        m_FrameGenerationInputs.ViewProjection = m_RenderGraphFrameState->ViewProjection;
        m_FrameGenerationInputs.PreviousViewProjection = m_RenderGraphFrameState->PreviousViewProjection;
        m_DLSS.PrepareFrameGeneration(m_FrameGenerationInputs);
    }
//Modify End
//Modify Begin:2026-07-29 by BestHui
    m_RenderGraph->SetGpuTimestampProfiler(m_GpuTimingEnabled ? &m_GpuTimestampProfiler : nullptr);
//Modify Begin:2026-08-03 by BestHui
    m_RenderGraph->SetAsyncComputeGpuTimestampProfiler(
        m_GpuTimingEnabled ? &m_AsyncComputeGpuTimestampProfiler : nullptr);
//Modify Begin:2026-07-30 by BestHui
    m_RenderGraph->SetDebugSerializeAsyncCompute(m_DebugSerializeAsyncCompute);
//Modify End
//Modify End
//Modify End
//Modify Begin:2026-08-02 by BestHui
    const auto renderGraphCpuStart = std::chrono::steady_clock::now();
//Modify End
//Modify Begin:2026-07-28 by BestHui
//Modify Begin:2026-08-10 by BestHui
    BindlessDescriptorHeap& bindlessDescriptorHeap = m_SceneResources.GetBindlessDescriptorHeap();
    bindlessDescriptorHeap.BeginFrame(*directCommandQueue, *asyncComputeCommandQueue);
    bool bindlessFrameEnded = false;
    const auto endBindlessFrame = [this, &bindlessDescriptorHeap, &bindlessFrameEnded]()
    {
        if (bindlessFrameEnded)
        {
            return;
        }

        const RenderGraph::RenderGraphQueueFenceValues& frameFences = m_RenderGraph->GetFrameSubmissionFences();
        bindlessDescriptorHeap.EndFrame(frameFences.Direct, frameFences.AsyncCompute);
        bindlessFrameEnded = true;
    };
//Modify End
    try
    {
        m_RenderGraph->Execute(metadata);
//Modify Begin:2026-08-10 by BestHui
        endBindlessFrame();
//Modify End
    }
    catch (const std::exception& exception)
    {
//Modify Begin:2026-08-10 by BestHui
        endBindlessFrame();
//Modify End
        throw std::runtime_error(std::string("RaytracingDemo::OnRender RenderGraph.Execute failed: ") + exception.what());
    }
//Modify Begin:2026-08-02 by BestHui
    m_LastRenderGraphCpuMilliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - renderGraphCpuStart).count();
//Modify End

    try
    {
        PresentDisplayOutput();
    }
    catch (const std::exception& exception)
    {
//Modify Begin:2026-08-03 by BestHui
        const char* backendName = m_PathTracingBackend == PathTracingBackend::InlineRayQuery
            ? "InlineRayQuery"
            : "ShaderTableDxr";
        throw std::runtime_error(
            std::string("RaytracingDemo::OnRender PresentDisplayOutput failed: ") + exception.what() +
            " [Backend=" + backendName +
            ", AsyncCompute=" + (m_AsyncComputeEnabled ? "true" : "false") + "]");
//Modify End
    }
//Modify End

    ++m_FrameIndex;
//Modify Begin:2026-08-06 by BestHui
    if (m_AccumulationEnabled)
    {
        ++m_AccumulationFrameIndex;
    }
    else
    {
        m_AccumulationFrameIndex = 0;
    }
//Modify Begin:2026-08-05 by BestHui
    m_ReSTIRDIHistoryValid =
        m_PathTracingBackend == PathTracingBackend::InlineRayQuery &&
        m_DirectLightingTechnique == RaytracingDemoLightingTechnique::ReSTIRDI;
//Modify End
//Modify Begin:2026-08-10 by BestHui
    m_ReSTIRGIHistoryValid =
        m_PathTracingBackend == PathTracingBackend::InlineRayQuery &&
        m_IndirectLightingTechnique == RaytracingDemoLightingTechnique::ReSTIRGI &&
        m_MaxBounces > 1;
//Modify End

//Modify Begin:2026-08-07 by BestHui
    m_PreviousViewProjection = m_RenderGraphFrameState->ViewProjection;
//Modify End
    m_HasPreviousViewProjection = true;
}
