#include "duckdb.hpp"
#include "sap_function.hpp"

namespace bics {

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
        Dimension _parent;

        std::vector<SingleFilter> _single_value_filters;
        std::vector<RangeFilter> _range_filters;
};

class UnitColumn 
{

};

class Measure 
{
    private:
        std::string _technical_name,
        std::string short_name,
        std::string description,
        std::string caption,
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
}

} // namespace duckdb