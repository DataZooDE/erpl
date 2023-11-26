#include "telemetry.hpp"

#ifdef __linux__

#include <dirent.h>

#elif _WIN32

#ifndef _MSC_VER
#define _MSC_VER 1936
#endif

#define NTDDI_VERSION NTDDI_WIN10

#include <winsock2.h>
#include <iphlpapi.h>

#endif

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.hpp"

namespace duckdb {

std::string PostHogEvent::GetPropertiesJson()
{
    std::string json = "{";
    bool first = true;
    for (auto &kv : properties)
    {
        if (!first) {
            json += ",";
        }
        json += StringUtil::Format("\"%s\": \"%s\"", kv.first, kv.second);
        first = false;
    }
    json += "}";
    return json;
}

std::string PostHogEvent::GetNowISO8601()
{
    std::time_t t = std::time(nullptr);
    std::tm tm = *std::localtime(&t);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%FT%TZ", &tm);
    return std::string(buffer);
}

// PostHogEvent -----------------------------------------------------------------
PostHogWorker::PostHogWorker(std::string api_key) : api_key(api_key)
{ 
    
}

void PostHogWorker::Process(PostHogEvent &event) 
{
    std::string payload = StringUtil::Format(R"(
        {
            "api_key": "%s",
            "batch": [{
                "event": "%s",
                "distinct_id": "%s",
                "properties": %s,
                "timestamp": "%s"
            }]
        }   
    )", api_key, event.event_name, event.distinct_id, 
        event.GetPropertiesJson(), event.GetNowISO8601());

    auto cli = make_uniq<duckdb_httplib_openssl::Client>("https://eu.posthog.com");
    auto url = "/batch/";
    auto res = cli->Post(url, payload, "application/json");
    if (res && res->status != 200) {
        throw new std::runtime_error(StringUtil::Format("Sending posthog event failed: %s", res->body));
    }
}

// PostHogWorker ----------------------------------------------------------------

PostHogTelemetry::PostHogTelemetry() : telemetry_enabled(true), api_key("phc_t3wwRLtpyEmLHYaZCSszG0MqVr74J6wnCrj9D41zk2t")
{ 
    queue = make_uniq<TelemetryWorkQueue<PostHogWorker, PostHogEvent>>();
    std::function<std::shared_ptr<PostHogWorker>()> worker_factory = [this]() { return make_shared<PostHogWorker>(api_key); };
    queue->InitializeWorkers(worker_factory);
};


PostHogTelemetry& PostHogTelemetry::Instance() 
{
    static PostHogTelemetry instance;
    return instance;
}

void PostHogTelemetry::CaptureExtensionLoad()
{
    if (!telemetry_enabled) {
        return;
    }

    PostHogEvent event = {
        "extension_load",
        GetMacAddress(),
        {
            {"extension_name", "erpl"},
            {"extension_version", "0.1.0"}
        }
    };
    queue->Capture(event);
}

void PostHogTelemetry::CaptureFunctionExecution(std::string function_name) 
{
    if (!telemetry_enabled) {
        return;
    }

    PostHogEvent event = {
        "function_execution",
        GetMacAddress(),
        {
            {"function_name", function_name},
            {"function_version", "0.1.0"}
        }
    };
    queue->Capture(event);
}


bool PostHogTelemetry::IsEnabled() 
{
    return telemetry_enabled;
}

void PostHogTelemetry::SetEnabled(bool enabled) 
{
    std::lock_guard<mutex> t(thread_lock);
    telemetry_enabled = enabled;
}

std::string PostHogTelemetry::GetAPIKey() 
{
    return api_key;
}

void PostHogTelemetry::SetAPIKey(std::string new_key) 
{
    std::lock_guard<mutex> t(thread_lock);
    api_key = new_key;
}

#ifdef __linux__ 
std::string PostHogTelemetry::GetMacAddress() 
{
    auto device = FindFirstPhysicalDevice();

    std::ifstream file(StringUtil::Format("/sys/class/net/%s/address", device));

    std::string mac_address;
    if (file >> mac_address) {
        return mac_address;
    }

    throw new std::runtime_error(StringUtil::Format("Could not read mac address of device %s", device));
}

bool PostHogTelemetry::IsPhysicalDevice(const std::string& device) {
    std::string path = StringUtil::Format("/sys/class/net/%s/device/driver", device);
    return access(path.c_str(), F_OK) != -1;
}

std::string PostHogTelemetry::FindFirstPhysicalDevice() 
{
    DIR* dir = opendir("/sys/class/net");
    if (!dir) {
        throw new std::runtime_error("Could not open /sys/class/net");
    }

    std::vector<std::string> devices;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_LNK || entry->d_type == DT_DIR) {
            std::string device = entry->d_name;
            if (device != "." && device != "..") {
                devices.push_back(device);
            }
        }
    }
    closedir(dir);

    std::sort(devices.begin(), devices.end());

    for (const std::string& device : devices) {
        if (IsPhysicalDevice(device)) {
            return device;
        }
    }

    throw new std::runtime_error("Could not find physical network device");
}

#elif _WIN32

std::string PostHogTelemetry::GetMacAddress() 
{
    ULONG out_buf_len = sizeof(IP_ADAPTER_INFO);
    std::vector<BYTE> buffer(out_buf_len);

    auto adapter_info = reinterpret_cast<PIP_ADAPTER_INFO>(buffer.data());
    if (GetAdaptersInfo(adapter_info, &out_buf_len) == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(out_buf_len);
        adapter_info = reinterpret_cast<PIP_ADAPTER_INFO>(buffer.data());
    }

    DWORD ret = GetAdaptersInfo(adapter_info, &out_buf_len);
    if (ret != NO_ERROR) {
        return "";
    }

    std::vector<std::string> mac_addresses;
    PIP_ADAPTER_INFO adapter = adapter_info;
    while (adapter) {
        std::ostringstream str_buf;
        for (UINT i = 0; i < adapter->AddressLength; i++) {
            str_buf << std::setw(2) << std::setfill('0') << std::hex << static_cast<int>(adapter->Address[i]);
            if (i != (adapter->AddressLength - 1)) str_buf << '-';
        }
        mac_addresses.push_back(str_buf.str());
        adapter = adapter->Next;
    }

    return mac_addresses.empty() ? "" : mac_addresses.front();
}

#else

std::string PostHogTelemetry::GetMacAddress() 
{
    return "";
}

#endif

// PostHogTelemetry --------------------------------------------------------

} // namespace duckdb