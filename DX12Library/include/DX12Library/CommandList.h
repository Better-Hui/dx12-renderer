// ReSharper disable CppRedundantQualifier
#pragma once

/*
 *  Copyright(c) 2018 Jeremiah van Oosten
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files(the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions :
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 */

/**
 *  @file CommandList.h
 *  @date October 22, 2018
 *  @author Jeremiah van Oosten
 *
 *  @brief CommandList class encapsulates a ID3D12GraphicsCommandList2 interface.
 *  The CommandList class provides additional functionality that makes working with
 *  DirectX 12 applications easier.
 */

#include <cassert>

#include <d3d12.h>
#include <wrl.h>

#include <functional>
#include <memory> // for std::unique_ptr
#include <string>
#include <vector> // for std::vector

#include "ClearValue.h"
#include "RenderTargetState.h"
#include "ResourceStateRegistry.h"
#include "UploadBuffer.h"

class CommandListInternalAccess;
class ConstantBuffer;
//Modify Begin:2026-08-12 by Hui
class D3D12DeviceContext;
//Modify End
class DynamicDescriptorHeap;
class IndexBuffer;
class RenderTarget;
class Resource;
class ResourceStateTracker;
class RootSignature;
class Texture;
class UploadBuffer;
class VertexBuffer;

class CommandList
{
public:
//Modify Begin:2026-08-07 by Hui
    CommandList(
        D3D12_COMMAND_LIST_TYPE type,
        std::shared_ptr<D3D12DeviceContext> deviceContext);
//Modify End
    virtual ~CommandList();

    /**
     * Get the type of command list.
     */
    D3D12_COMMAND_LIST_TYPE GetCommandListType() const
    {
        return m_D3d12CommandListType;
    }

    /**
     * Get direct access to the ID3D12GraphicsCommandList2 interface.
     */
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> GetGraphicsCommandList() const
    {
        return m_D3d12CommandList;
    }

//Modify Begin:2026-07-30 by Hui
    /**
     * Execute a third-party command-recording callback against the native D3D12
     * command list. The callback owns any state it changes. CommandList flushes
     * its pending work before the call and invalidates its state cache afterwards.
     */
    void ExecuteExternalCommandRecording(
        const std::function<void(ID3D12GraphicsCommandList2&)>& recordCommands);
//Modify End

    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList5> GetGraphicsCommandList5() const
    {
        return m_D3d12CommandList5;
    }

//Modify Begin:2026-07-30 by Hui
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList6> GetGraphicsCommandList6() const
    {
        return m_D3d12CommandList6;
    }
//Modify End
//Modify Begin:2026-08-12 by Hui
    const std::shared_ptr<D3D12DeviceContext>& GetDeviceContext() const
    {
        return m_DeviceContext;
    }
//Modify End

    /**
     * Transition a resource to a particular state.
     *
     * @param resource The resource to transition.
     * @param stateAfter The state to transition the resource to. The before state is resolved by the resource state tracker.
     * @param subresource The subresource to transition. By default, this is D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES which indicates that all subresources are transitioned to the same state.
     * @param flushBarriers Force flush any barriers. Resource barriers need to be flushed before a command (draw, dispatch, or copy) that expects the resource to be in a particular state can run.
     */
    void TransitionBarrier(const Resource& resource, D3D12_RESOURCE_STATES stateAfter,
        UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, bool flushBarriers = false);
    void TransitionBarrier(Microsoft::WRL::ComPtr<ID3D12Resource> resource, D3D12_RESOURCE_STATES stateAfter,
        UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, bool flushBarriers = false);

    /**
     * Add a UAV barrier to ensure that any writes to a resource have completed
     * before reading from the resource.
     *
     * @param resource The resource to add a UAV barrier for.
     * @param flushBarriers Force flush any barriers. Resource barriers need to be
     * flushed before a command (draw, dispatch, or copy) that expects the resource
     * to be in a particular state can run.
     */
    void UavBarrier(const Resource& resource, bool flushBarriers = false);
//Modify Begin:2026-07-30 by Hui
    void UavBarrier(ID3D12Resource* resource, bool flushBarriers = false);
//Modify End

