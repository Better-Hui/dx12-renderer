#define WIN32_LEAN_AND_MEAN
#include <codecvt>
#include <windows.h>
#include <Shlwapi.h>
#include <shellapi.h>

#include <DX12Library/Application.h>

#include <dxgidebug.h>

#include <Framework/Core/GraphicsSettings.h>

//Modify Begin:2026-07-21 by Hui
#include <fstream>
//Modify End
//Modify Begin:2026-07-28 by Hui
#include <sstream>
#include <vector>
#include <wrl.h>
//Modify End

#ifdef DEMO_TYPE

#define STR_VALUE(arg)      L#arg
#define TYPE_STR_VALUE(name) STR_VALUE(name)

#define DEMO_NAME TYPE_STR_VALUE(DEMO_TYPE)
#endif

void ReportLiveObjects()
{
	IDXGIDebug1* dxgiDebug = nullptr;
//Modify Begin:2026-07-21 by Hui
	if (FAILED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug))) || dxgiDebug == nullptr)
	{
		return;
	}
//Modify End

	dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_IGNORE_INTERNAL);
	dxgiDebug->Release();
}

struct Parameters
{
	int m_ClientWidth = 1280;
	int m_ClientHeight = 720;
	//Modify Begin:2026-08-07 by Hui
	bool m_EnableStreamlineInterposer = false;
	//Modify End

	GraphicsSettings m_GraphicsSettings;
};

void ParseCommandLineArguments(Parameters& parameters)
{
	int argc;
	wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);

	for (size_t i = 0; i < argc; ++i)
	{
		if (wcscmp(argv[i], L"-w") == 0 || wcscmp(argv[i], L"--width") == 0)
		{
			parameters.m_ClientWidth = wcstol(argv[++i], nullptr, 10);
		}

		if (wcscmp(argv[i], L"-h") == 0 || wcscmp(argv[i], L"--height") == 0)
		{
			parameters.m_ClientHeight = wcstol(argv[++i], nullptr, 10);
		}

		if (wcscmp(argv[i], L"--shadowResolution") == 0)
		{
			parameters.m_GraphicsSettings.DirectionalLightShadows.m_Resolution = wcstol(argv[++i], nullptr, 10);
		}

		if (wcscmp(argv[i], L"--poissonSpread") == 0)
		{
			parameters.m_GraphicsSettings.DirectionalLightShadows.m_PoissonSpread = wcstof(argv[++i], nullptr);
		}

		//Modify Begin:2026-08-07 by Hui
		if (wcscmp(argv[i], L"--streamline-interposer") == 0)
		{
			parameters.m_EnableStreamlineInterposer = true;
		}
		//Modify End
	}

	LocalFree(argv);
}

//Modify Begin:2026-07-30 by Hui
std::shared_ptr<Game> CreateGame(Application& application, const Parameters& parameters)
{
#ifdef DEMO_TYPE
	return std::make_shared<DEMO_TYPE>(application, DEMO_NAME, parameters.m_ClientWidth, parameters.m_ClientHeight,
		parameters.m_GraphicsSettings);
#else
	throw std::exception("DEMO_TYPE was not defined.");
#endif
}
//Modify End

//Modify Begin:2026-07-28 by Hui
std::string NarrowDredName(const wchar_t* value)
{
	if (value == nullptr)
	{
		return "<unnamed>";
	}

	std::string result;
	while (*value != L'\0')
	{
		result.push_back(*value < 128 ? static_cast<char>(*value) : '?');
		++value;
	}
	return result;
}

