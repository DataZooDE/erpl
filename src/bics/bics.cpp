#include "duckdb.hpp"

#include "bics.hpp"

namespace duckdb {

/*
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
*/

Dimension::Dimension(std::string name,  std::shared_ptr<Value> BICS_PROV_META_DATA) 
    : _name(name), _meta_data(BICS_PROV_META_DATA)
{}

ValueHelper Dimension::MetaHelper() const {
    return ValueHelper(*_meta_data);
}

std::string Dimension::Name() const {
    return _name;
}

std::string Dimension::Text() const {
    auto dim_list = ListValue::GetChildren(MetaHelper()["/DIMENSIONS"]);
    for (auto &dim : dim_list) {
        auto dim_helper = ValueHelper(dim);
        if (dim_helper["/NAME"] == _name) {
            return dim_helper["/TEXT"].ToString();
        }
    }
   
    throw std::runtime_error("Dimension not found");
}

std::vector<Characteristic> Dimension::Characteristics() const 
{
    std::vector<Characteristic> ret;

    auto ch_list = ListValue::GetChildren(MetaHelper()["/CHARACTERISTICS"]);
    for (auto &ch : ch_list) {
        auto ch_helper = ValueHelper(ch);
        auto dim_name = ch_helper["/DIMENSION"].ToString();
        if (dim_name == _name) {
            ret.push_back(Characteristic(ch_helper["/NAME"].ToString(), _meta_data));
        }
    }
    return ret;
}

// Dimension ------------------------------------------------------------

Characteristic::Characteristic(std::string name,  std::shared_ptr<Value> BICS_PROV_META_DATA) 
    : _name(name), _meta_data(BICS_PROV_META_DATA), _ch_meta(nullptr)
{
    _ch_meta = FindChValue(name, BICS_PROV_META_DATA);
}

