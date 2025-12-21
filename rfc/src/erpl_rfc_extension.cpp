#define DUCKDB_EXTENSION_MAIN

#include "duckdb.hpp"

#include "duckdb/catalog/catalog.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

#include "erpl_rfc_extension.hpp"
#include "pragma_ping.hpp"
#include "pragma_set_trace.hpp"
#include "pragma_ini.hpp"
#include "scanner_invoke.hpp"
#include "scanner_show_groups.hpp"
#include "scanner_describe_function.hpp"
#include "scanner_show_functions.hpp"
#include "scanner_show_tables.hpp"
#include "scanner_describe_fields.hpp"
#include "scanner_read_table.hpp"

#include "telemetry.hpp"
#include "sap_connection.hpp"
#include "sap_secret.hpp"
#include "erpl_tracing.hpp"

namespace duckdb {


    static void OnTelemetryEnabled(ClientContext &context, SetScope scope, Value &parameter)
    {
        PostHogTelemetry::Instance().SetEnabled(parameter.GetValue<bool>());
    }

    static void OnAPIKey(ClientContext &context, SetScope scope, Value &parameter)
    {
        PostHogTelemetry::Instance().SetAPIKey(parameter.GetValue<string>());
    }

    static erpl::TraceLevel StringToTraceLevel(const string &level_str)
    {
        auto level_upper = StringUtil::Upper(level_str);
        if (level_upper == "NONE") {
            return erpl::TraceLevel::NONE;
        }
        if (level_upper == "ERROR") {
            return erpl::TraceLevel::ERROR;
        }
        if (level_upper == "WARN") {
            return erpl::TraceLevel::WARN;
        }
        if (level_upper == "INFO") {
            return erpl::TraceLevel::INFO;
        }
        if (level_upper == "DEBUG") {
            return erpl::TraceLevel::DEBUG_LEVEL;
        }
        if (level_upper == "TRACE") {
            return erpl::TraceLevel::TRACE;
        }
        throw BinderException("Invalid trace level: " + level_str + ". Valid levels are: NONE, ERROR, WARN, INFO, DEBUG, TRACE");
    }

    static void OnTraceEnabled(ClientContext &, SetScope, Value &parameter)
    {
        erpl::ErplTracer::Instance().SetEnabled(parameter.GetValue<bool>());
    }

    static void OnTraceLevel(ClientContext &, SetScope, Value &parameter)
    {
        auto level_str = parameter.GetValue<string>();
        auto level = StringToTraceLevel(level_str);
        erpl::ErplTracer::Instance().SetLevel(level);
    }

    static void OnTraceOutput(ClientContext &, SetScope, Value &parameter)
    {
        auto output = parameter.GetValue<string>();
        auto output_upper = StringUtil::Upper(output);
        if (output_upper != "CONSOLE" && output_upper != "FILE" && output_upper != "BOTH") {
            throw BinderException("Invalid trace output: " + output + ". Valid outputs are: console, file, both");
        }
        erpl::ErplTracer::Instance().SetOutputMode(StringUtil::Lower(output));
    }

    static void OnTraceDirectory(ClientContext &, SetScope, Value &parameter)
    {
        erpl::ErplTracer::Instance().SetTraceDirectory(parameter.GetValue<string>());
    }

    static void OnTraceMaxFileSize(ClientContext &, SetScope, Value &parameter)
    {
        auto max_size = parameter.GetValue<int64_t>();
        if (max_size < 0) {
            throw BinderException("Trace max file size must be non-negative");
        }
        erpl::ErplTracer::Instance().SetMaxFileSize(max_size);
    }

    static void OnTraceRotation(ClientContext &, SetScope, Value &parameter)
    {
        erpl::ErplTracer::Instance().SetRotation(parameter.GetValue<bool>());
    }