    /**
     * Add an aliasing barrier to indicate a transition between usages of two
     * different resources that occupy the same space in a heap.
     *
     * @param beforeResource The resource that currently occupies the heap.
     * @param afterResource The resource that will occupy the space in the heap.
     */
    void AliasingBarrier(const Resource& beforeResource, const Resource& afterResource, bool flushBarriers = false);
    void AliasingBarrier(Microsoft::WRL::ComPtr<ID3D12Resource> beforeResource,
        Microsoft::WRL::ComPtr<ID3D12Resource> afterResource, bool flushBarriers = false);
//Modify Begin:2026-08-10 by Hui
    void AliasingBarrierBeforeFirstUse(const Resource& resourceAfter);
//Modify End

    /**
     * Copy resources.
     */
    void CopyResource(const Resource& dstRes, const Resource& srcRes);
    void CopyResource(Microsoft::WRL::ComPtr<ID3D12Resource> dstRes, Microsoft::WRL::ComPtr<ID3D12Resource> srcRes, bool dstAutoBarriers = true, bool srcAutoBarriers = true);
//Modify Begin:2026-08-12 by Hui
    void CopyBufferRegion(
        const Resource& destination,
        uint64_t destinationOffset,
        Microsoft::WRL::ComPtr<ID3D12Resource> source,
        uint64_t sourceOffset,
        uint64_t sizeInBytes);
//Modify End

    /**
     * Resolve a multisampled resource into a non-multisampled resource.
     */
    void ResolveSubresource(const Resource& dstRes, const Resource& srcRes, uint32_t dstSubresource = 0,
        uint32_t srcSubresource = 0);

