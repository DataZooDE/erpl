#include "erpl_tracing.hpp"

#include <iostream>

namespace erpl {

ErplTracer &ErplTracer::Instance()
{
    static ErplTracer instance;
    return instance;
}

ErplTracer::ErplTracer()
    : trace_directory("trace"), output_mode("console")
{
}

ErplTracer::~ErplTracer()
{
    std::lock_guard<std::mutex> lock(trace_mutex);
    if (trace_file && trace_file->is_open()) {
        trace_file->close();
    }
}

void ErplTracer::SetEnabled(bool enable_flag)
{
    {
        std::lock_guard<std::mutex> lock(trace_mutex);
        if (enabled == enable_flag) {
            return;
        }

        enabled = enable_flag;

        if (enabled) {
            EnsureTraceFile();
        } else {
            if (trace_file && trace_file->is_open()) {
                trace_file->close();
            }
            trace_file.reset();
        }
    }

    // Call Info outside the lock to avoid deadlock
    if (enable_flag) {
        Info("TRACER", "Tracing enabled");
    } else {
        Info("TRACER", "Tracing disabled");
    }
}

void ErplTracer::SetLevel(TraceLevel trace_level)
{
    std::lock_guard<std::mutex> lock(trace_mutex);
    level = trace_level;
    Info("TRACER", "Trace level set to: " + LevelToString(trace_level));
}

void ErplTracer::SetTraceDirectory(const std::string &directory)
{
    std::lock_guard<std::mutex> lock(trace_mutex);
    trace_directory = directory;
    std::filesystem::path path(directory);
    if (!std::filesystem::exists(path)) {
        std::filesystem::create_directories(path);
    }
    Info("TRACER", "Trace directory set to: " + directory);
    if (enabled) {
        EnsureTraceFile();
    }
}

void ErplTracer::SetOutputMode(const std::string &mode)
{
    std::lock_guard<std::mutex> lock(trace_mutex);
    output_mode = mode;
    Info("TRACER", "Trace output mode set to: " + mode);
}

void ErplTracer::SetMaxFileSize(int64_t max_size)
{
    std::lock_guard<std::mutex> lock(trace_mutex);
    max_file_size = max_size;
    Info("TRACER", "Trace max file size set to: " + std::to_string(max_size));
}

void ErplTracer::SetRotation(bool rotation)
{
    std::lock_guard<std::mutex> lock(trace_mutex);
    rotation_enabled = rotation;
    Info("TRACER", "Trace rotation " + std::string(rotation ? "enabled" : "disabled"));
}

void ErplTracer::Trace(TraceLevel msg_level, const std::string &component, const std::string &message)
{
    Trace(msg_level, component, message, std::string());
}

void ErplTracer::Trace(TraceLevel msg_level, const std::string &component, const std::string &message, const std::string &data)
{
    if (!ShouldEmit(msg_level)) {
        return;
    }

    std::string log_message;
    log_message.reserve(128 + component.size() + message.size() + data.size());

    log_message += GetTimestamp();
    log_message += " [";
    log_message += LevelToString(msg_level);
    log_message += "] [";
    log_message += component;
    log_message += "] ";
    log_message += message;

    if (!data.empty()) {
        log_message += "\nData: ";
        log_message += data;
    }

    std::cout << log_message << std::endl;
    WriteToFile(log_message);
}

void ErplTracer::Error(const std::string &component, const std::string &message)
{
    Trace(TraceLevel::ERROR, component, message);
}

void ErplTracer::Error(const std::string &component, const std::string &message, const std::string &data)
{
    Trace(TraceLevel::ERROR, component, message, data);
}

void ErplTracer::Warn(const std::string &component, const std::string &message)
{
    Trace(TraceLevel::WARN, component, message);
}

void ErplTracer::Warn(const std::string &component, const std::string &message, const std::string &data)
{
    Trace(TraceLevel::WARN, component, message, data);
}

void ErplTracer::Info(const std::string &component, const std::string &message)
{
    Trace(TraceLevel::INFO, component, message);
}

void ErplTracer::Info(const std::string &component, const std::string &message, const std::string &data)
{
    Trace(TraceLevel::INFO, component, message, data);
}

void ErplTracer::Debug(const std::string &component, const std::string &message)
{
    Trace(TraceLevel::DEBUG_LEVEL, component, message);
}

void ErplTracer::Debug(const std::string &component, const std::string &message, const std::string &data)
{
    Trace(TraceLevel::DEBUG_LEVEL, component, message, data);
}

void ErplTracer::TraceMessage(const std::string &component, const std::string &message)
{
    Trace(TraceLevel::TRACE, component, message);
}

void ErplTracer::TraceMessage(const std::string &component, const std::string &message, const std::string &data)
{
    Trace(TraceLevel::TRACE, component, message, data);
}

bool ErplTracer::Enabled() const
{
    std::lock_guard<std::mutex> lock(trace_mutex);
    return enabled;
}

TraceLevel ErplTracer::CurrentLevel() const
{
    std::lock_guard<std::mutex> lock(trace_mutex);
    return level;
}

void ErplTracer::WriteToFile(const std::string &message)
{
    std::lock_guard<std::mutex> lock(trace_mutex);
    if (!trace_file) {
        return;
    }
    if (!trace_file->is_open()) {
        return;
    }

    (*trace_file) << message << std::endl;
    trace_file->flush();
}

std::string ErplTracer::GetTimestamp()
{
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&time_t));

    std::string timestamp(buffer);
    timestamp += ".";
    timestamp += std::to_string(ms.count());
    return timestamp;
}

