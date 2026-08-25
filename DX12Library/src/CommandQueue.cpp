#include "DX12LibPCH.h"

#include "CommandQueue.h"

#include "CommandList.h"
//Modify Begin:2026-08-07 by Hui
#include "D3D12DeviceContext.h"
//Modify End

//Modify Begin:2026-08-21 by Hui
#include <fstream>
#include <chrono>
#include <sstream>
#include <unordered_set>

namespace
{
	const char* GetQueueTypeName(const D3D12_COMMAND_LIST_TYPE type)
	{
		switch (type)
		{
		case D3D12_COMMAND_LIST_TYPE_DIRECT: return "Direct";
		case D3D12_COMMAND_LIST_TYPE_COMPUTE: return "Compute";
		case D3D12_COMMAND_LIST_TYPE_COPY: return "Copy";
		default: return "Unknown";
		}
	}

	const char* GetCommandListLifecycleName(const CommandListLifecycle lifecycle)
	{
		switch (lifecycle)
		{
		case CommandListLifecycle::Available: return "Available";
		case CommandListLifecycle::Recording: return "Recording";
		case CommandListLifecycle::Submitted: return "Submitted";
		case CommandListLifecycle::InFlight: return "InFlight";
		case CommandListLifecycle::Retiring: return "Retiring";
		default: return "Unknown";
		}
	}
}
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

void CommandQueue::SetDiagnosticTelemetrySink(DiagnosticTelemetrySink* sink) noexcept
{
	m_DiagnosticTelemetrySink.store(sink, std::memory_order_release);
}

void CommandQueue::EmitTelemetry(DiagnosticTelemetryEvent event) const noexcept
{
	if (DiagnosticTelemetrySink* sink = m_DiagnosticTelemetrySink.load(std::memory_order_acquire))
	{
		sink->RecordTelemetry(std::move(event));
	}
}

bool CommandQueue::HasDiagnosticTelemetrySink() const noexcept
{
	return m_DiagnosticTelemetrySink.load(std::memory_order_acquire) != nullptr;
}

bool CommandQueue::HasFailure() const noexcept
{
	std::scoped_lock lock(m_FailureMutex);
	return !m_FailureMessage.empty();
}

void CommandQueue::ThrowIfQueueFailed() const
{
	std::string message;
	{
		std::scoped_lock lock(m_FailureMutex);
		message = m_FailureMessage;
	}
	if (!message.empty())
	{
		throw std::runtime_error("Command queue is unavailable: " + message);
	}
}

void CommandQueue::SetFailure(std::string message) noexcept
{
	try
	{
		std::scoped_lock lock(m_FailureMutex);
		if (m_FailureMessage.empty())
		{
			m_FailureMessage = std::move(message);
		}
	}
	catch (...)
	{
	}
}

CommandQueue::~CommandQueue()
{
	m_IsProcessingInFlightCommandLists = false;
	m_ProcessInFlightCommandListsThread.join();
}

uint64_t CommandQueue::Signal()
{
	uint64_t fenceValue = ++m_FenceValue;
	ThrowIfFailed(m_D3d12CommandQueue->Signal(m_D3d12Fence.Get(), fenceValue));
	if (HasDiagnosticTelemetrySink())
	{
		EmitTelemetry({
		.Category = "command_queue.signal",
		.Name = "signal",
		.CorrelationId = MakeDiagnosticQueueFenceCorrelationId(GetQueueTypeName(m_CommandListType), fenceValue),
		.Fields = {
			{ "queue", std::string(GetQueueTypeName(m_CommandListType)) },
			{ "fence", fenceValue },
		},
		});
	}
	return fenceValue;
}

bool CommandQueue::IsFenceComplete(uint64_t fenceValue)
{
	return m_D3d12Fence->GetCompletedValue() >= fenceValue;
}