//Modify Begin:2026-07-30 by Hui
void WriteDeviceRemovedDetails(Application& application, std::ostream& stream)
{
	try
	{
		auto device = application.GetDevice();
		stream << "DeviceRemovedReason=" << device->GetDeviceRemovedReason() << std::endl;

		Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;
		if (SUCCEEDED(device.As(&infoQueue)))
		{
			const UINT64 messageCount = infoQueue->GetNumStoredMessages();
			stream << "D3D12InfoQueueMessages=" << messageCount << std::endl;
			const UINT64 firstMessage = messageCount > 64 ? messageCount - 64 : 0;
			for (UINT64 messageIndex = firstMessage; messageIndex < messageCount; ++messageIndex)
			{
				SIZE_T messageLength = 0;
				if (FAILED(infoQueue->GetMessage(messageIndex, nullptr, &messageLength)) || messageLength == 0)
				{
					continue;
				}

				std::vector<char> messageStorage(messageLength);
				auto* message = reinterpret_cast<D3D12_MESSAGE*>(messageStorage.data());
				if (SUCCEEDED(infoQueue->GetMessage(messageIndex, message, &messageLength)))
				{
					stream << "D3D12[" << messageIndex << "] Severity=" << message->Severity
						<< " ID=" << message->ID
						<< " Text=" << (message->pDescription != nullptr ? message->pDescription : "") << std::endl;
				}
			}
		}

		Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedData1> dred;
		if (FAILED(device.As(&dred)))
		{
			return;
		}

		D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs = {};
		if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput1(&breadcrumbs)))
		{
			uint32_t nodeIndex = 0;
			for (const D3D12_AUTO_BREADCRUMB_NODE1* node = breadcrumbs.pHeadAutoBreadcrumbNode;
				node != nullptr && nodeIndex < 16;
				node = node->pNext, ++nodeIndex)
			{
				const UINT lastValue = node->pLastBreadcrumbValue != nullptr ? *node->pLastBreadcrumbValue : 0u;
				stream << "DRED Breadcrumb[" << nodeIndex << "] Queue="
					<< NarrowDredName(node->pCommandQueueDebugNameW)
					<< " CommandList=" << NarrowDredName(node->pCommandListDebugNameW)
					<< " Last=" << lastValue
					<< " Count=" << node->BreadcrumbCount << std::endl;
			}
		}

		D3D12_DRED_PAGE_FAULT_OUTPUT1 pageFault = {};
		if (SUCCEEDED(dred->GetPageFaultAllocationOutput1(&pageFault)))
		{
			stream << "DRED PageFaultVA=" << pageFault.PageFaultVA << std::endl;

			uint32_t existingIndex = 0;
			for (const D3D12_DRED_ALLOCATION_NODE1* allocation = pageFault.pHeadExistingAllocationNode;
				allocation != nullptr && existingIndex < 16;
				allocation = allocation->pNext, ++existingIndex)
			{
				stream << "DRED ExistingAllocation[" << existingIndex << "] Type="
					<< allocation->AllocationType
					<< " Name=" << NarrowDredName(allocation->ObjectNameW) << std::endl;
			}

			uint32_t freedIndex = 0;
			for (const D3D12_DRED_ALLOCATION_NODE1* allocation = pageFault.pHeadRecentFreedAllocationNode;
				allocation != nullptr && freedIndex < 16;
				allocation = allocation->pNext, ++freedIndex)
			{
				stream << "DRED RecentFreedAllocation[" << freedIndex << "] Type="
					<< allocation->AllocationType
					<< " Name=" << NarrowDredName(allocation->ObjectNameW) << std::endl;
			}
		}
	}
	catch (...)
	{
		stream << "DRED query failed." << std::endl;
	}
}
//Modify End

int CALLBACK wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow)
{
	int retCode = 0;

	// Set the working directory to the path of the executable.
	WCHAR path[MAX_PATH];
	const HMODULE hModule = GetModuleHandleW(nullptr);
	if (GetModuleFileNameW(hModule, path, MAX_PATH) > 0)
	{
		PathRemoveFileSpecW(path);
		SetCurrentDirectoryW(path);
	}

	Parameters parameters;
	ParseCommandLineArguments(parameters);

//Modify Begin:2026-07-21 by Hui
	const char* applicationStage = "Create";
	const HRESULT coInitializeResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	const bool shouldUninitializeCom = SUCCEEDED(coInitializeResult);
	Application* application = nullptr;
	try
	{
		ApplicationCreateDesc applicationCreateDesc;
		applicationCreateDesc.EnableStreamlineInterposer = parameters.m_EnableStreamlineInterposer;
		Application::Create(hInstance, applicationCreateDesc);
		application = &Application::Get();
		{
			applicationStage = "CreateGame";
			const auto demo = CreateGame(*application, parameters);
			applicationStage = "Run";
			retCode = application->Run(demo);
		}
		applicationStage = "Destroy";
		Application::Destroy();
		application = nullptr;
	}
	catch (const std::exception& exception)
	{
		std::ofstream errorLog("DemoException.log", std::ios::out | std::ios::trunc);
		errorLog << "Stage=" << applicationStage << std::endl;
		errorLog << exception.what() << std::endl;
		if (application != nullptr)
		{
			WriteDeviceRemovedDetails(*application, errorLog);
		}
		retCode = 3;
	}
	if (shouldUninitializeCom)
	{
		CoUninitialize();
	}
//Modify End

	atexit(&ReportLiveObjects);

	return retCode;
}
