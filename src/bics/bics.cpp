#include "duckdb.hpp"

#include "bics.hpp"
#include "duckdb_argument_helper.hpp"

namespace duckdb {

CharacteristicDefintion::CharacteristicDefintion(Value &meta_char_row) 
    : _meta_char_row(meta_char_row)
{}

// CharacteristicsDefinition --------------------------------------------

Characteristics::Characteristics(std::vector<Value> &meta_chars) 
    : _meta_chars(meta_chars) 
{}

Characteristics Characteristics::FromMeta(Value &BICS_PROV_META_TH_CHARACTERIST)
{
    auto meta_chars = ListValue::GetChildren(BICS_PROV_META_TH_CHARACTERIST);
    Characteristics characteristics(meta_chars);
    return characteristics;
}

unsigned int Characteristics::Count() const {
    return _meta_chars.size();
}


// Characteristics ------------------------------------------------------

BicsHandle::BicsHandle(std::string value) : _value(value) 
{
    if (value.length() != 4) {
        throw std::runtime_error("BicsHandle must be 4 characters long");
    }
}

std::string BicsHandle::Value() const {
    return _value;
}

bool BicsHandle::operator==(const BicsHandle& other) const {
    return _value == other._value;
}

bool BicsHandle::operator==(const std::string& other) const {
    return _value == other;
}

bool BicsHandle::Valid() const {
    return !(BicsHandle::Invalid == _value);
}

const BicsHandle BicsHandle::Invalid("0000");

// BicsHandle -----------------------------------------------------


/**
 * @brief Construct a new Bics Session object
 * A BICS sessions holds the client side state of a SAP BW connection.
 */
BicsSession::BicsSession(std::shared_ptr<RfcConnection> &connection, 
                    std::string &info_provider, 
                    std::string &data_provider) 
        : _connection(connection),
          _info_provider(info_provider),
          _data_provider(data_provider),
          _application_handle(BicsHandle::Invalid), 
          _data_provider_handle(BicsHandle::Invalid), 
          _variable_container_handle(BicsHandle::Invalid)
{ 
    Open();
}

BicsSession::BicsSession(std::shared_ptr<RfcConnection> &connection, 
                         std::string &info_provider) 
        : _connection(connection),
          _info_provider(info_provider),
          _application_handle(BicsHandle::Invalid), 
          _data_provider_handle(BicsHandle::Invalid), 
          _variable_container_handle(BicsHandle::Invalid)
{ 
    Open();
}

BicsSession::~BicsSession() 
{ 
    Close();
}


void BicsSession::Open() 
{
    _application_handle = CreateDataArea();
    if (! _application_handle.Valid()) {
        throw std::runtime_error("Failed to create data area on the BW server, giving up!");
    }

    std::tie(_data_provider_handle, _variable_container_handle) = OpenDataProvider(_application_handle, _info_provider, _data_provider);
    if (! _data_provider_handle.Valid()) {
        throw std::runtime_error("Failed to open data provider on the BW server, giving up!");
    }

    if (! _data_provider.empty() && ! _variable_container_handle.Valid()) {
        throw std::runtime_error("Failed to open variable container on the BW server, giving up!");
    }
}

BicsHandle BicsSession::CreateDataArea() 
{
    auto result_set = RfcResultSet::InvokeFunction(_connection, "BICS_CONS_CREATE_DATA_AREA");
    auto application_handle_value = result_set->GetResultValue("E_APPLICATION_HANDLE");

    return BicsHandle(application_handle_value.GetValue<string>());
}

std::tuple<BicsHandle, BicsHandle> BicsSession::OpenDataProvider(BicsHandle &application_handle, 
                                                                 std::string &info_provider,
                                                                 std::string &data_provider) 
{
    auto func_args = ArgBuilder().Add("I_APPLICATION_HANDLE", application_handle.Value())
                                 .Add("I_DATA_PROVIDER_INFO_PROVIDER", info_provider)
                                 .Add("I_DATA_PROVIDER_NAME", data_provider)
                                 .Add("I_STATE_VARIABLE_MODE", "U")
                                 .Add("I_OPTIMIZE_INIT_VERSION", 5)
                                 .BuildArgList();

    auto result_set = RfcResultSet::InvokeFunction(_connection, "BICS_PROV_OPEN", func_args);

    auto data_provider_handle = result_set->GetResultValue("E_DATA_PROVIDER_HANDLE");
    auto variable_container_handle = result_set->GetResultValue("E_VARIABLE_CONTAINER_HANDLE");

    return std::make_tuple(BicsHandle(data_provider_handle.GetValue<string>()), 
                           BicsHandle(variable_container_handle.GetValue<string>()));
}

void BicsSession::Close() 
{
    CloseDataProvider(_variable_container_handle);
    _variable_container_handle = BicsHandle::Invalid;

    CloseDataProvider(_data_provider_handle);
    _data_provider_handle = BicsHandle::Invalid;

    CloseDataProvider(_application_handle);
    _application_handle = BicsHandle::Invalid;
}

void BicsSession::CloseDataProvider(BicsHandle &handle) 
{
    auto func_args = ArgBuilder().Add("I_APPLICATION_HANDLE", handle.Value())
                                 .BuildArgList();
    auto result_set = RfcResultSet::InvokeFunction(_connection, "BICS_PROV_CLOSE", func_args);
}

bool BicsSession::IsOpen()
{
    return _application_handle.Valid() 
        && _data_provider_handle.Valid() 
        && _variable_container_handle.Valid();
}

std::string BicsSession::GetApplicationHandle() const {
    return _application_handle.Value();
}

std::string BicsSession::GetDataProviderHandle() const {
    return _data_provider_handle.Value();
}

std::string BicsSession::GetVariableContainerHandle() const {
    return _variable_container_handle.Value();
}

// BicsSession ----------------------------------------------------


} // namespace duckdb