void CommandQueue::WaitForFenceValue(uint64_t fenceValue)
{
	ThrowIfQueueFailed();
	if (!IsFenceComplete(fenceValue))
	{
		const bool captureTelemetry = HasDiagnosticTelemetrySink();
		const auto waitStart = captureTelemetry ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
		auto event = ::CreateEvent(nullptr, FALSE, FALSE, nullptr);
		assert(event && "Failed to create fence event handle.");

		ThrowIfFailed(m_D3d12Fence->SetEventOnCompletion(fenceValue, event));
		for (;;)
		{
			if (!m_IsProcessingInFlightCommandLists)
			{
				CloseHandle(event);
				ThrowIfQueueFailed();
				throw std::runtime_error("Fence wait was cancelled while the command queue was stopping.");
			}
			const DWORD waitResult = WaitForSingleObject(event, 100u);
			if (waitResult == WAIT_OBJECT_0)
			{
				break;
			}
			if (waitResult == WAIT_FAILED)
			{
				const DWORD waitError = GetLastError();
				CloseHandle(event);
				throw std::runtime_error("Fence wait failed with Win32 error " + std::to_string(waitError) + ".");
			}

			const HRESULT removedReason = m_DeviceContext->GetDevice()->GetDeviceRemovedReason();
			if (FAILED(removedReason))
			{
				const uint64_t completedFence = m_D3d12Fence->GetCompletedValue();
				if (captureTelemetry)
				{
					EmitTelemetry({
					.Category = "command_queue.device_removed",
					.Name = "fence_wait",
					.Severity = DiagnosticTelemetrySeverity::Error,
					.CorrelationId = MakeDiagnosticQueueFenceCorrelationId(GetQueueTypeName(m_CommandListType), fenceValue),
					.Fields = {
						{ "queue", std::string(GetQueueTypeName(m_CommandListType)) },
						{ "fence", fenceValue },
						{ "completed_fence", completedFence },
						{ "device_removed_reason", static_cast<int64_t>(removedReason) },
					},
					});
				}
				CloseHandle(event);
				std::ostringstream message;
				message << "Fence " << fenceValue << " on " << GetQueueTypeName(m_CommandListType)
					<< " queue did not complete because the D3D12 device was removed (0x"
					<< std::hex << static_cast<uint32_t>(removedReason) << ").";
				throw std::runtime_error(message.str());
			}
		}

		CloseHandle(event);
		if (captureTelemetry)
		{
			const double durationMilliseconds = std::chrono::duration<double, std::milli>(
				std::chrono::steady_clock::now() - waitStart).count();
			EmitTelemetry({
			.Category = "profiler.cpu",
			.Name = "queue_fence_wait",
			.CorrelationId = MakeDiagnosticQueueFenceCorrelationId(GetQueueTypeName(m_CommandListType), fenceValue),
			.Fields = {
				{ "queue", std::string(GetQueueTypeName(m_CommandListType)) },
				{ "fence", fenceValue },
				{ "cpu_duration_ms", durationMilliseconds },
			},
			});
		}
	}
}

void CommandQueue::Flush()
{
	std::unique_lock<std::mutex> lock(m_ProcessInFlightCommandListsThreadMutex);
	m_ProcessInFlightCommandListsThreadCv.wait(lock, [this]
	{
		return m_InFlightCommandLists.Empty() || HasFailure();
	});
	lock.unlock();
	ThrowIfQueueFailed();

	// In case the command queue was signaled directly 
	// using the CommandQueue::Signal method then the 
	// fence value of the command queue might be higher than the fence
	// value of any of the executed command lists.
	WaitForFenceValue(m_FenceValue);
}

bool CommandQueue::FlushWithTimeout(const uint32_t timeoutMilliseconds)
{
	ThrowIfQueueFailed();
	std::unique_lock<std::mutex> lock(m_ProcessInFlightCommandListsThreadMutex);
	const bool retired = m_ProcessInFlightCommandListsThreadCv.wait_for(
		lock,
		std::chrono::milliseconds(timeoutMilliseconds),
		[this]
		{
			return m_InFlightCommandLists.Empty() || HasFailure();
		});
	lock.unlock();
	if (retired)
	{
		ThrowIfQueueFailed();
		WaitForFenceValue(m_FenceValue);
		return true;
	}

	const uint64_t requestedFence = m_FenceValue.load();
	const uint64_t completedFence = m_D3d12Fence->GetCompletedValue();
	const HRESULT removedReason = m_DeviceContext->GetDevice()->GetDeviceRemovedReason();
	std::ostringstream message;
	message << "Timed out after " << timeoutMilliseconds << " ms while retiring "
		<< GetQueueTypeName(m_CommandListType) << " queue work for fence " << requestedFence
		<< " (completed fence " << completedFence << ", device removed reason 0x"
		<< std::hex << static_cast<uint32_t>(removedReason) << ").";
	SetFailure(message.str());
	m_IsProcessingInFlightCommandLists = false;
	m_ProcessInFlightCommandListsThreadCv.notify_all();
	EmitTelemetry({
		.Category = "command_queue.timeout",
		.Name = "flush",
		.Severity = DiagnosticTelemetrySeverity::Error,
		.CorrelationId = MakeDiagnosticQueueFenceCorrelationId(GetQueueTypeName(m_CommandListType), requestedFence),
		.Fields = {
			{ "queue", std::string(GetQueueTypeName(m_CommandListType)) },
			{ "fence", requestedFence },
			{ "completed_fence", completedFence },
			{ "timeout_ms", static_cast<uint64_t>(timeoutMilliseconds) },
			{ "device_removed_reason", static_cast<int64_t>(removedReason) },
		},
	});
	return false;
}