    /**
     * Set the current primitive topology for the rendering pipeline.
     */
    void SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY primitiveTopology) const;

    /**
     * Clear a texture.
     */
    void ClearTexture(const Texture& texture, const float clearColor[4]);
    void ClearTexture(const Texture& texture, const ClearValue& clearValue);

    /**
     * Clear depth/stencil texture.
     */
    void ClearDepthStencilTexture(const Texture& texture, D3D12_CLEAR_FLAGS clearFlags, float depth = 1.0f,
        uint8_t stencil = 0);

    /**
     * Set a dynamic constant buffer data to an inline descriptor in the root
     * signature.
     */
    void SetGraphicsDynamicConstantBuffer(uint32_t rootParameterIndex, size_t sizeInBytes,
        const void* bufferData) const;

    template <typename T>
    void SetGraphicsDynamicConstantBuffer(uint32_t rootParameterIndex, const T& data)
    {
        SetGraphicsDynamicConstantBuffer(rootParameterIndex, sizeof(T), &data);
    }

    void SetComputeDynamicConstantBuffer(uint32_t rootParameterIndex, size_t sizeInBytes,
        const void* bufferData) const;

    template <typename T>
    void SetComputeDynamicConstantBuffer(uint32_t rootParameterIndex, const T& data)
    {
        SetComputeDynamicConstantBuffer(rootParameterIndex, sizeof(T), &data);
    }

    /**
     * Set a set of 32-bit constants on the graphics pipeline.
     */
    void SetGraphics32BitConstants(uint32_t rootParameterIndex, uint32_t numConstants, const void* constants);

    template <typename T>
    void SetGraphics32BitConstants(uint32_t rootParameterIndex, const T& constants)
    {
        static_assert(sizeof(T) % sizeof(uint32_t) == 0, "Size of type must be a multiple of 4 bytes");
        SetGraphics32BitConstants(rootParameterIndex, sizeof(T) / sizeof(uint32_t), &constants);
    }

    /**
     * Set a set of 32-bit constants on the compute pipeline.
     */
    void SetCompute32BitConstants(uint32_t rootParameterIndex, uint32_t numConstants, const void* constants);

    template <typename T>
    void SetCompute32BitConstants(uint32_t rootParameterIndex, const T& constants)
    {
        static_assert(sizeof(T) % sizeof(uint32_t) == 0, "Size of type must be a multiple of 4 bytes");
        SetCompute32BitConstants(rootParameterIndex, sizeof(T) / sizeof(uint32_t), &constants);
    }

    /**
     * Set the vertex buffer to the rendering pipeline.
     */
    void SetVertexBuffer(uint32_t slot, const VertexBuffer& vertexBuffer);

    /**
     * Set dynamic vertex buffer data to the rendering pipeline.
     */
    //void SetDynamicVertexBuffer(uint32_t slot, size_t numVertices, size_t vertexSize, const void* vertexBufferData);

    /*template <typename T>
    void SetDynamicVertexBuffer(uint32_t slot, const std::vector<T>& vertexBufferData)
    {
        SetDynamicVertexBuffer(slot, vertexBufferData.size(), sizeof(T), vertexBufferData.data());
    }*/

    /**
     * Bind the index buffer to the rendering pipeline.
     */
    void SetIndexBuffer(const IndexBuffer& indexBuffer);

    /**
     * Bind dynamic index buffer data to the rendering pipeline.
     */
    /*void SetDynamicIndexBuffer(size_t numIndicies, DXGI_FORMAT indexFormat, const void* indexBufferData);

    template <typename T>
    void SetDynamicIndexBuffer(const std::vector<T>& indexBufferData)
    {
        static_assert(sizeof(T) == 2 || sizeof(T) == 4);

        DXGI_FORMAT indexFormat = (sizeof(T) == 2) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
        SetDynamicIndexBuffer(indexBufferData.size(), indexFormat, indexBufferData.data());
    }*/

    /**
     * Set dynamic structured buffer contents.
     */
    void SetGraphicsDynamicStructuredBuffer(uint32_t slot, size_t numElements, size_t elementSize,
        const void* bufferData) const;

    template <typename T>
    void SetGraphicsDynamicStructuredBuffer(const uint32_t slot, const std::vector<T>& bufferData)
    {
        SetGraphicsDynamicStructuredBuffer(slot, bufferData.size(), sizeof(T), bufferData.data());
    }

    /**
     * Set viewports.
     */
    void SetViewport(const D3D12_VIEWPORT& viewport);
    void SetViewports(const std::vector<D3D12_VIEWPORT>& viewports);

    /**
     * Set scissor rects.
     */
    void SetScissorRect(const D3D12_RECT& scissorRect);
    void SetScissorRects(const std::vector<D3D12_RECT>& scissorRects);

    /**
     * Set the pipeline state object on the command list.
     */
    void SetPipelineState(const Microsoft::WRL::ComPtr<ID3D12PipelineState>& pipelineState);

    /**
     * Set the current root signature on the command list.
     */
    void SetGraphicsRootSignature(const RootSignature& rootSignature);
    void SetComputeRootSignature(const RootSignature& rootSignature);
    void SetGraphicsAndComputeRootSignature(const RootSignature& rootSignature);

    /**
     * Set the SRV on the graphics pipeline.
     */
    void SetShaderResourceView(
        uint32_t rootParameterIndex,
        uint32_t descriptorOffset,
        const Resource& resource,
        D3D12_RESOURCE_STATES stateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        UINT firstSubresource = 0,
        UINT numSubresources = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        const D3D12_SHADER_RESOURCE_VIEW_DESC* srv = nullptr
    );

    /**
     * Set the UAV on the graphics pipeline.
     */
    void SetUnorderedAccessView(
        uint32_t rootParameterIndex,
        uint32_t descriptorOffset,
        const Resource& resource,
        D3D12_RESOURCE_STATES stateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        UINT firstSubresource = 0,
        UINT numSubresources = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        const D3D12_UNORDERED_ACCESS_VIEW_DESC* uavDesc = nullptr
    );

    /**
     * Set the SRV on the graphics pipeline.
     */
    void SetShaderResourceView(
        uint32_t rootParameterIndex,
        uint32_t descriptorOffset,
        const Resource& resource,
        UINT firstSubresource = 0,
        UINT numSubresources = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        const D3D12_SHADER_RESOURCE_VIEW_DESC* srv = nullptr
    );