std::string ErplTracer::LevelToString(TraceLevel trace_level) const
{
    switch (trace_level) {
        case TraceLevel::NONE:
            return "NONE";
        case TraceLevel::ERROR:
            return "ERROR";
        case TraceLevel::WARN:
            return "WARN";
        case TraceLevel::INFO:
            return "INFO";
        case TraceLevel::DEBUG_LEVEL:
            return "DEBUG";
        case TraceLevel::TRACE:
            return "TRACE";
        default:
            return "UNKNOWN";
    }
}

bool ErplTracer::ShouldEmit(TraceLevel msg_level) const
{
    if (!enabled) {
        return false;
    }
    return static_cast<int>(msg_level) <= static_cast<int>(level);
}

void ErplTracer::EnsureTraceFile()
{
    if (trace_file && trace_file->is_open()) {
        return;
    }

    std::filesystem::path directory(trace_directory);
    if (!std::filesystem::exists(directory)) {
        std::filesystem::create_directories(directory);
    }

    std::filesystem::path file_path = directory / "erpl_trace.log";
    trace_file = std::make_unique<std::ofstream>(file_path, std::ios::app);
    if (!trace_file->is_open()) {
        std::cerr << "Failed to open trace file: " << file_path << std::endl;
        trace_file.reset();
    }
}

} // namespace erpl

void erpl_trace_error(const std::string &component, const std::string &message)
{
    erpl::ErplTracer::Instance().Error(component, message);
}

void erpl_trace_error_data(const std::string &component, const std::string &message, const std::string &data)
{
    erpl::ErplTracer::Instance().Error(component, message, data);
}

void erpl_trace_warn(const std::string &component, const std::string &message)
{
    erpl::ErplTracer::Instance().Warn(component, message);
}

void erpl_trace_warn_data(const std::string &component, const std::string &message, const std::string &data)
{
    erpl::ErplTracer::Instance().Warn(component, message, data);
}

void erpl_trace_info(const std::string &component, const std::string &message)
{
    erpl::ErplTracer::Instance().Info(component, message);
}

void erpl_trace_info_data(const std::string &component, const std::string &message, const std::string &data)
{
    erpl::ErplTracer::Instance().Info(component, message, data);
}

void erpl_trace_debug(const std::string &component, const std::string &message)
{
    erpl::ErplTracer::Instance().Debug(component, message);
}

void erpl_trace_debug_data(const std::string &component, const std::string &message, const std::string &data)
{
    erpl::ErplTracer::Instance().Debug(component, message, data);
}

void erpl_trace_trace(const std::string &component, const std::string &message)
{
    erpl::ErplTracer::Instance().TraceMessage(component, message);
}

void erpl_trace_trace_data(const std::string &component, const std::string &message, const std::string &data)
{
    erpl::ErplTracer::Instance().TraceMessage(component, message, data);
}


