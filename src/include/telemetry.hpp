#pragma once

#include <vector>
#include <mutex>
#include <queue>
#include <thread>
#include <condition_variable>
#include <iostream>
#include <fstream>
#include <dirent.h>

#ifdef _WIN32

#include <windows.h>
#include <iphlpapi.h>

#pragma comment(lib, "iphlpapi.lib")

#endif 


#include "duckdb.hpp"
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.hpp"

namespace duckdb {

class PostHogEvent
{
    public:
        std::string GetPropertiesJson();
        std::string GetNowISO8601();

        std::string event_name;
        std::string distinct_id; // user distinct id
        std::map<std::string, std::string> properties;
};

class PostHogWorker
{
    public:
        PostHogWorker(std::string api_key);
        void Process(PostHogEvent &event);

    private:
        std::unique_ptr<duckdb_httplib_openssl::Client> cli;
        std::string url;
        std::string api_key;
};

template <typename TWorker, typename TEvent>
class TelemetryWorkQueue 
{
    public:
        TelemetryWorkQueue() : stop(false) { };

        ~TelemetryWorkQueue() 
        { 
            StopWorkers();
        };

        void InitializeWorkers(std::function<std::shared_ptr<TWorker>()> &worker_factory)
        {
            InitializeWorkers(1, worker_factory);
        }

        void InitializeWorkers(unsigned int n_threads, std::function<std::shared_ptr<TWorker>()> &worker_factory)
        {
            for(unsigned int i = 0; i < n_threads; ++i) {
                auto worker = worker_factory();
                std::thread worker_thread([this, worker] { 
                    while(!stop) 
                    {
                        std::unique_lock<std::mutex> lock(thread_lock);
                        condition.wait(lock, [this] { return !queue.empty() || stop; });
                        if(stop) {
                            break;
                        }

                        TEvent item = queue.front();
                        queue.pop();
                        lock.unlock();
                        
                        try {
                            worker->Process(item);
                        } catch (std::exception &ex) {
                            printf("Warning: Error during processing telementry event: %s\n", ex.what());
                        }
                    }
                });
                workers.push_back(std::move(worker_thread));
            }
        }

        void StopWorkers() 
        {
            {
                std::unique_lock<std::mutex> lock(thread_lock);
                stop = true;
            }
            condition.notify_all();
            for (std::thread& worker : workers)
            {
                worker.join();
            }
        }

        void Capture(TEvent item) 
        {
            {
                std::unique_lock<std::mutex> lock(thread_lock);
                queue.push(item);
            }
            condition.notify_one();
        }

    private:
        std::vector<std::thread> workers;
        std::queue<TEvent> queue;
        std::mutex thread_lock;
        std::condition_variable condition;
        bool stop;
};

class PostHogTelemetry 
{
    public:
        static PostHogTelemetry& Instance();
        static std::string GetMacAddress();

        void CaptureExtensionLoad();
        void CaptureFunctionExecution(std::string function_name);
       
    private:
        PostHogTelemetry(); 

        static bool IsPhysicalDevice(const std::string& device);
        static std::string FindFirstPhysicalDevice();

        bool telemetry_enabled = true;
        std::unique_ptr<TelemetryWorkQueue<PostHogWorker, PostHogEvent>> queue;
};

} // namespace duckdb