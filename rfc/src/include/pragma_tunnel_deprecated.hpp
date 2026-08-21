#pragma once

// Deprecation stubs for the SSH tunnel functions that used to ship inside erpl.
//
// The tunnel moved to the dedicated erpl_tunnel extension
// (https://github.com/DataZooDE/erpl-tunnel), which does strictly more: reverse tunnels,
// Tailscale and NetBird backends, peer discovery. erpl no longer bundles a tunnel of its
// own. See TUNNEL_REMOVAL_PLAN.md.
//
// These stubs exist so that a call to a moved function says where it went, rather than
// failing with DuckDB's generic "Function does not exist".
//
// They live in erpl_rfc rather than in a surviving erpl_tunnel extension, and that is
// load-bearing. DuckDB resolves LOAD by extension name: as long as anything erpl ships is
// called erpl_tunnel, a user's `LOAD erpl_tunnel` is a silent no-op and the real extension
// never loads. Freeing the name is the point of the exercise.

#include "duckdb.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {

// Registers a stub for every tunnel function erpl used to provide -- but only for names
// that are not already registered, so loading erpl after erpl_tunnel cannot displace the
// real implementation.
void RegisterDeprecatedTunnelFunctions(ExtensionLoader &loader);

} // namespace duckdb