//Modify Begin:2026-07-21 by Hui
    void SetGlobalTexture(
        uint32_t rootParameterIndex,
        uint32_t descriptorOffset,
        const Resource& texture,
        UINT firstSubresource = 0,
        UINT numSubresources = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        const D3D12_SHADER_RESOURCE_VIEW_DESC* srv = nullptr
    );

    void SetGlobalTexture(
        uint32_t rootParameterIndex,
        const Resource& texture,
        UINT firstSubresource = 0,
        UINT numSubresources = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        const D3D12_SHADER_RESOURCE_VIEW_DESC* srv = nullptr
    );

    void SetTexture(
        uint32_t rootParameterIndex,
        uint32_t descriptorOffset,
        const Resource& texture,
        UINT firstSubresource = 0,
        UINT numSubresources = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        const D3D12_SHADER_RESOURCE_VIEW_DESC* srv = nullptr
    );

    void SetTexture(
        uint32_t rootParameterIndex,
        const Resource& texture,
        UINT firstSubresource = 0,
        UINT numSubresources = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        const D3D12_SHADER_RESOURCE_VIEW_DESC* srv = nullptr
    );

    template <typename ShaderLike, typename TextureLike>
    void SetTexture(ShaderLike& shader, const std::string& variableName, const TextureLike& texture)
    {
        shader.SetTexture(*this, variableName, texture);
    }

    template <typename ShaderLike, typename TextureLike>
    void SetTexture(std::shared_ptr<ShaderLike>& shader, const std::string& variableName, const TextureLike& texture)
    {
        shader->SetTexture(*this, variableName, texture);
    }

    template <typename ShaderLike, typename TextureLike>
    void SetTexture(const std::shared_ptr<ShaderLike>& shader, const std::string& variableName, const TextureLike& texture)
    {
        shader->SetTexture(*this, variableName, texture);
    }

    template <typename ShaderLike, typename TextureLike>
    void SetTexture(std::unique_ptr<ShaderLike>& shader, const std::string& variableName, const TextureLike& texture)
    {
        shader->SetTexture(*this, variableName, texture);
    }

    template <typename ShaderLike, typename TextureLike>
    void SetTexture(const std::unique_ptr<ShaderLike>& shader, const std::string& variableName, const TextureLike& texture)
    {
        shader->SetTexture(*this, variableName, texture);
    }

//Modify Begin:2026-07-28 by Hui
    template <typename ShaderLike, typename BufferLike>
    void SetConstantBuffer(ShaderLike& shader, const std::string& variableName, const BufferLike& data)
    {
        shader.SetConstantBuffer(*this, variableName, data);
    }

    template <typename ShaderLike, typename BufferLike>
    void SetConstantBuffer(std::shared_ptr<ShaderLike>& shader, const std::string& variableName, const BufferLike& data)
    {
        shader->SetConstantBuffer(*this, variableName, data);
    }

    template <typename ShaderLike, typename BufferLike>
    void SetConstantBuffer(const std::shared_ptr<ShaderLike>& shader, const std::string& variableName, const BufferLike& data)
    {
        shader->SetConstantBuffer(*this, variableName, data);
    }

    template <typename ShaderLike, typename BufferLike>
    void SetConstantBuffer(std::unique_ptr<ShaderLike>& shader, const std::string& variableName, const BufferLike& data)
    {
        shader->SetConstantBuffer(*this, variableName, data);
    }

    template <typename ShaderLike, typename BufferLike>
    void SetConstantBuffer(const std::unique_ptr<ShaderLike>& shader, const std::string& variableName, const BufferLike& data)
    {
        shader->SetConstantBuffer(*this, variableName, data);
    }

    template <typename ShaderLike, typename UavLike>
    void SetUnorderedAccessView(ShaderLike& shader, const std::string& variableName, const UavLike& unorderedAccessView)
    {
        shader.SetUnorderedAccessView(*this, variableName, unorderedAccessView);
    }

    template <typename ShaderLike, typename UavLike>
    void SetUnorderedAccessView(std::shared_ptr<ShaderLike>& shader, const std::string& variableName, const UavLike& unorderedAccessView)
    {
        shader->SetUnorderedAccessView(*this, variableName, unorderedAccessView);
    }

    template <typename ShaderLike, typename UavLike>
    void SetUnorderedAccessView(const std::shared_ptr<ShaderLike>& shader, const std::string& variableName, const UavLike& unorderedAccessView)
    {
        shader->SetUnorderedAccessView(*this, variableName, unorderedAccessView);
    }

    template <typename ShaderLike, typename UavLike>
    void SetUnorderedAccessView(std::unique_ptr<ShaderLike>& shader, const std::string& variableName, const UavLike& unorderedAccessView)
    {
        shader->SetUnorderedAccessView(*this, variableName, unorderedAccessView);
    }

    template <typename ShaderLike, typename UavLike>
    void SetUnorderedAccessView(const std::unique_ptr<ShaderLike>& shader, const std::string& variableName, const UavLike& unorderedAccessView)
    {
        shader->SetUnorderedAccessView(*this, variableName, unorderedAccessView);
    }
