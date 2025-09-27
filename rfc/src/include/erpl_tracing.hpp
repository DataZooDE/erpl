#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>

namespace erpl {

enum class TraceLevel {
    NONE = 0,
    ERROR,
    WARN,
    INFO,
    DEBUG_LEVEL,
    TRACE
};

class ErplTracer {
public:
    static ErplTracer &Instance();

public:
    void SetEnabled(bool enabled);
    void SetLevel(TraceLevel level);
    void SetTraceDirectory(const std::string &directory);
    void SetOutputMode(const std::string &output_mode);
    void SetMaxFileSize(int64_t max_size);
    void SetRotation(bool rotation);

    void Trace(TraceLevel msg_level, const std::string &component, const std::string &message);
    void Trace(TraceLevel msg_level, const std::string &component, const std::string &message, const std::string &data);

    void Error(const std::string &component, const std::string &message);
    void Error(const std::string &component, const std::string &message, const std::string &data);
    void Warn(const std::string &component, const std::string &message);
    void Warn(const std::string &component, const std::string &message, const std::string &data);
    void Info(const std::string &component, const std::string &message);
    void Info(const std::string &component, const std::string &message, const std::string &data);
    void Debug(const std::string &component, const std::string &message);
    void Debug(const std::string &component, const std::string &message, const std::string &data);
    void TraceMessage(const std::string &component, const std::string &message);
    void TraceMessage(const std::string &component, const std::string &message, const std::string &data);

    bool Enabled() const;
    TraceLevel CurrentLevel() const;

private:
    ErplTracer();
    ~ErplTracer();

public:
    ErplTracer(const ErplTracer &) = delete;
    ErplTracer &operator=(const ErplTracer &) = delete;

private:
    void WriteToFile(const std::string &message);
    std::string GetTimestamp();
    std::string LevelToString(TraceLevel level) const;
    bool ShouldEmit(TraceLevel level) const;
    void EnsureTraceFile();

private:
    mutable std::mutex trace_mutex;
    bool enabled = false;
    TraceLevel level = TraceLevel::INFO;

private:
    std::string trace_directory;
    std::string output_mode;
    int64_t max_file_size = 0;
    bool rotation_enabled = false;
    std::unique_ptr<std::ofstream> trace_file;
};

} // namespace erpl

void erpl_trace_error(const std::string &component, const std::string &message);
void erpl_trace_error_data(const std::string &component, const std::string &message, const std::string &data);
void erpl_trace_warn(const std::string &component, const std::string &message);
void erpl_trace_warn_data(const std::string &component, const std::string &message, const std::string &data);
void erpl_trace_info(const std::string &component, const std::string &message);
void erpl_trace_info_data(const std::string &component, const std::string &message, const std::string &data);
void erpl_trace_debug(const std::string &component, const std::string &message);
void erpl_trace_debug_data(const std::string &component, const std::string &message, const std::string &data);
void erpl_trace_trace(const std::string &component, const std::string &message);
void erpl_trace_trace_data(const std::string &component, const std::string &message, const std::string &data);

#define ERPL_TRACE_ERROR(component, message) erpl_trace_error((component), (message))
#define ERPL_TRACE_ERROR_DATA(component, message, data) erpl_trace_error_data((component), (message), (data))
#define ERPL_TRACE_WARN(component, message) erpl_trace_warn((component), (message))
#define ERPL_TRACE_WARN_DATA(component, message, data) erpl_trace_warn_data((component), (message), (data))
#define ERPL_TRACE_INFO(component, message) erpl_trace_info((component), (message))
#define ERPL_TRACE_INFO_DATA(component, message, data) erpl_trace_info_data((component), (message), (data))
#define ERPL_TRACE_DEBUG(component, message) erpl_trace_debug((component), (message))
#define ERPL_TRACE_DEBUG_DATA(component, message, data) erpl_trace_debug_data((component), (message), (data))
#define ERPL_TRACE_TRACE(component, message) erpl_trace_trace((component), (message))
#define ERPL_TRACE_TRACE_DATA(component, message, data) erpl_trace_trace_data((component), (message), (data))


