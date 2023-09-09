#pragma once

#include "duckdb.hpp"
#include "sapnwrfc.h"

#include "sap_connection.hpp"
#include "sap_type_conversion.hpp"

namespace duckdb {

    class IConvertToSqlLiteral
    {
        public:
            virtual std::string ToSqlLiteral() = 0;
    };

    class RfcType; // forward declaration
    class RfcInvocation; // forward declaration
    class RfcFunction; // forward declaration
    class RfcResultSet; // forward declaration

    class RfcFieldDesc : public IConvertToSqlLiteral {
        public:
            RfcFieldDesc(const RFC_FIELD_DESC& handle);
            virtual ~RfcFieldDesc();

            std::string GetName() const;
            std::shared_ptr<RfcType> GetRfcType() const;
            std::string GetRfcTypeAsString() const;
            unsigned int GetLength() const;
            unsigned int GetDecimals() const;

            virtual std::string ToSqlLiteral();

        private:
            RFC_FIELD_DESC _desc_handle;
            std::shared_ptr<RfcType> _type;
    };

    /**
     * @brief A wrapper class for the RFC_TYPE_DESC_HANDLE structure.
     * 
     * This class wraps the RFC_TYPE_DESC_HANDLE structure used in the SAP NWRFC SDK.
     * It manages the lifetime of the typeDescHandle and provides convenient
     * member functions for accessing the type description data.
     */
    class RfcType : public IConvertToSqlLiteral
    {
        public:
            RfcType(const RFCTYPE rfc_type, const RFC_TYPE_DESC_HANDLE handle) : RfcType(rfc_type, handle, 0, 0) {};
            RfcType(const RFCTYPE rfc_type, const RFC_TYPE_DESC_HANDLE handle, const unsigned int length, const unsigned int decimals);
            virtual ~RfcType();

            bool IsComplexType() const;
            std::string GetName() const;
            unsigned int GetLength() const;
            unsigned int GetDecimals() const;
            std::vector<RfcFieldDesc> GetFieldInfos();
            RfcFieldDesc GetFieldInfo(std::string name);
            RfcFieldDesc GetFieldInfo(unsigned int index);
            LogicalTypeId GetCompatibleDuckDbType();
            LogicalType CreateDuckDbType();
            bool IsCompatibleType(const LogicalTypeId type);
            void AdaptValue(RfcInvocation &invocation, string &arg_name, Value &arg_value);
            void AdaptValue(DATA_CONTAINER_HANDLE &container_handle, string &arg_name, Value &arg_value);
            Value ConvertRfcValue(RfcInvocation &invocation, string &field_name);
            Value ConvertRfcValue(shared_ptr<RfcInvocation> invocation, string &field_name);
            Value ConvertCsvValue(Value &csv_value);
            
            virtual std::string ToSqlLiteral();

        private:
            RFCTYPE _rfc_type;
            RFC_TYPE_DESC_HANDLE _handle;
            unsigned int _length;
            unsigned int _decimals;
            std::vector<RfcFieldDesc> _field_infos;

            child_list_t<LogicalType> CreateDuckDbTypesForRfcStruct();
            LogicalType CreateDuckDbTypeForRfcTable();
            
            void AdaptStructure(RFC_STRUCTURE_HANDLE &structure_handle, string &arg_name, Value &arg_value);
            void AdaptTable(RFC_TABLE_HANDLE &table_handle, string &arg_name, Value &arg_value);

            Value ConvertRfcValue(RFC_FUNCTION_HANDLE &function_handle, string &field_name);
            Value ConvertRfcStruct(RFC_STRUCTURE_HANDLE &struct_handle);
            Value ConvertRfcTable(RFC_TABLE_HANDLE &table_handle);

        public:
            static RfcType FromTypeName(const std::string &type_name);
            static RfcType FromTypeName(const std::string &type_name, const unsigned int length, const unsigned int decimals);
    };


