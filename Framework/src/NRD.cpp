//Modify Begin:2026-07-27 by BestHui
#include <Framework/NRD.h>

#include <DX12Library/Application.h>
#include <DX12Library/CommandList.h>
#include <DX12Library/CommandQueue.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/Texture.h>
#include <Framework/ComputeShader.h>
#include <Framework/NRDInputAdapter_CS.h>
#include <Framework/NRDOutputComposite_CS.h>
#include <Framework/ShaderBlob.h>
#include <Framework/ShaderResourceView.h>
#include <Framework/UnorderedAccessView.h>

#include "../../External/NRD/Include/NRD.h"
#include <NRDDescs.h>
#include <NRDSettings.h>
#include <NRI.h>
#include <Extensions/NRIHelper.h>
#include <Extensions/NRIWrapperD3D12.h>

#include <d3dx12.h>

#include <array>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(min)
#undef min
#endif

#if defined(max)
#undef max
#endif

#include <NRDIntegration.hpp>

using namespace DirectX;

namespace FrameworkNRD
{
    constexpr DXGI_FORMAT RadianceFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
    constexpr DXGI_FORMAT NormalRoughnessFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    constexpr DXGI_FORMAT ViewZFormat = DXGI_FORMAT_R32_FLOAT;
    constexpr DXGI_FORMAT MotionFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
}

class NRDImpl
{
public:
    ~NRDImpl()
    {
        Integration.Destroy();
        DestroyWrappedTextures();
        if (Device != nullptr)
        {
            nri::nriDestroyDevice(Device);
            Device = nullptr;
        }
    }

    void DestroyWrappedTextures()
    {
        for (const WrappedTexture& wrappedTexture : WrappedTextures)
        {
            if (wrappedTexture.Texture != nullptr)
            {
                Core.DestroyTexture(wrappedTexture.Texture);
            }
        }
        WrappedTextures.clear();
    }

    nri::Texture* GetWrappedTexture(const std::shared_ptr<Texture>& texture, const DXGI_FORMAT format)
    {
        ID3D12Resource* resource = texture->GetD3D12Resource().Get();
        for (const WrappedTexture& wrappedTexture : WrappedTextures)
        {
            if (wrappedTexture.Resource == resource)
            {
                return wrappedTexture.Texture;
            }
        }

        nri::TextureD3D12Desc desc = {};
        desc.d3d12Resource = resource;
        desc.format = static_cast<DXGIFormat>(format);

        nri::Texture* nriTexture = nullptr;
        if (WrapperD3D12.CreateTextureD3D12(*Device, desc, nriTexture) != nri::Result::SUCCESS)
        {
            return nullptr;
        }

        WrappedTextures.push_back({ resource, nriTexture });
        return nriTexture;
    }

    bool EnsureDevice(ID3D12Device* d3d12Device, ID3D12CommandQueue* d3d12Queue)
    {
        if (Device != nullptr)
        {
            return true;
        }

        nri::QueueFamilyD3D12Desc queueDesc = {};
        queueDesc.d3d12Queues = &d3d12Queue;
        queueDesc.queueNum = 1;
        queueDesc.queueType = nri::QueueType::GRAPHICS;

        nri::DeviceCreationD3D12Desc deviceDesc = {};
        deviceDesc.d3d12Device = d3d12Device;
        deviceDesc.queueFamilies = &queueDesc;
        deviceDesc.queueFamilyNum = 1;
        deviceDesc.enableNRIValidation = false;
        deviceDesc.disableD3D12EnhancedBarriers = true;
        deviceDesc.disableNVAPIInitialization = true;

        if (nriCreateDeviceFromD3D12Device(deviceDesc, Device) != nri::Result::SUCCESS)
        {
            Device = nullptr;
            return false;
        }

        if (nriGetInterface(*Device, NRI_INTERFACE(nri::CoreInterface), &Core) != nri::Result::SUCCESS ||
            nriGetInterface(*Device, NRI_INTERFACE(nri::WrapperD3D12Interface), &WrapperD3D12) != nri::Result::SUCCESS)
        {
            nri::nriDestroyDevice(Device);
            Device = nullptr;
            Core = {};
            WrapperD3D12 = {};
            return false;
        }

        return true;
    }