    static void RegisterConfiguration(ExtensionLoader &loader)
    {
        auto &instance = loader.GetDatabaseInstance();
        auto &config = DBConfig::GetConfig(instance);
        config.AddExtensionOption("erpl_telemetry_enabled", "Enable ERPL telemetry, see https://erpl.io/telemetry for details.", 
                                  LogicalType::BOOLEAN, Value(true), OnTelemetryEnabled);
        config.AddExtensionOption("erpl_telemetry_key", "Telemetry key, see https://erpl.io/telemetry for details.", LogicalType::VARCHAR, 
                                  Value("phc_t3wwRLtpyEmLHYaZCSszG0MqVr74J6wnCrj9D41zk2t"), OnAPIKey);

        config.AddExtensionOption("erpl_trace_enabled", "Enable ERPL RFC tracing functionality", LogicalType::BOOLEAN,
                                  Value(false), OnTraceEnabled);
        config.AddExtensionOption("erpl_trace_level", "Set ERPL RFC trace level (TRACE, DEBUG, INFO, WARN, ERROR)",
                                  LogicalType::VARCHAR, Value("INFO"), OnTraceLevel);
        config.AddExtensionOption("erpl_trace_output", "Set ERPL RFC trace output (console, file, both)",
                                  LogicalType::VARCHAR, Value("console"), OnTraceOutput);
        config.AddExtensionOption("erpl_trace_file_path", "Set ERPL RFC trace file path", LogicalType::VARCHAR,
                                  Value("trace"), OnTraceDirectory);
        config.AddExtensionOption("erpl_trace_max_file_size", "Set ERPL RFC trace file max size in bytes",
                                  LogicalType::BIGINT, Value::BIGINT(0), OnTraceMaxFileSize);
        config.AddExtensionOption("erpl_trace_rotation", "Enable ERPL RFC trace file rotation", LogicalType::BOOLEAN,
                                  Value(false), OnTraceRotation);

        auto provider = make_uniq<RfcEnvironmentCredentialsProvider>(config);
        provider->SetAll();

        RegisterSapSecretType(loader);
    }

    static void RegisterRfcFunctions(ExtensionLoader &loader)
    {
        loader.RegisterFunction(CreateRfcPingPragma());
        loader.RegisterFunction(CreateRfcInvokeScanFunction());
        loader.RegisterFunction(CreateRfcShowFunctionScanFunction());
        loader.RegisterFunction(CreateRfcShowGroupScanFunction());
        loader.RegisterFunction(CreateRfcDescribeFunctionScanFunction());
        loader.RegisterFunction(CreateRfcShowTablesScanFunction());
        loader.RegisterFunction(CreateRfcDescribeFieldsScanFunction());
        loader.RegisterFunction(CreateRfcReadTableScanFunction());
        loader.RegisterFunction(CreateRfcSetTraceLevelPragma());
        loader.RegisterFunction(CreateRfcSetTraceDirPragma());
        loader.RegisterFunction(CreateRfcSetMaximumTraceFileSizePragma());
        loader.RegisterFunction(CreateRfcSetMaximumStoredTraceFilesPragma());
        loader.RegisterFunction(CreateRfcSetIniPathPragma());
        loader.RegisterFunction(CreateRfcReloadIniFilePragma());
    }
    
    static void LoadInternal(ExtensionLoader &loader)
    {
        PostHogTelemetry::Instance().CaptureExtensionLoad("erpl_rfc");

        RegisterConfiguration(loader);
        RegisterRfcFunctions(loader);
    }

    void ErplRfcExtension::Load(ExtensionLoader &loader) 
    {
        LoadInternal(loader);
    }

    std::string ErplRfcExtension::Name() {
        return "erpl_rfc";
    }

} // namespace duckdb

extern "C" {
    DUCKDB_CPP_EXTENSION_ENTRY(erpl_rfc, loader) {
        duckdb::LoadInternal(loader);
    }
}

#ifndef DUCKDB_EXTENSION_MAIN
#error DUCKDB_EXTENSION_MAIN not defined
#endif