//Modify End
//Modify End

    /**
     * Set the UAV on the graphics pipeline.
     */
    void SetUnorderedAccessView(
        uint32_t rootParameterIndex,
        uint32_t descriptorOffset,
        const Resource& resource,
        UINT firstSubresource = 0,
        UINT numSubresources = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        const D3D12_UNORDERED_ACCESS_VIEW_DESC* uavDesc = nullptr
    );

    void SetStencilRef(UINT8 stencilRef);

    /**
     * Set the render targets for the graphics rendering pipeline.
     */
    void SetRenderTarget(const RenderTarget& renderTarget, UINT texArrayIndex = -1, UINT mipLevel = 0, bool useDepth = true, bool readonlyDepth = false);

    void ClearRenderTarget(const RenderTarget& renderTarget, const float* clearColor, D3D12_CLEAR_FLAGS clearFlags);
    void ClearRenderTarget(const RenderTarget& renderTarget, const ClearValue& clearValue, D3D12_CLEAR_FLAGS clearFlags);

    void DiscardResource(const Resource& resource);

    /**
     * Draw geometry.
     */
    void Draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t startVertex = 0, uint32_t startInstance = 0);
    void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t startIndex = 0, int32_t baseVertex = 0,
        uint32_t startInstance = 0);

    void DrawIndirect(
        const Microsoft::WRL::ComPtr<ID3D12CommandSignature>& pCommandSignature,
        uint32_t maxCommandCount,
        const Microsoft::WRL::ComPtr<ID3D12Resource>& pArgumentBuffer,
        uint64_t argumentBufferOffset,
        const Microsoft::WRL::ComPtr<ID3D12Resource>& pCountBuffer,
        uint64_t countBufferOffset = 0
    );

    /**
     * Dispatch a compute shader.
     */
    void Dispatch(uint32_t numGroupsX, uint32_t numGroupsY = 1, uint32_t numGroupsZ = 1);

//Modify Begin:2026-07-30 by Hui
    void DispatchMesh(uint32_t numGroupsX, uint32_t numGroupsY = 1, uint32_t numGroupsZ = 1);
//Modify End

//Modify Begin:2026-07-21 by Hui
    void SetRaytracingPipelineState(const Microsoft::WRL::ComPtr<ID3D12StateObject>& stateObject);
    void DispatchRays(const D3D12_DISPATCH_RAYS_DESC& dispatchRaysDesc);
    void BuildRaytracingAccelerationStructure(const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC& buildDesc);
    void StageDynamicDescriptors(
        D3D12_DESCRIPTOR_HEAP_TYPE heapType,
        UINT rootParameterIndex,
        UINT descriptorOffset,
        UINT numDescriptors,
        D3D12_CPU_DESCRIPTOR_HANDLE srcDescriptor);
//Modify End
//Modify Begin:2026-07-30 by Hui
    void BindExternalDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType, ID3D12DescriptorHeap* heap);
//Modify End

    /***************************************************************************
     * Methods defined below are only intended to be used by internal classes. *
     ***************************************************************************/

    /**
     * Close the command list.
     * Used by the command queue.
     *
     * @param pendingCommandList The command list that is used to execute pending
     * resource barriers (if any) for this command list.
     *
     * @return true if there are any pending resource barriers that need to be
     * processed.
     */
//Modify Begin:2026-07-30 by Hui
    bool Close(
        CommandList& pendingCommandList,
        ResourceStateRegistry::SubmissionScope& submissionScope);
//Modify End
    // Just close the command list. This is useful for pending command lists.
    void Close();

    /**
     * Reset the command list. This should only be called by the CommandQueue
     * before the command list is returned from CommandQueue::GetCommandList.
     */
    void Reset();

    /**
     * Release tracked objects. Useful if the swap chain needs to be resized.
     */
    void ReleaseTrackedObjects();

    /**
     * Set the currently bound descriptor heap.
     * Should only be called by the DynamicDescriptorHeap class.
     */
    void SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType, ID3D12DescriptorHeap* heap);
    void SetComputeRootUnorderedAccessView(UINT rootParameterIndex, const Resource& resource);
    void SetComputeRootShaderResourceView(UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS gpuAddress);
    void SetComputeRootConstantBufferView(UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS gpuAddress);
//Modify Begin:2026-08-12 by Hui
    void SetGraphicsRootDescriptorTable(UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE descriptorHandle);
