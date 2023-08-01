#pragma once

#ifndef BICS_HPP
#define BICS_HPP

#include "duckdb.hpp"
#include "sap_function.hpp"
#include "duckdb_argument_helper.hpp"

namespace duckdb
{
    class Dimension;      // forward declaration
    class Characteristic; // forward declaration
    class KeyFigure;      // forward declaration
    class BicsHandle;     // forward declaration

    /*
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

    class SingleFilter
    {

    };


    class RangeFilter
    {

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


    class Dimension
    {
        public:
            Dimension(std::string name, std::shared_ptr<Value> BICS_PROV_META_QUERY);

            std::string Name() const;
            std::string Text() const;
            std::vector<Characteristic> Characteristics() const;

            ValueHelper MetaHelper() const;

        private:
            std::string _name;
            std::shared_ptr<Value> _meta_data;
    };

    class Characteristic 
    {
        friend class KeyFigure;

        public:
            Characteristic(std::string _name, std::shared_ptr<Value> BICS_PROV_META_QUERY);

            std::string Name() const;
            std::string Text() const;
            int Id() const;
            bool HasBaseCharacteristic() const;
            Characteristic BaseCharacteristic() const;
            Dimension Dim() const;

            ValueHelper ChHelper() const;
            ValueHelper MetaHelper() const;
        private:
            std::unique_ptr<Value> FindChValue(std::string _name,  std::shared_ptr<Value> BICS_PROV_META_QUERY) const;
            
            std::string _name;
            std::shared_ptr<Value> _meta_data;
            std::unique_ptr<Value> _ch_meta;
    };

    class KeyFigure
    {
        public:
            KeyFigure(std::string _parent_ch_name, std::string _name, std::shared_ptr<Value>);

            std::string Name() const;
            std::string AlternativeName() const;
            std::string Text() const;
            int MemberPosition() const;
            Characteristic ParentCharacteristic() const;
            ValueHelper KfHelper() const;
        private:
            ValueHelper MetaHelper() const;
            std::unique_ptr<Value> FindKfValue(std::string name) const;

            std::string _parent_ch_name;
            std::string _name;
            std::shared_ptr<Value> _meta_data;
            std::unique_ptr<Value> _kf_meta;
    };

    enum class QueryPropertyField {
        NAME,
        TEXT,
        INFOCUBE
    };

    class InfoProvider
    {
        friend class Dimension;
        friend class KeyFigure;

    public:
        InfoProvider(std::shared_ptr<Value> BICS_PROV_META_DATA);
        // QUERY_PROPERTIES::BICS_PROV_META_QUERY
        std::string Name() const;     // QUERY_PROPERTIES/INFOPROVIDER_NAME or QUERY_PROPERTIES/QUERY_NAME
        std::string Text() const;     // QUERY_PROPERTIES/INFOPROVIDER_TEXT or QUERY_PROPERTIES/QUERY_TEXT
        std::string InfoCube() const; // QUERY_PROPERTIES/INFOCUBE

        std::vector<Dimension> Dimensions() const;
        std::vector<KeyFigure> KeyFigures() const;
        std::vector<Characteristic> Characteristics() const;

        ValueHelper MetaHelper() const;

    private:
        std::shared_ptr<Value> _meta_data;

        template<typename T>
        T GetFieldFromQueryProperties(QueryPropertyField field) const;
    };

    class BicsHandle
    {
    public:
        static const BicsHandle Invalid;

        BicsHandle(std::string value);

        std::string Value() const;
        bool operator==(const BicsHandle &other) const;
        bool operator==(const std::string &other) const;
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
        void CloseDataProvider(BicsHandle &handle);                                      // Essentially calling BICS_PROV_CLOSE
    };

} // namespace duckdb

#endif // BICS_HPP