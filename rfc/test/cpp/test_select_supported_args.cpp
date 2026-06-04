#include "catch.hpp"
#include "test_helpers.hpp"
#include "duckdb.hpp"

#include "duckdb_argument_helper.hpp"

using namespace duckdb;
using namespace std;

namespace {

// Mirrors the argument list ERPL builds for BICS_PROV_GET_RESULT_SET.
Value MakeResultSetArgs()
{
    return ArgBuilder()
        .Add("I_DATA_PROVIDER_HANDLE", Value("0003"))
        .Add("I_OLAP_CACHE_WARM_UP", Value(""))
        .Add("I_MAX_DATA_CELLS", Value::BIGINT(1000000))
        .Add("I_SKIP_READ", Value(""))
        .Add("I_REFRESH_DATA", Value(""))
        .Add("I_APPLICATION_HANDLE", Value("0001"))
        .Add("I_DELTA_UPDATE", Value(""))
        .Add("I_CONFIRM_AUTORETRY", Value(""))
        .Build();
}

std::set<std::string> FieldNames(const Value &struct_value)
{
    std::set<std::string> names;
    for (auto &child : StructType::GetChildTypes(struct_value.type())) {
        names.insert(child.first);
    }
    return names;
}

// The args BICS_PROV_GET_RESULT_SET defines on an older BW release (no autoretry).
const std::set<std::string> kOlderRelease = {
    "I_APPLICATION_HANDLE", "I_DATA_PROVIDER_HANDLE", "I_DELTA_UPDATE",
    "I_MAX_DATA_CELLS", "I_OLAP_CACHE_WARM_UP", "I_REFRESH_DATA", "I_SKIP_READ"
};
const std::set<std::string> kDroppable = { "I_CONFIRM_AUTORETRY" };

} // namespace

TEST_CASE("SelectSupportedNamedArgs drops an unsupported droppable parameter", "[select_supported_args]") {
    // Older BW release whose BICS_PROV_GET_RESULT_SET has no I_CONFIRM_AUTORETRY (issue #81).
    std::vector<std::string> dropped;
    auto filtered = SelectSupportedNamedArgs(MakeResultSetArgs(), kOlderRelease, kDroppable, &dropped);

    auto names = FieldNames(filtered);
    REQUIRE(names.count("I_CONFIRM_AUTORETRY") == 0);
    REQUIRE(names.count("I_DATA_PROVIDER_HANDLE") == 1);
    REQUIRE(names.size() == 7);

    REQUIRE(dropped.size() == 1);
    REQUIRE(dropped[0] == "I_CONFIRM_AUTORETRY");
}

TEST_CASE("SelectSupportedNamedArgs keeps every field on a newer release", "[select_supported_args]") {
    auto newer_release = kOlderRelease;
    newer_release.insert("I_CONFIRM_AUTORETRY");

    std::vector<std::string> dropped;
    auto filtered = SelectSupportedNamedArgs(MakeResultSetArgs(), newer_release, kDroppable, &dropped);

    REQUIRE(FieldNames(filtered).size() == 8);
    REQUIRE(dropped.empty());
}

TEST_CASE("SelectSupportedNamedArgs keeps an unsupported field that is NOT droppable", "[select_supported_args]") {
    // A misspelled / unexpected required parameter must NOT be silently dropped:
    // it should be retained so argument adaptation raises a clear error.
    auto args = ArgBuilder()
        .Add("I_DATA_PROVIDER_HANDLE", Value("0003"))
        .Add("I_TYPodanger", Value("oops"))   // unsupported and not in the droppable allowlist
        .Build();

    std::vector<std::string> dropped;
    auto filtered = SelectSupportedNamedArgs(args, {"I_DATA_PROVIDER_HANDLE"}, kDroppable, &dropped);

    REQUIRE(FieldNames(filtered).count("I_TYPodanger") == 1);
    REQUIRE(dropped.empty());
}

TEST_CASE("SelectSupportedNamedArgs matches names case-insensitively", "[select_supported_args]") {
    std::set<std::string> supported = { "i_data_provider_handle" };
    std::set<std::string> droppable = { "i_confirm_autoretry" };

    auto args = ArgBuilder()
        .Add("I_DATA_PROVIDER_HANDLE", Value("0003"))
        .Add("I_CONFIRM_AUTORETRY", Value(""))
        .Build();

    std::vector<std::string> dropped;
    auto filtered = SelectSupportedNamedArgs(args, supported, droppable, &dropped);

    REQUIRE(FieldNames(filtered).count("I_DATA_PROVIDER_HANDLE") == 1);
    REQUIRE(dropped.size() == 1);
    REQUIRE(dropped[0] == "I_CONFIRM_AUTORETRY");
}

TEST_CASE("SelectSupportedNamedArgs preserves values of kept fields", "[select_supported_args]") {
    auto args = ArgBuilder()
        .Add("I_DATA_PROVIDER_HANDLE", Value("0042"))
        .Add("I_CONFIRM_AUTORETRY", Value(""))
        .Build();

    auto filtered = SelectSupportedNamedArgs(args, {"I_DATA_PROVIDER_HANDLE"}, kDroppable, nullptr);
    auto children = StructValue::GetChildren(filtered);

    REQUIRE(children.size() == 1);
    REQUIRE(children[0].ToString() == "0042");
}

TEST_CASE("SelectSupportedNamedArgs returns non-struct input unchanged", "[select_supported_args]") {
    auto scalar = Value("not a struct");
    auto filtered = SelectSupportedNamedArgs(scalar, {"X"}, {"X"}, nullptr);
    REQUIRE(filtered.ToString() == "not a struct");
}