//Modify End
    void SetComputeRootDescriptorTable(UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE descriptorHandle);

    void SetAutomaticViewportAndScissorRect(const RenderTarget& renderTarget, UINT mipLevel = 0);
    void SetInfiniteScrissorRect();

    const RenderTargetFormats& GetLastRenderTargetFormats() const { return m_LastRenderTargetState.GetFormats(); }
    const RenderTargetState& GetLastRenderTargetState() const { return m_LastRenderTargetState; }

    void SetShadingRateImage(const Resource& resource);
    void ResetShadingRateImage();
    void SetShadingRate(const D3D12_SHADING_RATE& shadingRate, const D3D12_SHADING_RATE_COMBINER* combiners);

private:
//Modify Begin:2026-08-17 by Hui
    friend class CommandListInternalAccess;

    UploadBuffer::Allocation AllocateInUploadBuffer(size_t bufferSize, size_t alignment);
    void TrackObject(const Microsoft::WRL::ComPtr<ID3D12Object>& object);
    void TrackResource(const Resource& res);
    void FlushResourceBarriers();
    void NotifyResourceState(
        const Resource& resource,
        D3D12_RESOURCE_STATES state,
        UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
    void TrackResourceState(
        Microsoft::WRL::ComPtr<ID3D12Resource> resource,
        std::shared_ptr<ResourceStateRegistration> stateRegistration);
    void RetireResourceState(Microsoft::WRL::ComPtr<ID3D12Resource> resource);
    void RetireResource(Resource& resource);
    void CommitStagedDescriptors();
//Modify End
//Modify Begin:2026-07-30 by Hui
    void InvalidateCachedNativeState();
//Modify End
//Modify Begin:2026-08-12 by Hui
    void CommitStagedDescriptorsForDraw();
    void CommitStagedDescriptorsForDispatch();
//Modify End

    // Binds the current descriptor heaps to the command list.
    void BindDescriptorHeaps();

    using TrackedObjectsType = std::vector<Microsoft::WRL::ComPtr<ID3D12Object>>;

    D3D12_COMMAND_LIST_TYPE m_D3d12CommandListType;
//Modify Begin:2026-08-07 by Hui
//Modify Begin:2026-08-12 by Hui
    std::shared_ptr<D3D12DeviceContext> m_DeviceContext;
//Modify End
    Microsoft::WRL::ComPtr<ID3D12Device2> m_Device;
//Modify Begin:2026-07-30 by Hui
    std::shared_ptr<ResourceStateRegistry> m_ResourceStateRegistry;
//Modify End
//Modify End
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> m_D3d12CommandList;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList5> m_D3d12CommandList5;
//Modify Begin:2026-07-30 by Hui
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList6> m_D3d12CommandList6;
//Modify End
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_D3d12CommandAllocator;

    // Keep track of the currently bound root signatures to minimize root
    // signature changes.
    ID3D12RootSignature* m_RootSignature;

    // Resource created in an upload heap. Useful for drawing of dynamic geometry
    // or for uploading constant buffer data that changes every draw call.
    std::unique_ptr<UploadBuffer> m_PUploadBuffer;

    // Resource state tracker is used by the command list to track (per command list)
    // the current state of a resource. The resource state tracker also tracks the
    // global state of a resource in order to minimize resource state transitions.
    std::unique_ptr<ResourceStateTracker> m_PResourceStateTracker;

    // The dynamic descriptor heap allows for descriptors to be staged before
    // being committed to the command list. Dynamic descriptors need to be
    // committed before a Draw or Dispatch.
    std::unique_ptr<DynamicDescriptorHeap> m_DynamicDescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES];

    // Keep track of the currently bound descriptor heaps. Only change descriptor
    // heaps if they are different than the currently bound descriptor heaps.
    ID3D12DescriptorHeap* m_DescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES];
    // Objects that are being tracked by a command list that is "in-flight" on
    // the command-queue and cannot be deleted. To ensure objects are not deleted
    // until the command list is finished executing, a reference to the object
    // is stored. The referenced objects are released when the command list is
    // reset.
    TrackedObjectsType m_TrackedObjects;
//Modify Begin:2026-08-12 by Hui
    std::vector<std::shared_ptr<ResourceStateRegistration>> m_TrackedResourceStateRegistrations;
//Modify End

    RenderTargetState m_LastRenderTargetState;

};