    /**
     * @brief A wrapper class for the RFC_PARAMETER_DESC structure.
     * 
     * This class wraps the RFC_PARAMETER_DESC structure used in the SAP NWRFC SDK.
     * It manages the lifetime of the typeDescHandle and provides convenient
    * member functions for accessing the parameter description data.
    */
    class RfcFunctionParameterDesc
    {
        public:
            RfcFunctionParameterDesc(const RFC_PARAMETER_DESC& sap_desc);
            ~RfcFunctionParameterDesc();

            std::string GetName() const;
            std::shared_ptr<RfcType> GetRfcType() const;
            RFCTYPE GetRfcTypeAsEnum() const;
            std::string GetRfcTypeAsString() const;
            RFC_DIRECTION GetDirection() const;
            std::string GetDirectionAsString() const;
            std::string GetDefaultValue() const;
            std::string GetDescription() const;
            unsigned int GetLength() const;
            unsigned int GetDecimals() const;
            bool IsOptional() const;

        private:
            RFC_PARAMETER_DESC _desc_handle;
            std::shared_ptr<RfcType> _type;
    };

    /**
     * @brief A wrapper class for the RFC_FUNCTION_DESC_HANDLE structure.
    */
    class RfcFunction : public std::enable_shared_from_this<RfcFunction>
    {
        public:
            RfcFunction(std::shared_ptr<RfcConnection> &connection, std::string function_name);
            ~RfcFunction() noexcept(false);

            RFC_FUNCTION_DESC_HANDLE GetDescriptionHandle() const;

            std::string GetName();
            std::vector<RfcFunctionParameterDesc> GetParameterInfos();
            RfcFunctionParameterDesc GetParameterInfo(std::string name);
            std::vector<LogicalType> GetResultTypes();
            std::vector<string> GetResultNames();
            std::vector<RfcFunctionParameterDesc> GetResultInfos();
            RfcFunctionParameterDesc GetResultInfo(std::string name);
            RfcFunctionParameterDesc GetResultInfo(unsigned int col_idx);

            std::shared_ptr<RfcConnection> GetConnection() const;
            std::shared_ptr<RfcInvocation> BeginInvocation(std::vector<Value> &arguments);
             std::shared_ptr<RfcInvocation> BeginInvocation();

        private:
            std::string _function_name;
            std::vector<RfcFunctionParameterDesc> _param_infos;
            std::shared_ptr<RfcConnection> _connection;
            RFC_FUNCTION_DESC_HANDLE _desc_handle;

    };

    
    class RfcInvocationAdapter {
        public:
            RfcInvocationAdapter(RfcInvocation &invocation);
            virtual ~RfcInvocationAdapter();
            virtual void Adapt(std::vector<RfcFunctionParameterDesc> parameter_infos, std::vector<Value> &arguments) = 0;

        protected:
            RfcInvocation &_invocation;
    };

    class RfcInvocationPositionalAdapter : public RfcInvocationAdapter {
        public:
            RfcInvocationPositionalAdapter(RfcInvocation &invocation);
            void Adapt(std::vector<RfcFunctionParameterDesc> parameter_infos, std::vector<Value> &arguments) override;
    };

    class RfcInvocationNamedAdapter : public RfcInvocationAdapter {
        public:
            RfcInvocationNamedAdapter(RfcInvocation &invocation);
            void Adapt(std::vector<RfcFunctionParameterDesc> parameter_infos, std::vector<Value> &arguments) override;

        private:
            void AdaptChildValue(std::vector<RfcFunctionParameterDesc> parameter_infos, std::string &arg_name, Value &arg_value);
    };


    /** 
     * @brief A class taking a `RfcFunction` and adapting a vector of duckdb as its arguments. Furtheron helping to stream results.
    */
    class RfcInvocation : public std::enable_shared_from_this<RfcInvocation> 
    {
        public:
            RfcInvocation(std::shared_ptr<RfcFunction> function, std::vector<Value> &arguments);
            ~RfcInvocation() noexcept(false);

