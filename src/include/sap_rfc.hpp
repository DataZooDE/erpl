#pragma once

#include "duckdb.hpp"
#include "sapnwrfc.h"

#include "sap_function.hpp"

namespace duckdb 
{
	
	
	string RfcFunctionDesc(ClientContext &context, const FunctionParameters &parameters);

	

} // namespace duckdb