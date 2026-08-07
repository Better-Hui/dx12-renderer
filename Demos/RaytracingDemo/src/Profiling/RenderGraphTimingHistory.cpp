//Modify Begin:2026-08-07 by BestHui
#include <Profiling/RenderGraphTimingHistory.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace
{
    std::string EscapeCsvField(const std::string& value)
    {
        std::string escaped = "\"";
        for (const char character : value)
        {
            if (character == '\"')
            {
                escaped += "\"\"";
            }
            else
            {
                escaped += character;
            }
        }
        escaped += "\"";
        return escaped;
    }
}

void DemoProfiling::RenderGraphTimingHistory::Clear()
{
    m_Frames.clear();
    m_Status = "RG timing history cleared.";
}

void DemoProfiling::RenderGraphTimingHistory::SetCapacity(const size_t capacity)
{
    m_Capacity = std::max<size_t>(1u, capacity);
    TrimToCapacity();
}

void DemoProfiling::RenderGraphTimingHistory::Record(
    const uint64_t frameNumber,
    const char* queueName,
    const std::vector<GpuTimestampSample>& samples)
{
    if (samples.empty())
    {
        return;
    }

    auto frameIt = std::find_if(
        m_Frames.begin(),
        m_Frames.end(),
        [frameNumber](const FrameSamples& frame) { return frame.Number == frameNumber; });
    if (frameIt == m_Frames.end())
    {
        const auto insertPosition = std::find_if(
            m_Frames.begin(),
            m_Frames.end(),
            [frameNumber](const FrameSamples& frame) { return frame.Number > frameNumber; });
        frameIt = m_Frames.insert(insertPosition, { frameNumber, {} });
    }

    const std::string queueNameString = queueName != nullptr ? queueName : "Unknown";
    auto queueIt = std::find_if(
        frameIt->Queues.begin(),
        frameIt->Queues.end(),
        [&queueNameString](const QueueSamples& queue) { return queue.Name == queueNameString; });
    if (queueIt == frameIt->Queues.end())
    {
        frameIt->Queues.push_back({ queueNameString, samples });
    }
    else
    {
        queueIt->Samples = samples;
    }

    TrimToCapacity();
}

bool DemoProfiling::RenderGraphTimingHistory::DumpCsv()
{
    if (m_Frames.empty())
    {
        m_Status = "RG timing history is empty.";
        return false;
    }

    try
    {
        const std::filesystem::path outputDirectory = std::filesystem::current_path() / "Profiling";
        std::filesystem::create_directories(outputDirectory);

        const auto currentTime = std::chrono::system_clock::now();
        const std::time_t currentTimeValue = std::chrono::system_clock::to_time_t(currentTime);
        std::tm localTime{};
        localtime_s(&localTime, &currentTimeValue);
        std::ostringstream filename;
        filename << "RenderGraphTiming_" << std::put_time(&localTime, "%Y%m%d_%H%M%S") << ".csv";
        const std::filesystem::path outputPath = outputDirectory / filename.str();

        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output.is_open())
        {
            throw std::runtime_error("Failed to open output file.");
        }

        output << "frame,queue,marker,gpu_delta_ms,gpu_total_ms,cpu_delta_ms,cpu_total_ms\n";
        output << std::fixed << std::setprecision(6);
        for (const FrameSamples& frame : m_Frames)
        {
            for (const QueueSamples& queue : frame.Queues)
            {
                for (const GpuTimestampSample& sample : queue.Samples)
                {
                    output << frame.Number << ','
                           << EscapeCsvField(queue.Name) << ','
                           << EscapeCsvField(sample.Name) << ','
                           << sample.MillisecondsFromPrevious << ','
                           << sample.MillisecondsFromFrameStart << ','
                           << sample.CpuMillisecondsFromPrevious << ','
                           << sample.CpuMillisecondsFromFrameStart << '\n';
                }
            }
        }

        m_Status = "Exported " + std::to_string(m_Frames.size()) +
            " frames to " + outputPath.string();
        return true;
    }
    catch (const std::exception& exception)
    {
        m_Status = std::string("RG timing export failed: ") + exception.what();
        return false;
    }
}

void DemoProfiling::RenderGraphTimingHistory::TrimToCapacity()
{
    while (m_Frames.size() > m_Capacity)
    {
        m_Frames.pop_front();
    }
}
//Modify End
