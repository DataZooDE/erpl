#include <thread>
#include <chrono>

#include "catch.hpp"
#include "test_helpers.hpp"
#include "duckdb.hpp"

#include "telemetry.hpp"
#include "erpl_telemetry.hpp"

#include <vector>
#include <fstream>
#include <cstdlib>

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

    TelemetryTaskQueue<PostHogEvent> queue;
    queue.EnqueueTask([api_key](auto event) { PostHogProcess(api_key, event); }, event);

    std::this_thread::sleep_for(std::chrono::seconds(1));
}

TEST_CASE("Test finding mac address", "[telemetry]")
{
    std::string mac_address = PostHogTelemetry::GetMacAddress();
    REQUIRE(mac_address != "");
    REQUIRE(mac_address.length() == 17);
}

// Drives erpl's real telemetry helpers (erpl_telemetry.hpp) through the real
// library, captures the enriched batch via the test-transport seam, and asserts
// the schema-2 envelope + event shapes. Prints every payload so a human can
// eyeball that only enumerated/numeric props leave the machine (no PII). This is
// exactly what would be POSTed to the default PostHog project.
TEST_CASE("erpl telemetry emits schema-2 envelope + events", "[telemetry_verify]")
{
    auto &t = PostHogTelemetry::Instance();
    t.ResetShutdownForTesting();
    t.SetEnabled(true);
    t.SetAPIKey("phc_t3wwRLtpyEmLHYaZCSszG0MqVr74J6wnCrj9D41zk2t"); // default project
    t.SetAutoFlushEnabledForTesting(false);   // buffer, then drive send via Flush()
    t.SetPromptFunctionCallsForTesting(0);    // aggregate every function call deterministically

    std::vector<PostHogEvent> captured;
    t.SetTransportForTesting(
        [&](const std::string &, const std::string &, const std::vector<PostHogEvent> &evs) {
            for (auto &e : evs) captured.push_back(e);
        });

    // ---- the ACTUAL erpl call sites ----
    erpl_telemetry::InitProduct();                 // SetProduct(erpl) + AssociateGroup(deployment)
    t.CaptureExtensionLoad("erpl_rfc");
    erpl_telemetry::CaptureConnectionOpened(erpl_telemetry::auth_kind::kBasic);
    { erpl_telemetry::ScopedFeature f(erpl_telemetry::feature::kSapRfc); }
    { erpl_telemetry::ScopedFeature f(erpl_telemetry::feature::kBapiCall); }
    { erpl_telemetry::ScopedFeature f(erpl_telemetry::feature::kRfcTableRead); }
    { erpl_telemetry::ScopedFeature f(erpl_telemetry::feature::kOdpExtract, {{"mode", "full"}}); }
    { erpl_telemetry::ScopedFeature f(erpl_telemetry::feature::kOdpExtract, {{"mode", "delta"}}); }
    erpl_telemetry::CaptureError(erpl_telemetry::error_class::kAuthError,
                                 erpl_telemetry::feature::kConnectionOpened,
                                 erpl_telemetry::phase::kConnect);
    t.RecordFunctionCall("sap_read_table");
    t.RecordFunctionCall("sap_read_table");
    t.RecordFunctionCall("sap_rfc_invoke");
    t.Flush();

    // ---- dump every enriched payload ----
    for (size_t i = 0; i < captured.size(); ++i) {
        WARN("EVENT " << (i + 1) << " " << captured[i].event_name << " "
                      << captured[i].GetPropertiesJson());
    }

    auto names_contains = [&](const std::string &n) {
        for (auto &e : captured) if (e.event_name == n) return true;
        return false;
    };
    auto find_feature = [&](const std::string &feat) {
        for (auto &e : captured) {
            if (e.event_name != "feature_used") continue;
            auto it = e.properties.find("feature");
            if (it != e.properties.end() && it->second.ToJson() == "\"" + feat + "\"") return true;
        }
        return false;
    };

    REQUIRE(!captured.empty());
    // Events present
    REQUIRE(names_contains("extension_loaded"));
    REQUIRE(names_contains("$groupidentify"));       // deployment association
    REQUIRE(names_contains("feature_used"));
    REQUIRE(names_contains("$exception"));
    REQUIRE(names_contains("function_executed"));
    // Every enum feature we emit
    REQUIRE(find_feature("connection_opened"));
    REQUIRE(find_feature("sap_rfc"));
    REQUIRE(find_feature("bapi_call"));
    REQUIRE(find_feature("rfc_table_read"));
    REQUIRE(find_feature("odp_extract"));

    // Envelope on a representative event
    const PostHogEvent *sample = nullptr;
    for (auto &e : captured) if (e.event_name == "feature_used") { sample = &e; break; }
    REQUIRE(sample != nullptr);
    auto has = [&](const char *k) { return sample->properties.find(k) != sample->properties.end(); };
    REQUIRE(has("product"));
    REQUIRE(sample->properties.at("product").ToJson() == "\"erpl\"");
    REQUIRE(has("product_version"));
    REQUIRE(has("product_edition"));
    REQUIRE(has("telemetry_schema"));
    REQUIRE(sample->properties.at("telemetry_schema").ToJson() == "2"); // JSON number, unquoted
    REQUIRE(has("os"));
    REQUIRE(has("arch"));
    REQUIRE(has("$session_id"));
    REQUIRE(has("$groups"));   // deployment carried once associated

    // function_executed aggregates 2x sap_read_table into call_count 2
    for (auto &e : captured) {
        if (e.event_name != "function_executed") continue;
        auto fn = e.properties.find("function_name");
        if (fn != e.properties.end() && fn->second.ToJson() == "\"sap_read_table\"") {
            REQUIRE(e.properties.at("call_count").ToJson() == "2");
        }
    }

    // Optionally write the exact /batch/ wire payload (same format as the
    // library's PostOneChunk) so default-project ingestion can be confirmed
    // out-of-band via curl.
    if (const char *pp = std::getenv("ERPL_TELEMETRY_DUMP")) {
        auto esc = [](const std::string &s) {
            std::string o = "\"";
            for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; }
            o += "\"";
            return o;
        };
        std::string batch;
        for (size_t i = 0; i < captured.size(); ++i) {
            auto &e = captured[i];
            if (i) batch += ",";
            std::string ts = e.timestamp.empty() ? e.GetNowISO8601() : e.timestamp;
            batch += "{\"event\":" + esc(e.event_name) +
                     ",\"distinct_id\":" + esc(e.distinct_id) +
                     ",\"properties\":" + e.GetPropertiesJson() +
                     ",\"timestamp\":" + esc(ts) + "}";
        }
        std::ofstream(pp) << "{\"api_key\":\"phc_t3wwRLtpyEmLHYaZCSszG0MqVr74J6wnCrj9D41zk2t\","
                          << "\"batch\":[" << batch << "]}";
    }

    t.SetTransportForTesting({});
    t.SetAutoFlushEnabledForTesting(true);
}