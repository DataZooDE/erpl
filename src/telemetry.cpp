#include "telemetry.hpp"

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
    cli = make_uniq<duckdb_httplib_openssl::Client>("https://eu.posthog.com");
    url = "/batch/";
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

    auto res = cli->Post(url.c_str(), payload, "application/json");
    if (res && res->status != 200) {
        throw new std::runtime_error(StringUtil::Format("Sending posthog event failed: %s", res->body));
    }
}

// PostHogWorker ----------------------------------------------------------------

PostHogTelemetry::PostHogTelemetry()
{ 
    std::string api_key = "phc_t3wwRLtpyEmLHYaZCSszG0MqVr74J6wnCrj9D41zk2t";
    queue = make_uniq<TelemetryWorkQueue<PostHogWorker, PostHogEvent>>();
    std::function<std::shared_ptr<PostHogWorker>()> worker_factory = [api_key]() { return make_shared<PostHogWorker>(api_key); };
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
    return "";
}
#else
std::string PostHogTelemetry::GetMacAddress() 
{
    return "";
}
#endif

// PostHogTelemetry --------------------------------------------------------

} // namespace duckdb