    nrd::Integration Integration;
    nri::Device* Device = nullptr;
    nri::CoreInterface Core = {};
    nri::WrapperD3D12Interface WrapperD3D12 = {};

private:
    struct WrappedTexture
    {
        ID3D12Resource* Resource = nullptr;
        nri::Texture* Texture = nullptr;
    };

    std::vector<WrappedTexture> WrappedTextures;
};

namespace
{
    constexpr nrd::Identifier DIFFUSE_DENOISER_ID = 0;

    D3D12_RESOURCE_STATES GetD3D12ResourceStates(const nri::AccessBits accessBits)
    {
        D3D12_RESOURCE_STATES resourceStates = D3D12_RESOURCE_STATE_COMMON;

        if (accessBits & nri::AccessBits::SHADER_RESOURCE)
        {
            resourceStates |= D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }

        if (accessBits & (nri::AccessBits::SHADER_RESOURCE_STORAGE | nri::AccessBits::SCRATCH_BUFFER | nri::AccessBits::CLEAR_STORAGE))
        {
            resourceStates |= D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }

        if (accessBits & nri::AccessBits::COPY_SOURCE)
        {
            resourceStates |= D3D12_RESOURCE_STATE_COPY_SOURCE;
        }

        if (accessBits & nri::AccessBits::COPY_DESTINATION)
        {
            resourceStates |= D3D12_RESOURCE_STATE_COPY_DEST;
        }

        return resourceStates;
    }

    void StoreNRDColumnMajorMatrix(float* destination, const DirectX::XMMATRIX& matrix)
    {
        DirectX::XMFLOAT4X4 stored{};
        DirectX::XMStoreFloat4x4(&stored, matrix);
        std::memcpy(destination, &stored, sizeof(stored));
    }

    nrd::Resource MakeNRDResource(nri::Texture* texture, const bool isStorage, void* userArg)
    {
        nrd::Resource resource = {};
        resource.nri.texture = texture;
        resource.state.access = isStorage ? nri::AccessBits::SHADER_RESOURCE_STORAGE : nri::AccessBits::SHADER_RESOURCE;
        resource.state.layout = isStorage ? nri::Layout::SHADER_RESOURCE_STORAGE : nri::Layout::SHADER_RESOURCE;
        resource.state.stages = nri::StageBits::COMPUTE_SHADER;
        resource.userArg = userArg;
        return resource;
    }

    void TransitionRaw(
        CommandList& commandList,
        const std::shared_ptr<Texture>& texture,
        const D3D12_RESOURCE_STATES beforeState,
        const D3D12_RESOURCE_STATES afterState)
    {
        if (beforeState == afterState)
        {
            return;
        }

        const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(texture->GetD3D12Resource().Get(), beforeState, afterState);
        commandList.GetGraphicsCommandList()->ResourceBarrier(1, &barrier);
    }

    std::unique_ptr<ComputeShader> CreateReflectedComputeShader(const void* shaderBytecode, const size_t shaderBytecodeSize)
    {
        ShaderBlob shader(shaderBytecode, shaderBytecodeSize);
        return std::make_unique<ComputeShader>(
            shader,
            ComputePipelineDescBuilder::ReflectedDefault(shader).Build());
    }

}

NRD::NRD()
    : m_PrepareShader(CreateReflectedComputeShader(ShaderBytecode_NRDInputAdapter_CS, sizeof ShaderBytecode_NRDInputAdapter_CS))
    , m_CompositeShader(CreateReflectedComputeShader(ShaderBytecode_NRDOutputComposite_CS, sizeof ShaderBytecode_NRDOutputComposite_CS))
    , m_Impl(std::make_unique<NRDImpl>())
{
    char* bypass = nullptr;
    size_t bypassLength = 0;
    _dupenv_s(&bypass, &bypassLength, "RAYTRACING_DEMO_NRD_BYPASS");
    m_BypassDenoise = bypass != nullptr && std::strcmp(bypass, "1") == 0;
    std::free(bypass);

    m_Available = true;
}

NRD::~NRD() = default;

void NRD::ResetHistory()
{
    m_FrameIndex = 0;
    m_HasPreviousFrame = false;
}

