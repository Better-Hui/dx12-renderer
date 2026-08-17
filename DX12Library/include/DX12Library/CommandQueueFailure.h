#pragma once

#include <d3d12.h>

#include <functional>
#include <string>

//Modify Begin:2026-07-30 by Hui
struct CommandQueueFailure
{
    int ExitCode = 5;
    D3D12_COMMAND_LIST_TYPE QueueType = D3D12_COMMAND_LIST_TYPE_DIRECT;
    std::string Stage;
    std::string Message;
};

using CommandQueueFailureHandler = std::function<void(const CommandQueueFailure&)>;
//Modify End
