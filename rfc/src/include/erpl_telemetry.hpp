#pragma once

// -----------------------------------------------------------------------------
// erpl telemetry helpers — thin, product-specific layer over the shared
// DataZooDE/posthog-telemetry library.
//
// PRIVACY CONTRACT (enforced by construction, checked in code review):
// every value emitted through these helpers is EITHER
//   (a) a compile-time constant drawn from the small enums below, OR
//   (b) a pure number (durations, counts).
// We NEVER put into any property: SAP host names, user names, client numbers,
// RFC/BAPI/function-module names or their arguments, table names, SQL text,
// row data, or free-form SAP/RFC error messages.
//
// Cross-product schema (single source of truth, telemetry_schema 2):
//   third_party/posthog-telemetry/TELEMETRY-SCHEMA.md
// What erpl actually collects: erpl's TELEMETRY.md.
//
// Header-only + all-inline on purpose: erpl_rfc, erpl_bics, erpl_odp and
// erpl_tunnel are four separately-compiled/linked extensions (bics/odp are even
// separate repos). They all already have ../rfc/src/include on their include
// path, so a single header reaches every one of them with no CMake changes and
// no cross-extension link dependency. It also compiles cleanly under
// POSTHOG_TELEMETRY_DISABLED (the library ships no-op stubs for that lane).
// -----------------------------------------------------------------------------

#include "telemetry.hpp"

#include <chrono>
#include <exception>
#include <string>
#include <utility>

namespace duckdb {
namespace erpl_telemetry {

// product_version stamped on every event's envelope. Kept as ONE header
// constant (not a per-translation-unit -D macro) so all four extensions agree
// regardless of load order. Bump on release (erpl uses CalVer, e.g. 2026.07.02).
static constexpr const char *kProductVersion = "2026.08.31";

// -- Bounded enums: the ONLY feature values we ever emit. Extend by adding a
//    constant here — never pass a free-form string. -------------------------
namespace feature {
    static constexpr const char *kConnectionOpened = "connection_opened"; // + auth_kind
    static constexpr const char *kSapRfc           = "sap_rfc";           // + duration_ms
    static constexpr const char *kBapiCall         = "bapi_call";         // + duration_ms
    static constexpr const char *kOdpExtract       = "odp_extract";       // + mode, duration_ms
    static constexpr const char *kRfcTableRead     = "rfc_table_read";    // + duration_ms
    // Reserved for the OData/CDS extensions (not present in this repo yet):
    static constexpr const char *kODataRead        = "odata_read";
    static constexpr const char *kCdsRead          = "cds_read";
}

// -- Enumerated auth kinds for connection_opened. No credential material. ----
namespace auth_kind {
    static constexpr const char *kBasic = "basic";
    static constexpr const char *kSso   = "sso";
    static constexpr const char *kSnc   = "snc";
    static constexpr const char *kX509  = "x509"; // reserved; erpl has no x509 path yet
}

// -- Enumerated error classes for $exception. NEVER a SAP/RFC message. -------
namespace error_class {
    static constexpr const char *kConnectionFailed = "connection_failed";
    static constexpr const char *kAuthError        = "auth_error";
    static constexpr const char *kRfcError         = "rfc_error";
    static constexpr const char *kTimeout          = "timeout";
}

// -- Enumerated phases (where in a feature the error happened). --------------
namespace phase {
    static constexpr const char *kConnect = "connect";
    static constexpr const char *kInvoke  = "invoke";
    static constexpr const char *kRead    = "read";
}

// Idempotent per-process product identification. Safe to call from every
// sub-extension's LoadInternal: SetProduct is last-wins with identical values,
// the first AssociateGroup("deployment", …) emits $groupidentify and the rest
// are cheap. distinct_id doubles as the deployment key (per TELEMETRY-SCHEMA §4).
//
// Enterprise builds with a license key should additionally call
//   AssociateGroup("account", sha256(license_id), {"edition","enterprise"})
// hashing the id, never sending the raw key. OSS erpl has no license key, so we
// stay on edition "oss" with only the deployment group.
inline void InitProduct() {
    auto &t = PostHogTelemetry::Instance();
    t.SetProduct("erpl", kProductVersion, "oss");
    t.AssociateGroup("deployment", PostHogTelemetry::GetDistinctId());
}

// Emits feature_used {feature: "connection_opened", auth_kind}. auth_kind must
// be one of the auth_kind:: constants above.
inline void CaptureConnectionOpened(const std::string &kind) {
    // Sent: feature="connection_opened", auth_kind (enum). Nothing else.
    PostHogTelemetry::Instance().CaptureFeature(
        feature::kConnectionOpened, {{"auth_kind", kind}});
}

// Emits $exception {error_class, feature, phase} — all three are enum constants.
inline void CaptureError(const char *err_class, const char *feat, const char *ph) {
    // Sent: error_class (enum), feature (enum), phase (enum). No message/PII.
    PostHogTelemetry::Instance().CaptureError(err_class, {{"feature", feat}, {"phase", ph}});
}

// RAII stopwatch that emits feature_used {feature[, <extra enums/numbers>],
// duration_ms} once, on scope exit (or on an explicit Fire()). `extra` must
// contain only enum-constant or numeric values. Cheap when telemetry is off:
// the guarded IsEnabled() check skips building the event; the tiny steady_clock
// read is never on a per-row path (construct these once per statement/call).
class ScopedFeature {
public:
    explicit ScopedFeature(const char *feat, PropertyMap extra = {})
        : _feature(feat), _extra(std::move(extra)),
          _start(std::chrono::steady_clock::now()),
          _uncaught(std::uncaught_exceptions()), _fired(false) {}

    // feature_used means a *successful* capability use. If the scope is exiting
    // because a new exception is propagating, stay silent — the failure is
    // reported via an enumerated $exception instead. Normal exits emit.
    ~ScopedFeature() {
        if (std::uncaught_exceptions() > _uncaught) {
            return;
        }
        Fire();
    }

    // Suppress emission (idempotent). Call before rethrowing on an error path
    // so a *failed* operation does not emit feature_used — the $exception event
    // carries the failure instead.
    void Cancel() { _fired = true; }

    // Emit now (idempotent). Call explicitly to time only the SAP round-trip
    // rather than the whole enclosing scope.
    void Fire() {
        if (_fired) {
            return;
        }
        _fired = true;
        auto &t = PostHogTelemetry::Instance();
        if (!t.IsEnabled()) {
            return; // zero-cost: don't even build the property map
        }
        auto ms = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
                      std::chrono::steady_clock::now() - _start)
                      .count();
        PropertyMap props = _extra;
        props["duration_ms"] = ms; // double -> JSON number
        t.CaptureFeature(_feature, std::move(props));
    }

    ScopedFeature(const ScopedFeature &) = delete;
    ScopedFeature &operator=(const ScopedFeature &) = delete;

private:
    const char *_feature;
    PropertyMap _extra;
    std::chrono::steady_clock::time_point _start;
    int _uncaught;
    bool _fired;
};

} // namespace erpl_telemetry
} // namespace duckdb
