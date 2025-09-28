#define NOMINMAX

#include "telemetry.hpp"
#include "duckdb/common/string_util.hpp"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <string>
#include <vector>
#ifdef __linux__
#include <unistd.h>
#endif
#ifdef _WIN32
#include <iomanip>
#include <sstream>
#endif

#ifdef __linux__

#include <dirent.h>

#elif _WIN32

#ifndef _MSC_VER
#define _MSC_VER 1936
#endif

#include <winsock2.h>
#include <iphlpapi.h>

#elif __APPLE__

#include <iostream>
#include <stdexcept>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <cstring>

#endif

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.hpp"

namespace duckdb {

std::string PostHogEvent::GetPropertiesJson() const
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

std::string PostHogEvent::GetNowISO8601() const
{
    std::time_t t = std::time(nullptr);
    std::tm tm = *std::localtime(&t);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%FT%TZ", &tm);
    return std::string(buffer);
}

// PostHogEvent -----------------------------------------------------------------

void PostHogProcess(const std::string api_key, const PostHogEvent &event) 
{
    auto re = duckdb_re2::Regex("*");
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

    try {
        auto cli = duckdb_httplib_openssl::Client("https://eu.posthog.com");
        auto url = "/batch/";
        if (cli.is_valid() == false) {
            // Silently fail instead of throwing - telemetry should not break the application
            return;
        }
        auto res = cli.Post(url, payload, "application/json");
        if (res && res->status != 200) {
            // Silently fail instead of throwing - telemetry should not break the application
            return;
        }
        cli.stop();
    } catch (...) {
        // Silently fail instead of throwing - telemetry should not break the application
        return;
    }
}

// PostHogProcess ----------------------------------------------------------------

PostHogTelemetry::PostHogTelemetry()
    : _telemetry_enabled(true),
      _api_key("phc_t3wwRLtpyEmLHYaZCSszG0MqVr74J6wnCrj9D41zk2t"),
      _queue(nullptr)
{  }

PostHogTelemetry::~PostHogTelemetry()
{
    if (_queue) {
        _queue->Stop();
    }
}

PostHogTelemetry& PostHogTelemetry::Instance()
{
    static PostHogTelemetry instance;
    return instance;
}

void PostHogTelemetry::EnsureQueueInitialized()
{
    if (!_queue) {
        _queue = std::make_unique<TelemetryTaskQueue<PostHogEvent>>();
    }
}

void PostHogTelemetry::CaptureExtensionLoad()
{
    CaptureExtensionLoad("erpl_rfc");
}

void PostHogTelemetry::CaptureExtensionLoad(std::string extension_name)
{
    if (!_telemetry_enabled) {
        return;
    }

    PostHogEvent event = {
        "extension_load",
        GetMacAddressSafe(),
        {
            {"extension_name", extension_name},
            {"extension_version", "0.1.0"},
            {"extension_platform", "linux" }
        }
    };
    auto api_key = this->_api_key;
    EnsureQueueInitialized();
    _queue->EnqueueTask([api_key](auto event) { PostHogProcess(api_key, event); }, event);
}

void PostHogTelemetry::CaptureFunctionExecution(std::string function_name)
{
    if (!_telemetry_enabled) {
        return;
    }

    PostHogEvent event = {
        "function_execution",
        GetMacAddressSafe(),
        {
            {"function_name", function_name},
            {"function_version", "0.1.0"}
        }
    };
    auto api_key = this->_api_key;
    EnsureQueueInitialized();
    _queue->EnqueueTask([api_key](auto event) { PostHogProcess(api_key, event); }, event);
}


bool PostHogTelemetry::IsEnabled() 
{
    return _telemetry_enabled;
}

void PostHogTelemetry::SetEnabled(bool enabled) 
{
    std::lock_guard<mutex> t(_thread_lock);
    _telemetry_enabled = enabled;
}

std::string PostHogTelemetry::GetAPIKey() 
{
    return _api_key;
}

void PostHogTelemetry::SetAPIKey(std::string new_key) 
{
    std::lock_guard<mutex> t(_thread_lock);
    _api_key = new_key;
}

std::string PostHogTelemetry::GetMacAddressSafe() 
{
    try {
        return GetMacAddress();
    } catch (std::exception &e) {
        return "00:00:00:00:00:00";
    }
}

#ifdef __linux__

std::string PostHogTelemetry::GetMacAddress() 
{
    auto device = FindFirstPhysicalDevice();
    if (device.empty()) {
        return "00:00:00:00:00:00";
    }

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

    return "";
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

#elif __APPLE__

std::string PostHogTelemetry::GetMacAddress() 
{
    struct ifaddrs *ifap, *ifa;
    struct sockaddr_dl *sdl = nullptr;
    char mac_address[18] = {0};

    if (getifaddrs(&ifap) != 0) {
        throw std::runtime_error("getifaddrs() failed!");
    }

    for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr->sa_family == AF_LINK && strcmp(ifa->ifa_name, "en0") == 0) {
            sdl = (struct sockaddr_dl *)ifa->ifa_addr;
            break;
        }
    }

    if (sdl && sdl->sdl_alen == 6) {
        unsigned char *ptr = (unsigned char *)LLADDR(sdl);
        snprintf(mac_address, sizeof(mac_address), "%02x:%02x:%02x:%02x:%02x:%02x",
                 ptr[0], ptr[1], ptr[2], ptr[3], ptr[4], ptr[5]);
    } else {
        strncpy(mac_address, "00:00:00:00:00:00", sizeof(mac_address));
    }

    freeifaddrs(ifap);

    return std::string(mac_address);
}

#else

std::string PostHogTelemetry::GetMacAddress() 
{
    return "";
}

#endif

// PostHogTelemetry --------------------------------------------------------

} // namespace duckdb