std::shared_ptr<CommandList> CommandQueue::GetCommandList()
{
	ThrowIfQueueFailed();
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

	if (!commandList->TransitionLifecycle(CommandListLifecycle::Available, CommandListLifecycle::Recording))
	{
		const CommandListLifecycle lifecycle = commandList->GetLifecycle();
		EmitTelemetry({
			.Category = "assertion",
			.Name = "command_list_lease",
			.Severity = DiagnosticTelemetrySeverity::Error,
			.Fields = {
				{ "result", std::string("fail") },
				{ "queue", std::string(GetQueueTypeName(m_CommandListType)) },
				{ "command_list_id", commandList->GetStableId() },
				{ "lifecycle", std::string(GetCommandListLifecycleName(lifecycle)) },
			},
		});
		throw std::logic_error(
			"Command list #" + std::to_string(commandList->GetStableId()) +
			" was leased while it was " + GetCommandListLifecycleName(lifecycle) + ".");
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
	ThrowIfQueueFailed();
	if (commandLists.empty())
	{
		throw std::invalid_argument("Cannot submit an empty command-list collection.");
	}
	std::unordered_set<const CommandList*> uniqueCommandLists;
	uniqueCommandLists.reserve(commandLists.size());
	for (const std::shared_ptr<CommandList>& commandList : commandLists)
	{
		if (commandList == nullptr || commandList->GetCommandListType() != m_CommandListType)
		{
			EmitTelemetry({
				.Category = "assertion",
				.Name = "command_list_queue_compatibility",
				.Severity = DiagnosticTelemetrySeverity::Error,
				.Fields = {
					{ "result", std::string("fail") },
					{ "message", std::string("Submitted command-list type does not match the native queue.") },
					{ "queue", std::string(GetQueueTypeName(m_CommandListType)) },
					{ "command_list_type", commandList != nullptr
						? std::string(GetQueueTypeName(commandList->GetCommandListType()))
						: std::string("null") },
				},
			});
			throw std::invalid_argument("Submitted command-list type does not match the command queue.");
		}
		if (!uniqueCommandLists.insert(commandList.get()).second)
		{
			EmitTelemetry({
				.Category = "assertion",
				.Name = "command_list_submission_unique",
				.Severity = DiagnosticTelemetrySeverity::Error,
				.Fields = {
					{ "result", std::string("fail") },
					{ "queue", std::string(GetQueueTypeName(m_CommandListType)) },
					{ "command_list_id", commandList->GetStableId() },
				},
			});
			throw std::invalid_argument("The same command list cannot be submitted more than once in one submission.");
		}
	}

	auto submissionScope = m_DeviceContext->GetResourceStateRegistry()->AcquireSubmissionScope();

	// Command lists that need to put back on the command list queue.
	std::vector<std::shared_ptr<CommandList>> toBeQueued;
	toBeQueued.reserve(commandLists.size() * 2); // 2x since each command list will have a pending command list.

	// Command lists that need to be executed.
	std::vector<ID3D12CommandList*> d3d12CommandLists;
	d3d12CommandLists.reserve(commandLists.size() * 2); // 2x since each command list will have a pending command list.

	uint64_t pendingBarrierCommandListCount = 0;
	for (auto commandList : commandLists)
	{
		auto pendingCommandList = GetCommandList();
		bool hasPendingBarriers = commandList->Close(*pendingCommandList, submissionScope);
		pendingCommandList->Close();
		if (!commandList->TransitionLifecycle(CommandListLifecycle::Recording, CommandListLifecycle::Submitted) ||
			!pendingCommandList->TransitionLifecycle(CommandListLifecycle::Recording, CommandListLifecycle::Submitted))
		{
			throw std::logic_error("Command-list lifecycle changed while closing a queue submission.");
		}
		// If there are no pending barriers on the pending command list, there is no reason to 
		// execute an empty command list on the command queue.
		if (hasPendingBarriers)
		{
			++pendingBarrierCommandListCount;
			d3d12CommandLists.push_back(pendingCommandList->GetGraphicsCommandList().Get());
		}
		d3d12CommandLists.push_back(commandList->GetGraphicsCommandList().Get());

		toBeQueued.push_back(pendingCommandList);
		toBeQueued.push_back(commandList);
	}

	UINT numCommandLists = static_cast<UINT>(d3d12CommandLists.size());
	m_D3d12CommandQueue->ExecuteCommandLists(numCommandLists, d3d12CommandLists.data());
	uint64_t fenceValue = Signal();
	for (const std::shared_ptr<CommandList>& commandList : toBeQueued)
	{
		if (!commandList->TransitionLifecycle(CommandListLifecycle::Submitted, CommandListLifecycle::InFlight))
		{
			throw std::logic_error("Command-list lifecycle changed before its submission fence was recorded.");
		}
	}
	if (HasDiagnosticTelemetrySink())
	{
		EmitTelemetry({
		.Category = "command_queue.submission",
		.Name = "execute_command_lists",
		.CorrelationId = MakeDiagnosticQueueFenceCorrelationId(GetQueueTypeName(m_CommandListType), fenceValue),
		.Fields = {
			{ "queue", std::string(GetQueueTypeName(m_CommandListType)) },
			{ "fence", fenceValue },
			{ "logical_command_list_count", static_cast<uint64_t>(commandLists.size()) },
			{ "native_command_list_count", static_cast<uint64_t>(numCommandLists) },
			{ "pending_barrier_command_list_count", pendingBarrierCommandListCount },
		},
		});
		for (const std::shared_ptr<CommandList>& commandList : toBeQueued)
		{
			EmitTelemetry({
				.Category = "command_queue.command_list",
				.Name = "submitted",
				.CorrelationId = MakeDiagnosticQueueFenceCorrelationId(GetQueueTypeName(m_CommandListType), fenceValue),
				.Fields = {
					{ "queue", std::string(GetQueueTypeName(m_CommandListType)) },
					{ "command_list_id", commandList->GetStableId() },
					{ "fence", fenceValue },
				},
			});
		}
	}

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
		ThrowIfFailed(m_D3d12CommandQueue->Wait(other.m_D3d12Fence.Get(), fenceValue));
		if (HasDiagnosticTelemetrySink())
		{
			EmitTelemetry({
			.Category = "command_queue.wait",
			.Name = "queue_wait",
			.CorrelationId = MakeDiagnosticQueueFenceCorrelationId(GetQueueTypeName(other.m_CommandListType), fenceValue),
			.Fields = {
				{ "consumer_queue", std::string(GetQueueTypeName(m_CommandListType)) },
				{ "producer_queue", std::string(GetQueueTypeName(other.m_CommandListType)) },
				{ "producer_fence", fenceValue },
			},
			});
		}
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
		if (!commandList->TransitionLifecycle(CommandListLifecycle::InFlight, CommandListLifecycle::Retiring))
		{
			throw std::logic_error(
				"Command list #" + std::to_string(commandList->GetStableId()) +
				" entered retirement while it was " + GetCommandListLifecycleName(commandList->GetLifecycle()) + ".");
		}

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
		if (!commandList->TransitionLifecycle(CommandListLifecycle::Retiring, CommandListLifecycle::Available))
		{
			throw std::logic_error("Command-list lifecycle changed while returning a retired command list to the queue.");
		}
		m_AvailableCommandLists.Push(commandList);
		m_ProcessInFlightCommandListsThreadCv.notify_one();
	}
	}
	catch (const std::exception& exception)
	{
		SetFailure(std::string(stage) + ": " + exception.what());
		m_IsProcessingInFlightCommandLists = false;
		m_ProcessInFlightCommandListsThreadCv.notify_all();
		EmitTelemetry({
			.Category = "command_queue.failure",
			.Name = "worker_failure",
			.Severity = DiagnosticTelemetrySeverity::Fatal,
			.Fields = {
				{ "queue", std::string(GetQueueTypeName(m_CommandListType)) },
				{ "stage", std::string(stage) },
				{ "message", std::string(exception.what()) },
			},
		});
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
