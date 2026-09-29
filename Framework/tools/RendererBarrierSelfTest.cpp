//Modify Begin:2026-09-29 by Hui
#include "RendererDiagnosticsCommands.h"
#include <DX12Library/Helpers.h>
#include <DX12Library/CommandList.h>
#include <DX12Library/CommandQueue.h>
#include <DX12Library/D3D12DeviceContext.h>
#include <DX12Library/ExternalCommandContext.h>
#include <DX12Library/RootSignature.h>
#include <DX12Library/StructuredBuffer.h>
#include <DX12Library/Texture.h>
#include <Framework/Core/FrameworkDeviceContext.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/Pipeline/ComputeShader.h>
#include <Framework/Rendering/Denoising/SVGF.h>
#include <Framework/Rendering/Texture/RenderTexture.h>
#include <Framework/Rendering/Texture/UnorderedAccessView.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <dxcapi.h>
#include <array>
#include <cstring>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
    using Microsoft::WRL::ComPtr;
    constexpr UINT ElementCount = 64;
    constexpr UINT ByteCount = ElementCount * sizeof(UINT);
    constexpr UINT Iterations = 8;

    ComPtr<IDxcBlob> Compile(const char* source)
    {
        ComPtr<IDxcCompiler3> compiler;
        ThrowIfFailed(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)));
        const DxcBuffer buffer{ source, std::strlen(source), DXC_CP_UTF8 };
        const wchar_t* args[]{ L"-E", L"main", L"-T", L"cs_6_0", L"-O0" };
        ComPtr<IDxcResult> result;
        ThrowIfFailed(compiler->Compile(&buffer, args, static_cast<UINT>(std::size(args)), nullptr, IID_PPV_ARGS(&result)));
        HRESULT status;
        ThrowIfFailed(result->GetStatus(&status));
        if (FAILED(status))
        {
            ComPtr<IDxcBlobUtf8> errors;
            result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
            throw std::runtime_error(errors ? errors->GetStringPointer() : "Test shader compilation failed.");
        }
        ComPtr<IDxcBlob> object;
        ThrowIfFailed(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr));
        return object;
    }

    void Expect(const bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    template<typename Action> void ExpectRejected(Action&& action, const char* message)
    {
        bool rejected = false;
        try { action(); } catch (const std::exception&) { rejected = true; }
        Expect(rejected, message);
    }

    void ExpectPixels(ID3D12Resource& readback, const UINT value)
    {
        const D3D12_RANGE range{ 0, ByteCount };
        UINT* values = nullptr;
        ThrowIfFailed(readback.Map(0, &range, reinterpret_cast<void**>(&values)));
        bool valid = true;
        for (UINT i = 0; i < ElementCount; ++i) valid &= values[i] == value;
        const D3D12_RANGE written{ 0, 0 };
        readback.Unmap(0, &written);
        Expect(valid, "GPU readback did not match the expected iteration count.");
    }
}