bool NRD::EnsureCreated(const uint32_t width, const uint32_t height)
{
    if (!m_Available)
    {
        return false;
    }

    if (m_Width == width && m_Height == height && m_CreatedMode == m_Settings.Mode)
    {
        return true;
    }

    const auto device = Application::Get().GetDevice();
    const auto queue = Application::Get().GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT)->GetD3D12CommandQueue();
    ID3D12CommandQueue* rawQueue = queue.Get();

    if (!m_Impl->EnsureDevice(device.Get(), rawQueue))
    {
        m_Available = false;
        m_Width = 0;
        m_Height = 0;
        return false;
    }

    Application::Get().Flush();

    m_Impl->Integration.Destroy();
    m_Impl->DestroyWrappedTextures();

    nrd::IntegrationCreationDesc integrationDesc = {};
    strcpy_s(integrationDesc.name, "FrameworkNRD");
    integrationDesc.resourceWidth = static_cast<uint16_t>(width);
    integrationDesc.resourceHeight = static_cast<uint16_t>(height);
    integrationDesc.queuedFrameNum = 128;
    integrationDesc.demoteFloat32to16 = false;
    integrationDesc.autoWaitForIdle = false;
    integrationDesc.enableWholeLifetimeDescriptorCaching = true;

    nrd::DenoiserDesc denoiserDesc = {};
    denoiserDesc.identifier = DIFFUSE_DENOISER_ID;
    denoiserDesc.denoiser = m_Settings.Mode == NRD::DenoiserMode::ReblurDiffuse ? nrd::Denoiser::REBLUR_DIFFUSE : nrd::Denoiser::RELAX_DIFFUSE;

    nrd::InstanceCreationDesc instanceDesc = {};
    instanceDesc.denoisers = &denoiserDesc;
    instanceDesc.denoisersNum = 1;

    const nrd::Result result = m_Impl->Integration.Recreate(integrationDesc, instanceDesc, m_Impl->Device);
    m_Available = result == nrd::Result::SUCCESS;
    if (!m_Available)
    {
        m_Width = 0;
        m_Height = 0;
        return false;
    }

    m_Width = width;
    m_Height = height;
    m_CreatedMode = m_Settings.Mode;
    ResetHistory();
    return true;
}

void NRD::PrepareInputs(
    CommandList& commandList,
    const FrameMatrices& frameMatrices,
    const std::shared_ptr<Texture>& gBufferSpecularSmoothness,
    const std::shared_ptr<Texture>& gBufferNormal,
    const std::shared_ptr<Texture>& gBufferPosition,
    const std::shared_ptr<Texture>& depthTexture,
    const std::shared_ptr<Texture>& motionVector,
    const std::shared_ptr<Texture>& nrdNormalRoughness,
    const std::shared_ptr<Texture>& nrdViewZ,
    const std::shared_ptr<Texture>& nrdMotion,
    const uint32_t width,
    const uint32_t height)
{
    PrepareConstants constants = {};
    const XMMATRIX currentView = frameMatrices.WorldToView;
    constants.WorldToView = currentView;
    constants.PreviousWorldToView = m_HasPreviousFrame ? m_PreviousView : currentView;
    constants.Width = width;
    constants.Height = height;

    m_PrepareShader->Bind(commandList);
    m_PrepareShader->SetConstantBuffer(commandList, "NRDInputAdapterConstants", constants);
    commandList.SetTexture(*m_PrepareShader, "GBufferSpecularSmoothness", ShaderResourceView(gBufferSpecularSmoothness));
    commandList.SetTexture(*m_PrepareShader, "GBufferNormal", ShaderResourceView(gBufferNormal));
    commandList.SetTexture(*m_PrepareShader, "GBufferPosition", ShaderResourceView(gBufferPosition));
    commandList.SetTexture(*m_PrepareShader, "DepthTexture", ShaderResourceView::DepthAsFloat(depthTexture));
    commandList.SetTexture(*m_PrepareShader, "MotionVector", ShaderResourceView(motionVector));
    m_PrepareShader->SetUnorderedAccessView(commandList, "NRDNormalRoughness", UnorderedAccessView(nrdNormalRoughness));
    m_PrepareShader->SetUnorderedAccessView(commandList, "NRDViewZ", UnorderedAccessView(nrdViewZ));
    m_PrepareShader->SetUnorderedAccessView(commandList, "NRDMotion", UnorderedAccessView(nrdMotion));
    m_PrepareShader->ApplyBindings(commandList);
    commandList.Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1u);
}

