#pragma once

#include "duckdb.hpp"
#include "duckdb/function/table_function.hpp"
#include "tunnel_connection.hpp"
#include <vector>
#include <utility>

namespace duckdb {

class TunnelsBindData : public FunctionData {
public:
    TunnelsBindData(std::vector<std::pair<int64_t, TunnelConnectionAttributes>> tunnels) 
        : tunnels(std::move(tunnels)), current_row(0) {}

    unique_ptr<FunctionData> Copy() const override {
        return make_uniq<TunnelsBindData>(tunnels);
    }

    bool Equals(const FunctionData &other_p) const override {
        auto &other = other_p.Cast<TunnelsBindData>();
        return tunnels == other.tunnels;
    }

    bool HasMoreResults() const {
        return current_row < tunnels.size();
    }

    void FetchNextResult(DataChunk &output) {
        if (!HasMoreResults()) {
            return;
        }

        // Set the cardinality to 1 (one row at a time)
        output.SetCardinality(1);

        // Set the values for the current row
        const auto &tunnel_info = tunnels[current_row];
        output.SetValue(0, 0, Value::BIGINT(tunnel_info.first));                    // tunnel_id
        output.SetValue(1, 0, Value(tunnel_info.second.ssh_host));                  // ssh_host
        output.SetValue(2, 0, Value::INTEGER(tunnel_info.second.ssh_port));         // ssh_port
        output.SetValue(3, 0, Value(tunnel_info.second.ssh_user));                  // ssh_user
        output.SetValue(4, 0, Value(tunnel_info.second.remote_host));               // remote_host
        output.SetValue(5, 0, Value::INTEGER(tunnel_info.second.remote_port));      // remote_port
        output.SetValue(6, 0, Value::INTEGER(tunnel_info.second.local_port));       // local_port
        output.SetValue(7, 0, Value(tunnel_info.second.status));                    // status

        current_row++;
    }

private:
    std::vector<std::pair<int64_t, TunnelConnectionAttributes>> tunnels;
    idx_t current_row;
};

void ListTunnelsFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &output);
unique_ptr<FunctionData> ListTunnelsBind(ClientContext &context, TableFunctionBindInput &input, vector<LogicalType> &return_types, vector<string> &names);
TableFunction CreateTunnelsTableFunction();

} // namespace duckdb 