int RendererDiagnosticsTool::BarrierSelfTestCommand()
{
    ComPtr<ID3D12Debug> debug;
    ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));
    debug->EnableDebugLayer();
    ComPtr<ID3D12Debug1> gpuDebug;
    ThrowIfFailed(debug.As(&gpuDebug));
    gpuDebug->SetEnableGPUBasedValidation(TRUE);
    ComPtr<IDXGIFactory4> factory;
    ThrowIfFailed(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIAdapter> adapter;
    ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
    ComPtr<ID3D12Device2> device;
    ThrowIfFailed(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)));
    ComPtr<ID3D12InfoQueue> info;
    ThrowIfFailed(device.As(&info));
    auto registry = std::make_shared<ResourceStateRegistry>();
    auto deviceContext = std::make_shared<D3D12DeviceContext>(D3D12DeviceContextDesc{ device, registry });
    auto queue = std::make_shared<CommandQueue>(D3D12_COMMAND_LIST_TYPE_DIRECT, deviceContext);
    FrameworkDeviceContext framework({ .DeviceContext = deviceContext });
    const auto increment = Compile("RWStructuredBuffer<uint> Output : register(u0); [numthreads(64,1,1)] void main(uint3 i:SV_DispatchThreadID) { Output[i.x] += 1; }");
    const auto pingPong = Compile("StructuredBuffer<uint> Input : register(t0); RWStructuredBuffer<uint> Output : register(u0); [numthreads(64,1,1)] void main(uint3 i:SV_DispatchThreadID) { Output[i.x] = Input[i.x] + 1; }");
    const ShaderBlob incrementBlob(increment->GetBufferPointer(), increment->GetBufferSize());
    ComputeShader shader(framework, incrementBlob, ComputePipelineDescBuilder::ReflectedDefault(incrementBlob).Build());
    const auto initialize = Compile("RWStructuredBuffer<uint> Output:register(u0); [numthreads(64,1,1)] void main(uint3 i:SV_DispatchThreadID) { Output[i.x]=0; }");
    const ShaderBlob initializeBlob(initialize->GetBufferPointer(), initialize->GetBufferSize());
    ComputeShader initializeShader(framework, initializeBlob, ComputePipelineDescBuilder::ReflectedDefault(initializeBlob).Build());
    const D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(ByteCount, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    StructuredBuffer a(desc, ElementCount, sizeof(UINT), L"Barrier test A", deviceContext);
    StructuredBuffer b(desc, ElementCount, sizeof(UINT), L"Barrier test B", deviceContext);
    const CD3DX12_HEAP_PROPERTIES readbackHeap(D3D12_HEAP_TYPE_READBACK);
    const auto readbackDesc = CD3DX12_RESOURCE_DESC::Buffer(ByteCount);
    ComPtr<ID3D12Resource> readback;
    ThrowIfFailed(device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &readbackDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)));
    const UINT zeros[4]{};
    const auto resetBuffer = [&](CommandContext& ctx)
    {
        ctx.SetUnorderedAccessView(initializeShader, "Output", UnorderedAccessView(a));
        ctx.BindPipeline(initializeShader);
        ctx.BindDescriptorSet(initializeShader.GetDescriptorSet());
        ctx.Dispatch(1);
    };

    // No RenderGraph: reflected ctx binding survives copy and repeated dispatch.
    {
        auto cmd = queue->GetCommandList();
        CommandContext ctx(*cmd);
        resetBuffer(ctx);
        ctx.SetUnorderedAccessView(shader, "Output", UnorderedAccessView(a));
        ctx.BindPipeline(shader);
        ctx.BindDescriptorSet(shader.GetDescriptorSet());
        for (UINT i = 0; i < Iterations; ++i)
        {
            if (i == 4) cmd->CopyBufferToReadback(a, 0, readback, 0, ByteCount);
            if ((i & 1) == 0) ctx.Dispatch(1); else cmd->Dispatch(1);
        }
        cmd->CopyBufferToReadback(a, 0, readback, 0, ByteCount);
        queue->WaitForFenceValue(queue->ExecuteCommandList(cmd));
        ExpectPixels(*readback.Get(), Iterations);
    }

    // Low-level cmd descriptors alternate SRV/UAV roles without graph passes.
    {
        CD3DX12_DESCRIPTOR_RANGE1 ranges[2];
        ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
        ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
        CD3DX12_ROOT_PARAMETER1 parameters[2];
        parameters[0].InitAsDescriptorTable(1, &ranges[0]);
        parameters[1].InitAsDescriptorTable(1, &ranges[1]);
        const D3D12_ROOT_SIGNATURE_DESC1 signatureDesc{ 2, parameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE };
        RootSignature signature(signatureDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, *device.Get());
        D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc{};
        pipelineDesc.pRootSignature = signature.GetRootSignature().Get();
        pipelineDesc.CS = { pingPong->GetBufferPointer(), pingPong->GetBufferSize() };
        ComPtr<ID3D12PipelineState> pipeline;
        ThrowIfFailed(device->CreateComputePipelineState(&pipelineDesc, IID_PPV_ARGS(&pipeline)));
        auto cmd = queue->GetCommandList();
        CommandContext initContext(*cmd);
        resetBuffer(initContext);
        cmd->SetComputeRootSignature(signature);
        cmd->SetPipelineState(pipeline);
        for (UINT i = 0; i < Iterations; ++i)
        {
            cmd->SetShaderResourceView(0, 0, (i & 1) ? b : a);
            cmd->SetUnorderedAccessView(1, 0, (i & 1) ? a : b);
            cmd->Dispatch(1);
        }
        cmd->CopyBufferToReadback(a, 0, readback, 0, ByteCount);
        queue->WaitForFenceValue(queue->ExecuteCommandList(cmd));
        ExpectPixels(*readback.Get(), Iterations);
    }

    // The test harness is the host. Only it closes/submits the native list.
    for (const UINT mode : { 0u, 1u, 2u })
    {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList2> native;
        ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
        ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&native)));
        ExternalCommandContext external(D3D12_COMMAND_LIST_TYPE_DIRECT, deviceContext, native.Get());
        external.DeclareResource({ a.GetD3D12Resource().Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE, true, false });
        external.RegisterResource(a.GetCounterBuffer().GetD3D12Resource().Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        {
            CommandContext ctx(external);
            resetBuffer(ctx);
            ctx.SetUnorderedAccessView(shader, "Output", UnorderedAccessView(a));
            ctx.BindPipeline(shader);
            ctx.BindDescriptorSet(shader.GetDescriptorSet());
            for (UINT i = 0; i < Iterations; ++i) ctx.Dispatch(1);
        }
        if (mode == 2)
            ExpectRejected([&] { external.RecordNativeCommands([](ID3D12GraphicsCommandList2&) { throw std::runtime_error("Injected recording failure"); }); },
                "Native callback exception did not propagate.");
        if (mode != 0) external.Abort(); else external.End();
        ExpectRejected([&] { external.GetCommandList(); }, "Finished external recording is still accessible.");
        Expect(external.IsFinished(), "External context did not finish.");
        {
            auto scope = registry->AcquireSubmissionScope();
            Expect(scope.GetStates().at(a.GetD3D12Resource().Get()).GetSubresourceState(0) == D3D12_RESOURCE_STATE_COPY_SOURCE,
                "External final state was not committed to the registry.");
        }
        native->CopyBufferRegion(readback.Get(), 0, a.GetD3D12Resource().Get(), 0, ByteCount);
        ThrowIfFailed(native->Close());
        ID3D12CommandList* lists[]{ native.Get() };
        queue->GetD3D12CommandQueue()->ExecuteCommandLists(1, lists);
        queue->WaitForFenceValue(queue->Signal());
        ExpectPixels(*readback.Get(), Iterations);
    }
    // A compute queue works without RG; the harness explicitly orders queues.
    {
        CommandQueue compute(D3D12_COMMAND_LIST_TYPE_COMPUTE, deviceContext);
        auto cmd = compute.GetCommandList();
        CommandContext ctx(*cmd);
        resetBuffer(ctx);
        ctx.SetUnorderedAccessView(shader, "Output", UnorderedAccessView(a));
        ctx.BindPipeline(shader);
        ctx.BindDescriptorSet(shader.GetDescriptorSet());
        for (UINT i = 0; i < Iterations; ++i) ctx.Dispatch(1);
        cmd->CopyBufferToReadback(a, 0, readback, 0, ByteCount);
        compute.WaitForFenceValue(compute.ExecuteCommandList(cmd));
        ExpectPixels(*readback.Get(), Iterations);
    }

    // Exact declarations neither cover ALL nor overwrite an untouched mip.
    {
        auto mipTexture = RenderTexture::CreateUav2D(framework, DXGI_FORMAT_R32_UINT, 8, 8, L"Exact mip test", 1, 2);
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList2> native;
        ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
        ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&native)));
        ExpectRejected([&] { ExternalCommandContext invalid(D3D12_COMMAND_LIST_TYPE_COMPUTE, deviceContext, native.Get()); },
            "External command list type mismatch was accepted.");
        ExternalCommandContext external(D3D12_COMMAND_LIST_TYPE_DIRECT, deviceContext, native.Get());
        const ExternalCommandContext::ResourceAccess exact{ mipTexture->GetD3D12Resource().Get(), 0,
            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE, true, false };
        auto all = exact;
        all.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        const std::array overlap{ exact, all };
        ExpectRejected([&] { external.DeclareResources(overlap); }, "Overlapping declaration batch was accepted.");
        {
            auto scope = registry->AcquireSubmissionScope();
            Expect(scope.GetStates().at(exact.Resource).GetSubresourceState(0) == D3D12_RESOURCE_STATE_COMMON,
                "Rejected declaration batch changed the resource state.");
        }
        const std::array duplicates{ exact, exact };
        external.DeclareResources(duplicates);
        external.DeclareResources(duplicates);
        CommandContext ctx(external);
        ExpectRejected([&] { ctx.TransitionResource(*mipTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS); },
            "An exact declaration incorrectly covered ALL subresources.");
        ExpectRejected([&] { ctx.TransitionResource(*mipTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, false, 1); },
            "An exact declaration incorrectly covered another mip.");
        external.End();
        {
            auto scope = registry->AcquireSubmissionScope();
            const auto& state = scope.GetStates().at(exact.Resource);
            Expect(state.GetSubresourceState(0) == D3D12_RESOURCE_STATE_COPY_SOURCE &&
                state.GetSubresourceState(1) == D3D12_RESOURCE_STATE_COMMON, "Exact final state overwrote another mip.");
        }
        ThrowIfFailed(native->Close());
        ID3D12CommandList* lists[]{ native.Get() };
        queue->GetD3D12CommandQueue()->ExecuteCommandLists(1, lists);
        queue->WaitForFenceValue(queue->Signal());
    }

    // The real multi-stage SVGF algorithm records into a host-owned list.
    {
        SVGF svgf(framework);
        svgf.SetEnabled(true);
        svgf.GetSettings().AtrousIterations = 8;
        SVGF::RecordingResources resources;
        const auto texture = [&](DXGI_FORMAT format) { return RenderTexture::CreateUav2D(framework, format, 8, 8, L"SVGF test"); };
        resources.NoisyRadiance = texture(DXGI_FORMAT_R32G32B32A32_FLOAT);
        resources.GBufferNormal = texture(DXGI_FORMAT_R32G32B32A32_FLOAT);
        resources.GBufferPosition = texture(DXGI_FORMAT_R32G32B32A32_FLOAT);
        resources.MotionVector = texture(DXGI_FORMAT_R32G32_FLOAT);
        resources.Depth = RenderTexture::Create2D(
            framework,
            DXGI_FORMAT_R32_TYPELESS,
            8,
            8,
            L"SVGF depth",
            D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
        resources.Output = texture(DXGI_FORMAT_R32G32B32A32_FLOAT);
        resources.HistoryColorRead = texture(DXGI_FORMAT_R32G32B32A32_FLOAT);
        resources.HistoryColorWrite = texture(DXGI_FORMAT_R32G32B32A32_FLOAT);
        resources.HistoryMomentsRead = texture(DXGI_FORMAT_R32G32_FLOAT);
        resources.HistoryMomentsWrite = texture(DXGI_FORMAT_R32G32_FLOAT);
        resources.TemporalColor = texture(DXGI_FORMAT_R32G32B32A32_FLOAT);
        resources.TemporalMoments = texture(DXGI_FORMAT_R32G32_FLOAT);
        resources.Variance = texture(DXGI_FORMAT_R32_FLOAT);
        resources.Ping = texture(DXGI_FORMAT_R32G32B32A32_FLOAT);
        resources.Pong = texture(DXGI_FORMAT_R32G32B32A32_FLOAT);
        const std::shared_ptr<Texture> textures[]{ resources.NoisyRadiance, resources.GBufferNormal, resources.GBufferPosition,
            resources.MotionVector, resources.Depth, resources.Output, resources.HistoryColorRead, resources.HistoryColorWrite,
            resources.HistoryMomentsRead, resources.HistoryMomentsWrite, resources.TemporalColor, resources.TemporalMoments,
            resources.Variance, resources.Ping, resources.Pong };
        const auto initColor = Compile("RWTexture2D<float4> Output:register(u0); [numthreads(8,8,1)] void main(uint3 i:SV_DispatchThreadID) { Output[i.xy]=float4(1,2,3,1); }");
        const ShaderBlob colorBlob(initColor->GetBufferPointer(), initColor->GetBufferSize());
        ComputeShader colorShader(framework, colorBlob, ComputePipelineDescBuilder::ReflectedDefault(colorBlob).Build());
        auto setup = queue->GetCommandList();
        CommandContext setupContext(*setup);
        for (const auto& item : textures)
            if (item != resources.Depth)
                setupContext.ClearUnorderedAccessUint(*item, zeros);
        setupContext.SetUnorderedAccessView(colorShader, "Output", UnorderedAccessView(resources.NoisyRadiance));
        setupContext.BindPipeline(colorShader);
        setupContext.BindDescriptorSet(colorShader.GetDescriptorSet());
        setupContext.Dispatch(1);
        setupContext.TransitionResource(*resources.Depth, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        setup->ClearDepthStencilTexture(*resources.Depth, D3D12_CLEAR_FLAG_DEPTH, 0.5f);
        setupContext.TransitionResource(*resources.Depth, D3D12_RESOURCE_STATE_COPY_SOURCE);
        for (const auto& item : textures) setupContext.TransitionResource(*item, D3D12_RESOURCE_STATE_COPY_SOURCE);
        queue->WaitForFenceValue(queue->ExecuteCommandList(setup));

        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList2> native;
        ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
        ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&native)));
        ExternalCommandContext external(D3D12_COMMAND_LIST_TYPE_DIRECT, deviceContext, native.Get());
        for (const auto& item : textures) external.RegisterResource(item->GetD3D12Resource().Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
        CommandContext context(external);
        svgf.Record(context, resources);
        std::swap(resources.HistoryColorRead, resources.HistoryColorWrite);
        std::swap(resources.HistoryMomentsRead, resources.HistoryMomentsWrite);
        resources.HistoryValid = true;
        svgf.Record(context, resources);
        external.End();

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT64 size = 0;
        const auto outputDesc = resources.Output->GetD3D12Resource()->GetDesc();
        device->GetCopyableFootprints(&outputDesc, 0, 1, 0, &footprint, nullptr, nullptr, &size);
        ComPtr<ID3D12Resource> textureReadback;
        const auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(size);
        ThrowIfFailed(device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&textureReadback)));
        const CD3DX12_TEXTURE_COPY_LOCATION dst(textureReadback.Get(), footprint);
        const CD3DX12_TEXTURE_COPY_LOCATION src(resources.Output->GetD3D12Resource().Get(), 0);
        native->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        ThrowIfFailed(native->Close());
        ID3D12CommandList* lists[]{ native.Get() };
        queue->GetD3D12CommandQueue()->ExecuteCommandLists(1, lists);
        queue->WaitForFenceValue(queue->Signal());
        const D3D12_RANGE range{ 0, static_cast<SIZE_T>(size) };
        unsigned char* pixels = nullptr;
        ThrowIfFailed(textureReadback->Map(0, &range, reinterpret_cast<void**>(&pixels)));
        bool valid = true;
        for (UINT y = 0; y < 8; ++y)
            for (UINT x = 0; x < 8; ++x)
            {
                const auto* pixel = reinterpret_cast<const float*>(pixels + y * footprint.Footprint.RowPitch) + x * 4;
                for (UINT channel = 0; channel < 4; ++channel)
                    valid &= std::isfinite(pixel[channel]) && std::abs(pixel[channel] - (channel == 3 ? 1.0f : float(channel + 1))) < 0.01f;
            }
        const D3D12_RANGE written{ 0, 0 };
        textureReadback->Unmap(0, &written);
        Expect(valid, "Graphless SVGF did not preserve constant radiance across ping-pong iterations.");
    }
    queue->Flush();
    UINT64 errors = 0;
    for (UINT64 i = 0; i < info->GetNumStoredMessages(); ++i)
    {
        SIZE_T size = 0;
        info->GetMessage(i, nullptr, &size);
        std::vector<unsigned char> bytes(size);
        auto* message = reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
        ThrowIfFailed(info->GetMessage(i, message, &size));
        if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR)
        {
            ++errors;
            std::cerr << message->pDescription << '\n';
        }
    }
    Expect(errors == 0, "D3D12 validation reported resource-state errors.");
    std::cout << "{\"barrier_selftest\":\"pass\",\"adapter\":\"WARP\",\"gpu_validation\":true,\"cases\":8,\"iterations\":8,\"svgf_frames\":2,\"debug_errors\":0}\n";
    return 0;
}
//Modify End