            std::shared_ptr<RfcFunction> GetFunction() const;
            std::shared_ptr<RfcConnection> GetConnection() const;
            std::shared_ptr<RfcResultSet> Invoke(std::string path);
            std::shared_ptr<RfcResultSet> Invoke();

            RFC_FUNCTION_HANDLE GetFunctionHandle() const;
            DATA_CONTAINER_HANDLE GetDataContainerHandle() const;

        private:
            bool IsAdaptByPosition(std::vector<Value> &arguments);
            RFC_FUNCTION_HANDLE CreateRfcFunctionHandle(std::shared_ptr<RfcFunction> function);
            std::unique_ptr<RfcInvocationAdapter> CreateRfcInvocationAdapter(std::vector<Value> &arguments);

            std::shared_ptr<RfcFunction> _function;
            std::vector<Value> _arguments;
            RFC_FUNCTION_HANDLE _handle;
    };


    /**
     * @brief A class taking a `RfcInvocation` and streaming its results back.
    */
    class RfcResultSet {
        public: 
            RfcResultSet(std::shared_ptr<RfcInvocation> invocation, std::string path);

            std::shared_ptr<RfcInvocation> GetInvocation() const;
            std::shared_ptr<RfcFunction> GetFunction() const;

            std::vector<LogicalType> GetResultTypes();
            std::vector<string> GetResultNames();

            unsigned int FetchNextResult(DataChunk &output);
            bool HasMoreResults();
            Value GetResultValue(unsigned int col_idx);
            Value GetResultValue(std::string col_name);

            unsigned int TotalRows();

            static std::shared_ptr<RfcResultSet> InvokeFunction(
                std::shared_ptr<RfcConnection> connection,
                std::string function_name,
                std::vector<Value> &function_arguments,
                std::string result_path
            );

            static std::shared_ptr<RfcResultSet> InvokeFunction(
                std::shared_ptr<RfcConnection> connection,
                std::string function_name,
                std::vector<Value> &function_arguments
            );

            static std::shared_ptr<RfcResultSet> InvokeFunction(
                std::shared_ptr<RfcConnection> connection,
                std::string function_name
            );

        private:
            std::shared_ptr<RfcInvocation> _invocation;
            std::string _path;
            std::vector<std::string> _result_names;
            std::vector<LogicalType> _result_types;
            std::vector<Value> _result_data;
            unsigned int _total_rows;
            unsigned int _current_row_idx;
            
            std::pair<std::vector<std::string>, std::vector<LogicalType>> InferResultSchema(std::string path);
            std::vector<LogicalType> ExtractResultTypes(std::vector<RfcFunctionParameterDesc> &param_infos);
            std::vector<LogicalType> ExtractResultTypes(std::vector<RfcFieldDesc> &field_infos);
            std::vector<std::string> ExtractResultNames(std::vector<RfcFunctionParameterDesc> &param_infos);
            std::vector<std::string> ExtractResultNames(std::vector<RfcFieldDesc> &field_infos);

            std::vector<Value> ConvertValuesAndSelectPath(std::string path);
            std::vector<Value> ConvertResultValues(std::vector<RfcFunctionParameterDesc> &param_infos);
            Value ConvertSingleResultValue(RfcFunctionParameterDesc &param_info);
            std::vector<Value> ConvertValuesFromStruct(Value &container_value, std::vector<std::string> tokens);
            std::vector<Value> ConvertValuesFromList(Value &container_value, std::vector<std::string> tokens);

            bool IsTabularResult();

    };

    struct RfcFunctionBindData : public TableFunctionData
    {
        std::shared_ptr<RfcInvocation> invocation;
        std::shared_ptr<RfcResultSet> result_set;

        std::vector<std::string> GetResultNames();
        std::vector<LogicalType> GetResultTypes();
    };


} // namespace duckdb