 std::unique_ptr<Value> Characteristic::FindChValue(std::string name, std::shared_ptr<Value> BICS_PROV_META_DATA) const {
    auto ch_list = ListValue::GetChildren(MetaHelper()["/CHARACTERISTICS"]);
    for (auto &ch : ch_list) {
        auto ch_helper = ValueHelper(ch);
        if (ch_helper["/NAME"] == name) {
            return make_uniq<Value>(ch);
        }
    }
    throw std::runtime_error("Characteristic not found");
}

ValueHelper Characteristic::MetaHelper() const {
    return ValueHelper(*_meta_data);
}

ValueHelper Characteristic::ChHelper() const{
    return ValueHelper(*_ch_meta);
} 

std::string Characteristic::Name() const {
    return _name;
}

std::string Characteristic::Text() const {
    return ChHelper()["/TEXT"].ToString();
}

int Characteristic::Id() const {
    return ChHelper()["/ID"].GetValue<int>();
}

bool Characteristic::HasBaseCharacteristic() const {
    auto base_ch_name = ChHelper()["/BASE_CHARACTERISTIC"].ToString();
    return base_ch_name != Name();
}

Characteristic Characteristic::BaseCharacteristic() const {
    auto base_ch_name = ChHelper()["/BASE_CHARACTERISTIC"].ToString();
    return Characteristic(base_ch_name, _meta_data);
}

Dimension Characteristic::Dim() const {
    auto dim_name = ChHelper()["/DIMENSION"].ToString();
    return Dimension(dim_name, _meta_data);
}

// Characteristic -------------------------------------------------------

KeyFigure::KeyFigure(std::string _parent_ch_name, std::string name, std::shared_ptr<Value> BICS_PROV_META_DATA) 
    : _parent_ch_name(_parent_ch_name), _name(name), _meta_data(BICS_PROV_META_DATA), _kf_meta(nullptr)
{
    _kf_meta = FindKfValue(name);
}

std::unique_ptr<Value> KeyFigure::FindKfValue(std::string name) const {
    auto parent_ch = ParentCharacteristic();
    auto ch_meta = parent_ch.ChHelper();

    if (! ValueHelper::IsX(ch_meta["/IS_STRUCTURE"])) {
        throw std::runtime_error("Parent characteristic is not a structure");
    }
    
    auto struct_list = ListValue::GetChildren(ch_meta["/STRUCTURE_MEMBERS"]);
    for (auto &struct_member : struct_list) {
        auto struct_helper = ValueHelper(struct_member);
        if (struct_helper["/NAME"] == _name) {
            return make_uniq<Value>(struct_member);
        }
    }
    throw std::runtime_error(StringUtil::Format("Current Key Figure with name '%s' not found", _name));
}

ValueHelper KeyFigure::MetaHelper() const 
{
    return ValueHelper(*_meta_data);
}

ValueHelper KeyFigure::KfHelper() const {
    return ValueHelper(*_kf_meta);
}

std::string KeyFigure::Name() const 
{
    return _name;
}

Characteristic KeyFigure::ParentCharacteristic() const {
    Characteristic parent_ch(_parent_ch_name, _meta_data);
    return parent_ch;
}

std::string KeyFigure::AlternativeName() const 
{
    auto kf_helper = KfHelper();
    return kf_helper["/ALTERNATIVE_NAME"].ToString();
}

std::string KeyFigure::Text() const {
    auto kf_helper = KfHelper();
    return kf_helper["/TEXT"].ToString();
}

// KeyFigure ------------------------------------------------------------

InfoProvider::InfoProvider(std::shared_ptr<Value> BICS_PROV_META_DATA) 
    : _meta_data(BICS_PROV_META_DATA)
{}

std::string InfoProvider::Name() const {
    return GetFieldFromQueryProperties<std::string>(QueryPropertyField::NAME);
}

std::string InfoProvider::Text() const {
    return GetFieldFromQueryProperties<std::string>(QueryPropertyField::TEXT);
}

std::string InfoProvider::InfoCube() const {
    return GetFieldFromQueryProperties<std::string>(QueryPropertyField::INFOCUBE);
}

ValueHelper InfoProvider::MetaHelper() const {
    return ValueHelper(*_meta_data);
}

template<typename T>
T InfoProvider::GetFieldFromQueryProperties(QueryPropertyField field) const
{
    switch(field) {
        case QueryPropertyField::NAME:
            return MetaHelper()["/QUERY_PROPERTIES/QUERY_NAME"].GetValue<T>();
        case QueryPropertyField::TEXT:
            return MetaHelper()["/QUERY_PROPERTIES/QUERY_TEXT"].GetValue<T>();
        case QueryPropertyField::INFOCUBE:
            return MetaHelper()["/QUERY_PROPERTIES/INFOCUBE"].GetValue<T>();
    }
}

std::vector<Dimension> InfoProvider::Dimensions() const 
{
    auto dim_list = ListValue::GetChildren(MetaHelper()["/DIMENSIONS"]);
    std::vector<Dimension> dimensions;

    for (auto &dim : dim_list) {
        auto name = ValueHelper(dim)["/NAME"].ToString();
        dimensions.push_back(Dimension(name, _meta_data));
    }

    return dimensions;
}

std::vector<KeyFigure> InfoProvider::KeyFigures() const 
{
    auto char_list = ListValue::GetChildren(MetaHelper()["/CHARACTERISTICS"]);
    std::vector<KeyFigure> key_figures;

    for (auto &ch : char_list) {
        auto ch_helper = ValueHelper(ch);
        if (!ValueHelper::IsX(ch_helper["/CONTAINS_KEYFIGURES"]) || !ValueHelper::IsX(ch_helper["/IS_STRUCTURE"])) {
            continue;
        }
        auto parent_ch_name = ch_helper["/NAME"].ToString();
        auto struct_members = ListValue::GetChildren(ch_helper["/STRUCTURE_MEMBERS"]);
        for (auto &struct_member : struct_members) {
            auto kf_name = ValueHelper(struct_member)["/NAME"].ToString();
            key_figures.push_back(KeyFigure(parent_ch_name, kf_name, _meta_data));
        }
    }

    return key_figures;
}

std::vector<Characteristic> InfoProvider::Characteristics() const 
{
    auto char_list = ListValue::GetChildren(MetaHelper()["/CHARACTERISTICS"]);
    std::vector<Characteristic> ret;

    for (auto &ch : char_list) {
        auto ch_helper = ValueHelper(ch);
        if (ValueHelper::IsX(ch_helper["/CONTAINS_KEYFIGURES"])) {
            continue;
        }

        auto ch_name = ch_helper["/NAME"].ToString();
        ret.push_back(Characteristic(ch_name, _meta_data));
    }

    return ret;
}

// InfoProvider --------------------------------------------------------

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