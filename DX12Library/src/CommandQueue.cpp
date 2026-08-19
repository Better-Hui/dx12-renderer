#include "DX12LibPCH.h"

#include "CommandQueue.h"

#include "CommandList.h"
//Modify Begin:2026-08-07 by Hui
#include "D3D12DeviceContext.h"
//Modify End

//Modify Begin:2026-07-29 by Hui
#include <fstream>
#include <chrono>
//Modify End

//Modify Begin:2026-08-07 by Hui
CommandQueue::CommandQueue(
	const D3D12_COMMAND_LIST_TYPE type,
	std::shared_ptr<D3D12DeviceContext> deviceContext)
	: m_CommandListType(type)
	, m_DeviceContext(std::move(deviceContext))
	, m_FenceValue(0)
	, m_IsProcessingInFlightCommandLists(true)
{
	Assert(m_DeviceContext != nullptr, "D3D12 device context is null.");
	const Microsoft::WRL::ComPtr<ID3D12Device2>& device = m_DeviceContext->GetDevice();

	D3D12_COMMAND_QUEUE_DESC desc = {};
	desc.Type = type;
	desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	desc.NodeMask = 0;

	ThrowIfFailed(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&m_D3d12CommandQueue)));
	InitializeFenceAndWorker();
}

CommandQueue::CommandQueue(
	const D3D12_COMMAND_LIST_TYPE type,
	std::shared_ptr<D3D12DeviceContext> deviceContext,
	ID3D12CommandQueue* externalCommandQueue)
	: m_CommandListType(type)
	, m_DeviceContext(std::move(deviceContext))
	, m_D3d12CommandQueue(externalCommandQueue)
	, m_FenceValue(0)
	, m_IsProcessingInFlightCommandLists(true)
{
	Assert(m_DeviceContext != nullptr, "D3D12 device context is null.");
	Assert(m_D3d12CommandQueue != nullptr, "External command queue is null.");
	Assert(m_D3d12CommandQueue->GetDesc().Type == type, "External command queue type does not match the wrapper type.");
	InitializeFenceAndWorker();
}

void CommandQueue::InitializeFenceAndWorker()
{
	ThrowIfFailed(m_DeviceContext->GetDevice()->CreateFence(m_FenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_D3d12Fence)));

	switch (m_CommandListType)
	{
	case D3D12_COMMAND_LIST_TYPE_COPY:
		m_D3d12CommandQueue->SetName(L"Copy Command Queue");
		break;
	case D3D12_COMMAND_LIST_TYPE_COMPUTE:
		m_D3d12CommandQueue->SetName(L"Compute Command Queue");
		break;
	case D3D12_COMMAND_LIST_TYPE_DIRECT:
		m_D3d12CommandQueue->SetName(L"Direct Command Queue");
		break;
	}

	m_ProcessInFlightCommandListsThread = std::thread(&CommandQueue::ProcessInFlightCommandLists, this);
}

void CommandQueue::SetFatalErrorHandler(CommandQueueFailureHandler handler)
{
	m_FatalErrorHandler = std::move(handler);
}

CommandQueue::~CommandQueue()
{
	m_IsProcessingInFlightCommandLists = false;
	m_ProcessInFlightCommandListsThread.join();
}

uint64_t CommandQueue::Signal()
{
	uint64_t fenceValue = ++m_FenceValue;
	m_D3d12CommandQueue->Signal(m_D3d12Fence.Get(), fenceValue);
	return fenceValue;
}

bool CommandQueue::IsFenceComplete(uint64_t fenceValue)
{
	return m_D3d12Fence->GetCompletedValue() >= fenceValue;
}

void CommandQueue::WaitForFenceValue(uint64_t fenceValue)
{
	if (!IsFenceComplete(fenceValue))
	{
		auto event = ::CreateEvent(nullptr, FALSE, FALSE, nullptr);
		assert(event && "Failed to create fence event handle.");

		// Is this function thread safe?
		m_D3d12Fence->SetEventOnCompletion(fenceValue, event);
		WaitForSingleObject(event, DWORD_MAX);

		CloseHandle(event);
	}
}

void CommandQueue::Flush()
{
	std::unique_lock<std::mutex> lock(m_ProcessInFlightCommandListsThreadMutex);
	m_ProcessInFlightCommandListsThreadCv.wait(lock, [this] { return m_InFlightCommandLists.Empty(); });

	// In case the command queue was signaled directly 
	// using the CommandQueue::Signal method then the 
	// fence value of the command queue might be higher than the fence
	// value of any of the executed command lists.
	WaitForFenceValue(m_FenceValue);
}

std::shared_ptr<CommandList> CommandQueue::GetCommandList()
{
	std::shared_ptr<CommandList> commandList;

	// TryPop is the atomic availability check. Empty followed by TryPop is racy
	// when multiple recording workers request command lists concurrently.
	if (m_AvailableCommandLists.TryPop(commandList))
	{
		assert(commandList != nullptr);
	}
	else
	{
		// Otherwise create a new command list.
		commandList = std::make_shared<CommandList>(m_CommandListType, m_DeviceContext);
	}

	return commandList;
}

