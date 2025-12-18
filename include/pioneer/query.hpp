#pragma once

#include "graph.hpp"
#include <functional>
#include <string>
#include <vector>

namespace pioneer {

// Callback for streaming path output
// Returns false to stop searching, true to continue
using PathCallback = std::function<bool(const std::vector<std::string> &path)>;

// Query engine for finding paths in the call graph
class QueryEngine {
public:

    explicit QueryEngine(const Graph &graph);

    // Find all paths from start to end symbol
    // Paths are streamed via callback as they are found
    // If start == "START", performs backtrace from end
    // If end == "END", finds all forward paths to leaf nodes
    void find_paths(const std::string &start, const std::string &end, PathCallback callback);

    // Find all callers of a symbol (backtrace)
    void backtrace(const std::string &symbol, PathCallback callback);

    // Find all callees reachable from a symbol (forward trace)
    void forward_trace(const std::string &symbol, PathCallback callback);

    // Check if symbol exists
    bool has_symbol(const std::string &name) const;

    // Get all symbols matching a pattern (simple substring match)
    std::vector<std::string> find_symbols(const std::vector<std::string> &patterns) const;
    std::vector<std::string> find_symbols(const std::string &pattern) const;

    // Print path in human-readable format
    static void print_path(const std::vector<std::string> &path);

    // ============ Data Flow Queries (v1.1.0) ============

    // Find what a variable is assigned from (data sources)
    std::vector<std::string> data_sources(const std::string &variable) const;

    // Find what variables a function's return value flows to
    std::vector<std::string> data_sinks(const std::string &source) const;

    // Find all variables in a function
    std::vector<std::string> variables_in(const std::string &func_pattern) const;

    // Trace data flow path from source to variable
    void find_data_flow_paths(const std::string &source, const std::string &variable,
                              PathCallback callback);

private:

    const Graph &graph_;

    // Iterative DFS for forward paths (avoid recursion)
    void dfs_forward(SymbolUID start, SymbolUID end, PathCallback &callback);

    // Iterative DFS for backward paths (backtrace)
    void dfs_backward(SymbolUID start, SymbolUID end, PathCallback &callback);

    // DFS for data flow paths
    void dfs_data_flow(SymbolUID source, SymbolUID target, PathCallback &callback);
};

} // namespace pioneer
