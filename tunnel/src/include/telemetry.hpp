#pragma once

#include <chrono>
#include <condition_variable>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "duckdb.hpp"

namespace duckdb {

class PostHogEvent
{
    public:
        std::string GetPropertiesJson() const;
        std::string GetNowISO8601() const;

        std::string event_name;
        std::string distinct_id; // user distinct id
        std::map<std::string, std::string> properties;
};

class PostHogWorker
{
    public:
        PostHogWorker(std::string api_key);
        void Process(const PostHogEvent &event);

    private:
        std::string api_key;
};

void PostHogProcess(const std::string api_key, const PostHogEvent &event);

template <typename TEvent>
class TelemetryTaskQueue {
public:
    TelemetryTaskQueue(unsigned int n_threads = 1)
    {
        _stop = false;
        for (unsigned int i = 0; i < n_threads; ++i) {
            _workers.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->_queue_mutex);
                        this->_condition.wait(lock, [this] {
                            return this->_stop || !this->_tasks.empty();
                        });

                        if (this->_stop)
                            return;

                        task = std::move(this->_tasks.front());
                        this->_tasks.pop();
                    }

                    task();
                }
            });
        }
    }

    ~TelemetryTaskQueue()
    {
        {
            std::unique_lock<std::mutex> lock(_queue_mutex);
            _stop = true;
        }
        _condition.notify_all();
        for (auto &worker : _workers) {
            worker.detach();
        }
    }

    template<class F, class... Args>
    auto EnqueueTask(F&& f, Args&&... args) 
        -> std::future<typename std::result_of<F(Args...)>::type> {
        using return_type = typename std::result_of<F(Args...)>::type;

        auto task = std::make_shared< std::packaged_task<return_type()> >(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
            
        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(_queue_mutex);

            if(_stop)
                throw std::runtime_error("Enqueue on stopped TelemetryTaskQueue");

            _tasks.emplace([task](){ (*task)(); });
        }
        _condition.notify_one();
        return res;
    }

private:
    std::vector<std::thread> _workers;
    std::queue< std::function<void()> > _tasks;
    
    std::mutex _queue_mutex;
    std::condition_variable _condition;
    bool _stop;
};

class PostHogTelemetry 
{
    public:
        static PostHogTelemetry& Instance();
        static std::string GetMacAddress();
        static std::string GetMacAddressSafe();

        void CaptureExtensionLoad();
        void CaptureExtensionLoad(std::string extension_name);
        void CaptureFunctionExecution(std::string function_name);
       
        bool IsEnabled();
        void SetEnabled(bool enabled);

        std::string GetAPIKey();
        void SetAPIKey(std::string api_key);
        
    private:
        PostHogTelemetry(); 
        ~PostHogTelemetry();

        static bool IsPhysicalDevice(const std::string& device);
        static std::string FindFirstPhysicalDevice();

        TelemetryTaskQueue<PostHogEvent> _queue;

        bool _telemetry_enabled;
        std::string _api_key;
        std::mutex _thread_lock;
};

} // namespace duckdb 