void NRD::PrepareDenoiserInputs(
    CommandList& commandList,
    const FrameMatrices& frameMatrices,
    const std::shared_ptr<Texture>& gBufferSpecularSmoothness,
    const std::shared_ptr<Texture>& gBufferNormal,
    const std::shared_ptr<Texture>& gBufferPosition,
    const std::shared_ptr<Texture>& depthTexture,
    const std::shared_ptr<Texture>& motionVector,
    const std::shared_ptr<Texture>& nrdNormalRoughness,
    const std::shared_ptr<Texture>& nrdViewZ,
    const std::shared_ptr<Texture>& nrdMotion,
    const uint32_t width,
    const uint32_t height)
{
    if (!IsEnabled() || !EnsureCreated(width, height) || m_BypassDenoise)
    {
        return;
    }

    PrepareInputs(commandList, frameMatrices, gBufferSpecularSmoothness, gBufferNormal, gBufferPosition, depthTexture, motionVector, nrdNormalRoughness, nrdViewZ, nrdMotion, width, height);
}

void NRD::Denoise(
    CommandList& commandList,
    const FrameMatrices& frameMatrices,
    const std::shared_ptr<Texture>& noisyRadiance,
    const std::shared_ptr<Texture>& nrdNormalRoughness,
    const std::shared_ptr<Texture>& nrdViewZ,
    const std::shared_ptr<Texture>& nrdMotion,
    const std::shared_ptr<Texture>& denoisedRadiance,
    const uint32_t width,
    const uint32_t height)
{
    nrd::CommonSettings commonSettings = {};
    const XMMATRIX currentView = frameMatrices.WorldToView;
    const XMMATRIX currentProjection = frameMatrices.ViewToClip;
    const XMMATRIX previousView = m_HasPreviousFrame ? m_PreviousView : currentView;
    const XMMATRIX previousProjection = m_HasPreviousFrame ? m_PreviousProjection : currentProjection;
    StoreNRDColumnMajorMatrix(commonSettings.viewToClipMatrix, currentProjection);
    StoreNRDColumnMajorMatrix(commonSettings.viewToClipMatrixPrev, previousProjection);
    StoreNRDColumnMajorMatrix(commonSettings.worldToViewMatrix, currentView);
    StoreNRDColumnMajorMatrix(commonSettings.worldToViewMatrixPrev, previousView);
//Modify Begin:2026-07-27 by BestHui
    commonSettings.motionVectorScale[0] = 1.0f / static_cast<float>(width);
    commonSettings.motionVectorScale[1] = 1.0f / static_cast<float>(height);
//Modify End
    commonSettings.motionVectorScale[2] = 1.0f;
    commonSettings.resourceSize[0] = static_cast<uint16_t>(width);
    commonSettings.resourceSize[1] = static_cast<uint16_t>(height);
    commonSettings.resourceSizePrev[0] = static_cast<uint16_t>(width);
    commonSettings.resourceSizePrev[1] = static_cast<uint16_t>(height);
    commonSettings.rectSize[0] = static_cast<uint16_t>(width);
    commonSettings.rectSize[1] = static_cast<uint16_t>(height);
    commonSettings.rectSizePrev[0] = static_cast<uint16_t>(width);
    commonSettings.rectSizePrev[1] = static_cast<uint16_t>(height);
    commonSettings.viewZScale = 1.0f;
    commonSettings.denoisingRange = m_Settings.DenoisingRange;
    const bool resetHistory = m_FrameIndex == 0;
    commonSettings.frameIndex = m_FrameIndex++;
    commonSettings.accumulationMode = resetHistory ? nrd::AccumulationMode::CLEAR_AND_RESTART : nrd::AccumulationMode::CONTINUE;
    commonSettings.isMotionVectorInWorldSpace = false;
    m_Impl->Integration.SetCommonSettings(commonSettings);
    m_PreviousView = currentView;
    m_PreviousProjection = currentProjection;
    m_HasPreviousFrame = true;

    if (m_Settings.Mode == NRD::DenoiserMode::ReblurDiffuse)
    {
        nrd::ReblurSettings reblurSettings = {};
        reblurSettings.hitDistanceParameters.A = m_Settings.ReblurHitDistanceA;
        reblurSettings.hitDistanceParameters.B = m_Settings.ReblurHitDistanceB;
        reblurSettings.hitDistanceParameters.C = m_Settings.ReblurHitDistanceC;
        reblurSettings.maxAccumulatedFrameNum = m_Settings.ReblurMaxAccumulatedFrameNum;
        reblurSettings.maxFastAccumulatedFrameNum = m_Settings.ReblurMaxFastAccumulatedFrameNum;
        reblurSettings.historyFixFrameNum = m_Settings.ReblurHistoryFixFrameNum;
        reblurSettings.historyFixBasePixelStride = m_Settings.ReblurHistoryFixBasePixelStride;
        reblurSettings.historyFixAlternatePixelStride = m_Settings.ReblurHistoryFixBasePixelStride;
        reblurSettings.fastHistoryClampingSigmaScale = m_Settings.ReblurFastHistoryClampingSigmaScale;
        reblurSettings.diffusePrepassBlurRadius = m_Settings.ReblurDiffusePrepassBlurRadius;
        reblurSettings.minHitDistanceWeight = m_Settings.ReblurMinHitDistanceWeight;
        reblurSettings.minBlurRadius = m_Settings.ReblurMinBlurRadius;
        reblurSettings.maxBlurRadius = m_Settings.ReblurMaxBlurRadius;
        reblurSettings.lobeAngleFraction = m_Settings.ReblurLobeAngleFraction;
        reblurSettings.roughnessFraction = m_Settings.ReblurRoughnessFraction;
        reblurSettings.planeDistanceSensitivity = m_Settings.ReblurPlaneDistanceSensitivity;
        reblurSettings.fireflySuppressorMinRelativeScale = m_Settings.ReblurFireflySuppressorMinRelativeScale;
        reblurSettings.hitDistanceReconstructionMode = nrd::HitDistanceReconstructionMode::AREA_3X3;
        reblurSettings.enableAntiFirefly = m_Settings.ReblurEnableAntiFirefly;
        m_Impl->Integration.SetDenoiserSettings(DIFFUSE_DENOISER_ID, &reblurSettings);
    }
    else
    {
        nrd::RelaxSettings relaxSettings = {};
        relaxSettings.diffuseMaxAccumulatedFrameNum = m_Settings.RelaxDiffuseMaxAccumulatedFrameNum;
        relaxSettings.diffuseMaxFastAccumulatedFrameNum = m_Settings.RelaxDiffuseMaxFastAccumulatedFrameNum;
        relaxSettings.historyFixFrameNum = m_Settings.RelaxHistoryFixFrameNum;
        relaxSettings.historyFixBasePixelStride = m_Settings.RelaxHistoryFixBasePixelStride;
        relaxSettings.historyFixAlternatePixelStride = m_Settings.RelaxHistoryFixBasePixelStride;
        relaxSettings.fastHistoryClampingSigmaScale = m_Settings.RelaxFastHistoryClampingSigmaScale;
        relaxSettings.diffusePrepassBlurRadius = m_Settings.RelaxDiffusePrepassBlurRadius;
        relaxSettings.minHitDistanceWeight = m_Settings.RelaxMinHitDistanceWeight;
        relaxSettings.spatialVarianceEstimationHistoryThreshold = m_Settings.RelaxSpatialVarianceEstimationHistoryThreshold;
        relaxSettings.diffusePhiLuminance = m_Settings.RelaxDiffusePhiLuminance;
        relaxSettings.lobeAngleFraction = m_Settings.RelaxLobeAngleFraction;
        relaxSettings.roughnessFraction = m_Settings.RelaxRoughnessFraction;
        relaxSettings.atrousIterationNum = m_Settings.RelaxAtrousIterationNum;
        relaxSettings.depthThreshold = m_Settings.RelaxDepthThreshold;
        relaxSettings.luminanceEdgeStoppingRelaxation = m_Settings.RelaxLuminanceEdgeStoppingRelaxation;
        relaxSettings.normalEdgeStoppingRelaxation = m_Settings.RelaxNormalEdgeStoppingRelaxation;
        relaxSettings.roughnessEdgeStoppingRelaxation = m_Settings.RelaxRoughnessEdgeStoppingRelaxation;
        relaxSettings.hitDistanceReconstructionMode = nrd::HitDistanceReconstructionMode::AREA_3X3;
        relaxSettings.enableAntiFirefly = m_Settings.RelaxEnableAntiFirefly;
        relaxSettings.enableRoughnessEdgeStopping = m_Settings.RelaxEnableRoughnessEdgeStopping;
        m_Impl->Integration.SetDenoiserSettings(DIFFUSE_DENOISER_ID, &relaxSettings);
    }

//Modify Begin:2026-07-27 by BestHui
    m_Impl->Integration.NewFrame();
//Modify End

    nrd::ResourceSnapshot snapshot = {};
    snapshot.restoreInitialState = false;

    nri::Texture* noisyTexture = m_Impl->GetWrappedTexture(noisyRadiance, FrameworkNRD::RadianceFormat);
    nri::Texture* normalRoughnessTexture = m_Impl->GetWrappedTexture(nrdNormalRoughness, FrameworkNRD::NormalRoughnessFormat);
    nri::Texture* viewZTexture = m_Impl->GetWrappedTexture(nrdViewZ, FrameworkNRD::ViewZFormat);
    nri::Texture* motionTexture = m_Impl->GetWrappedTexture(nrdMotion, FrameworkNRD::MotionFormat);
    nri::Texture* denoisedTexture = m_Impl->GetWrappedTexture(denoisedRadiance, FrameworkNRD::RadianceFormat);
    if (noisyTexture == nullptr || normalRoughnessTexture == nullptr || viewZTexture == nullptr || motionTexture == nullptr || denoisedTexture == nullptr)
    {
        m_Available = false;
        return;
    }

    const D3D12_RESOURCE_STATES shaderResourceState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList.TransitionBarrier(*noisyRadiance, shaderResourceState);
    commandList.TransitionBarrier(*nrdNormalRoughness, shaderResourceState);
    commandList.TransitionBarrier(*nrdViewZ, shaderResourceState);
    commandList.TransitionBarrier(*nrdMotion, shaderResourceState);
    commandList.TransitionBarrier(*denoisedRadiance, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList.FlushResourceBarriers();

    snapshot.SetResource(nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST, MakeNRDResource(noisyTexture, false, noisyRadiance.get()));
    snapshot.SetResource(nrd::ResourceType::IN_NORMAL_ROUGHNESS, MakeNRDResource(normalRoughnessTexture, false, nrdNormalRoughness.get()));
    snapshot.SetResource(nrd::ResourceType::IN_VIEWZ, MakeNRDResource(viewZTexture, false, nrdViewZ.get()));
    snapshot.SetResource(nrd::ResourceType::IN_MV, MakeNRDResource(motionTexture, false, nrdMotion.get()));
    snapshot.SetResource(nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST, MakeNRDResource(denoisedTexture, true, denoisedRadiance.get()));

    nri::CommandBufferD3D12Desc commandBufferDesc = {};
    commandBufferDesc.d3d12CommandList = commandList.GetGraphicsCommandList().Get();
    commandBufferDesc.d3d12CommandAllocator = nullptr;

    nri::CommandBuffer* nriCommandBuffer = nullptr;
    if (m_Impl->WrapperD3D12.CreateCommandBufferD3D12(*m_Impl->Device, commandBufferDesc, nriCommandBuffer) != nri::Result::SUCCESS)
    {
        m_Available = false;
        return;
    }

    const nrd::Identifier denoiser = DIFFUSE_DENOISER_ID;
    m_Impl->Integration.Denoise(&denoiser, 1, *nriCommandBuffer, snapshot);
    m_Impl->Core.DestroyCommandBuffer(nriCommandBuffer);

    D3D12_RESOURCE_STATES noisyState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    D3D12_RESOURCE_STATES normalRoughnessState = noisyState;
    D3D12_RESOURCE_STATES viewZState = noisyState;
    D3D12_RESOURCE_STATES motionState = noisyState;
    D3D12_RESOURCE_STATES denoisedState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    for (size_t i = 0; i < snapshot.uniqueNum; ++i)
    {
        const nrd::Resource& resource = snapshot.unique[i];
        if (resource.userArg == noisyRadiance.get())
        {
            noisyState = GetD3D12ResourceStates(resource.state.access);
        }
        else if (resource.userArg == nrdNormalRoughness.get())
        {
            normalRoughnessState = GetD3D12ResourceStates(resource.state.access);
        }
        else if (resource.userArg == nrdViewZ.get())
        {
            viewZState = GetD3D12ResourceStates(resource.state.access);
        }
        else if (resource.userArg == nrdMotion.get())
        {
            motionState = GetD3D12ResourceStates(resource.state.access);
        }
        else if (resource.userArg == denoisedRadiance.get())
        {
            denoisedState = GetD3D12ResourceStates(resource.state.access);
        }
    }

    const auto uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(denoisedRadiance->GetD3D12Resource().Get());
    commandList.GetGraphicsCommandList()->ResourceBarrier(1, &uavBarrier);
    TransitionRaw(commandList, noisyRadiance, noisyState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    TransitionRaw(commandList, nrdNormalRoughness, normalRoughnessState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    TransitionRaw(commandList, nrdViewZ, viewZState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    TransitionRaw(commandList, nrdMotion, motionState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    TransitionRaw(commandList, denoisedRadiance, denoisedState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    commandList.InvalidateCachedNativeState();
}

void NRD::Composite(
    CommandList& commandList,
    const std::shared_ptr<Texture>& denoisedRadiance,
    const std::shared_ptr<Texture>& depthTexture,
    const std::shared_ptr<Texture>& gBufferAlbedoOcclusion,
    const std::shared_ptr<Texture>& gBufferEmissionMetallic,
    const std::shared_ptr<Texture>& output,
    const uint32_t width,
    const uint32_t height)
{
    CompositeConstants constants = {};
    constants.Width = width;
    constants.Height = height;
    constants.DenoiserMode = static_cast<uint32_t>(m_Settings.Mode);

    m_CompositeShader->Bind(commandList);
    m_CompositeShader->SetConstantBuffer(commandList, "NRDOutputCompositeConstants", constants);
    commandList.SetTexture(*m_CompositeShader, "DenoisedRadiance", ShaderResourceView(denoisedRadiance));
    commandList.SetTexture(*m_CompositeShader, "DepthTexture", ShaderResourceView::DepthAsFloat(depthTexture));
    commandList.SetTexture(*m_CompositeShader, "GBufferAlbedoOcclusion", ShaderResourceView(gBufferAlbedoOcclusion));
    commandList.SetTexture(*m_CompositeShader, "GBufferEmissionMetallic", ShaderResourceView(gBufferEmissionMetallic));
    m_CompositeShader->SetUnorderedAccessView(commandList, "Output", UnorderedAccessView(output));
    m_CompositeShader->ApplyBindings(commandList);
    commandList.Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1u);
}

void NRD::Execute(
    CommandList& commandList,
    const FrameMatrices& frameMatrices,
    const std::shared_ptr<Texture>& noisyRadiance,
    const std::shared_ptr<Texture>& gBufferAlbedoOcclusion,
    const std::shared_ptr<Texture>& gBufferEmissionMetallic,
    const std::shared_ptr<Texture>& depthTexture,
    const std::shared_ptr<Texture>& nrdNormalRoughness,
    const std::shared_ptr<Texture>& nrdViewZ,
    const std::shared_ptr<Texture>& nrdMotion,
    const std::shared_ptr<Texture>& denoisedRadiance,
    const std::shared_ptr<Texture>& output,
    const uint32_t width,
    const uint32_t height)
{
    if (!IsEnabled() || !EnsureCreated(width, height))
    {
        return;
    }

    if (m_BypassDenoise)
    {
        return;
    }

    Denoise(commandList, frameMatrices, noisyRadiance, nrdNormalRoughness, nrdViewZ, nrdMotion, denoisedRadiance, width, height);
    Composite(commandList, denoisedRadiance, depthTexture, gBufferAlbedoOcclusion, gBufferEmissionMetallic, output, width, height);
}
//Modify End
