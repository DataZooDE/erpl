#include "duckdb.hpp"
#include "sap_function.hpp"

namespace duckdb {

class Dimension; // forward declaration
class BicsHandle; // forward declaration

/*
class SingleFilter 
{

};


class RangeFilter 
{

};


class Dimension 
{
    private:
        std::string _technical_name;
        std::string _short_name;
        std::string _caption;
        std::string _description;
        int _id;
        bool _is_key_figure;
        bool _has_hierarchy;
        bool _supports_hierarchy;
        //Dimension _parent;

        std::vector<SingleFilter> _single_value_filters;
        std::vector<RangeFilter> _range_filters;
};

class UnitColumn 
{

};

class Measure 
{
    private:
        std::string _technical_name;
        std::string short_name;
        std::string description;
        std::string caption;
        RfcType _rfc_type;
        UnitColumn _unit_column;
};

class Variable 
{

};

class InfoproviderDefintion 
{

    private:
        std::string _technical_name;
        std::string _caption;
        std::string _description;
        std::vector<Dimension> _dimensions;
        std::vector<Measure> _measures;
        std::vector<Variable> _variables;
};

*/

class CharacteristicDefintion 
{
    public:
        CharacteristicDefintion(Value &meta_char_row);

    private:
        Value &_meta_char_row;
};

class Characteristics 
{
    public:
        static Characteristics FromMeta(Value &BICS_PROV_META_TH_CHARACTERIST);

        unsigned int Count() const;
        vector<CharacteristicDefintion> GetCharacteristics() const;

        std::vector<LogicalType> GetMetaResultTypes();
        std::vector<string> GetMetaResultNames();

    private:
        Characteristics(std::vector<Value> &meta_chars);
        
        std::vector<Value> &_meta_chars;
};

class BicsHandle 
{
    public:
        static const BicsHandle Invalid;

        BicsHandle(std::string value);

        std::string Value() const;
        bool operator==(const BicsHandle& other) const; 
        bool operator==(const std::string& other) const;
        bool Valid() const; 

    private:
        std::string _value;
};

class BicsSession
{
    public:
        BicsSession(std::shared_ptr<RfcConnection> &connection, 
                    std::string &info_provider, 
                    std::string &data_provider);
            
        BicsSession(std::shared_ptr<RfcConnection> &connection, 
                    std::string &info_provider);


        ~BicsSession();

        void Open();
        void Close();
        bool IsOpen();

        std::string GetApplicationHandle() const;
        std::string GetDataProviderHandle() const;
        std::string GetVariableContainerHandle() const;

    private:

    std::shared_ptr<RfcConnection> _connection;
    std::string _info_provider;
    std::string _data_provider;

    BicsHandle _application_handle;
    BicsHandle _data_provider_handle;
    BicsHandle _variable_container_handle;

    BicsHandle CreateDataArea(); // Essentially calling BICS_CONS_CREATE_DATA_AREA
    std::tuple<BicsHandle, BicsHandle> OpenDataProvider(BicsHandle &application_handle, 
                                                        std::string &info_provider,
                                                        std::string &data_provider); // Essentially calling BICS_PROV_OPEN
    void CloseDataProvider(BicsHandle &handle); // Essentially calling BICS_PROV_CLOSE
};

} // namespace duckdb