// Execute a command list.
// Returns the fence value to wait for for this command list.
uint64_t CommandQueue::ExecuteCommandList(std::shared_ptr<CommandList> commandList)
{
	return ExecuteCommandLists(std::vector<std::shared_ptr<CommandList>>({ commandList }));
}

uint64_t CommandQueue::ExecuteCommandLists(const std::vector<std::shared_ptr<CommandList>>& commandLists)
{
	auto submissionScope = m_DeviceContext->GetResourceStateRegistry()->AcquireSubmissionScope();

	// Command lists that need to put back on the command list queue.
	std::vector<std::shared_ptr<CommandList>> toBeQueued;
	toBeQueued.reserve(commandLists.size() * 2); // 2x since each command list will have a pending command list.

	// Command lists that need to be executed.
	std::vector<ID3D12CommandList*> d3d12CommandLists;
	d3d12CommandLists.reserve(commandLists.size() * 2); // 2x since each command list will have a pending command list.

	for (auto commandList : commandLists)
	{
		auto pendingCommandList = GetCommandList();
		bool hasPendingBarriers = commandList->Close(*pendingCommandList, submissionScope);
		pendingCommandList->Close();
		// If there are no pending barriers on the pending command list, there is no reason to 
		// execute an empty command list on the command queue.
		if (hasPendingBarriers)
		{
			d3d12CommandLists.push_back(pendingCommandList->GetGraphicsCommandList().Get());
		}
		d3d12CommandLists.push_back(commandList->GetGraphicsCommandList().Get());

		toBeQueued.push_back(pendingCommandList);
		toBeQueued.push_back(commandList);
	}

	UINT numCommandLists = static_cast<UINT>(d3d12CommandLists.size());
	m_D3d12CommandQueue->ExecuteCommandLists(numCommandLists, d3d12CommandLists.data());
	uint64_t fenceValue = Signal();

	// Queue command lists for reuse.
	for (auto commandList : toBeQueued)
	{
		m_InFlightCommandLists.Push({ fenceValue, commandList });
	}
	m_ProcessInFlightCommandListsThreadCv.notify_one();
	return fenceValue;
}

void CommandQueue::Wait(const CommandQueue& other)
{
	Wait(other, other.m_FenceValue.load());
}

void CommandQueue::Wait(const CommandQueue& other, const uint64_t fenceValue)
{
	if (fenceValue != 0u)
	{
		m_D3d12CommandQueue->Wait(other.m_D3d12Fence.Get(), fenceValue);
	}
}

ComPtr<ID3D12CommandQueue> CommandQueue::GetD3D12CommandQueue() const
{
	return m_D3d12CommandQueue;
}

std::shared_ptr<ResourceStateRegistry> CommandQueue::GetResourceStateRegistry() const
{
	return m_DeviceContext->GetResourceStateRegistry();
}

void CommandQueue::ProcessInFlightCommandLists()
{
	std::unique_lock lock(m_ProcessInFlightCommandListsThreadMutex, std::defer_lock);
	const char* stage = "initialization";

	try
	{
	while (m_IsProcessingInFlightCommandLists)
	{
		stage = "dequeue in-flight command list";
		CommandListEntry commandListEntry;

		if (!m_InFlightCommandLists.TryPop(commandListEntry))
		{
			lock.lock();
			m_ProcessInFlightCommandListsThreadCv.wait_for(
				lock,
				std::chrono::milliseconds(1),
				[this]
				{
					return !m_IsProcessingInFlightCommandLists || !m_InFlightCommandLists.Empty();
				});
			lock.unlock();
			continue;
		}

		const auto fenceValue = std::get<0>(commandListEntry);
		const auto commandList = std::get<1>(commandListEntry);

		stage = "wait for submitted fence";
		try
		{
			WaitForFenceValue(fenceValue);
		}
		catch (const std::exception& exception)
		{
			throw std::runtime_error("Command queue fence wait failed: " + std::string(exception.what()));
		}

		stage = "reset command list";
		try
		{
			commandList->Reset();
		}
		catch (const std::exception& exception)
		{
			throw std::runtime_error("Command list reset failed: " + std::string(exception.what()));
		}

		stage = "return command list to available queue";
		m_AvailableCommandLists.Push(commandList);
		m_ProcessInFlightCommandListsThreadCv.notify_one();
	}
	}
	catch (const std::exception& exception)
	{
		if (m_FatalErrorHandler)
		{
			m_FatalErrorHandler(CommandQueueFailure{
				.QueueType = m_CommandListType,
				.Stage = stage,
				.Message = exception.what(),
			});
		}
		else
		{
			std::terminate();
		}
	}
}
//Modify End
