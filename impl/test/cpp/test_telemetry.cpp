#include <thread>
#include <chrono>

#include "catch.hpp"
#include "test_helpers.hpp"
#include "duckdb.hpp"

#include "telemetry.hpp"

using namespace duckdb;
using namespace std;


TEST_CASE("Test recording of tracking events", "[telemetry]")
{
    PostHogEvent event = {
        "test_event",
        "1234",
        {
            {"test_property1", "test_value"},
            {"test_property2", "test_value"}
        }
    };

    std::string api_key = "phc_t3wwRLtpyEmLHYaZCSszG0MqVr74J6wnCrj9D41zk2t";

    TelemetryWorkQueue<PostHogWorker, PostHogEvent> queue;
    std::function<std::shared_ptr<PostHogWorker>()> worker_factory = [api_key]() { return make_shared<PostHogWorker>(api_key); };
    queue.InitializeWorkers(worker_factory);
    queue.Capture(event);

    std::this_thread::sleep_for(std::chrono::seconds(1));
}

TEST_CASE("Test finding mac address", "[telemetry]")
{
    std::string mac_address = PostHogTelemetry::GetMacAddress();
    REQUIRE(mac_address != "");
    REQUIRE(mac_address.length() == 17);
}