#pragma once

#include "graph.hpp"
#include <functional>
#include <vector>
#include <string>

namespace pioneer {

// Callback for streaming path output
// Returns false to stop searching, true to continue
using PathCallback = std::function<bool(const std::vector<std::string>& path)>;

// Query engine for finding paths in the call graph
class QueryEngine {
public:
    explicit QueryEngine(const Graph& graph);
    
    // Find all paths from start to end symbol
    // Paths are streamed via callback as they are found
    // If start == "START", performs backtrace from end
    // If end == "END", finds all forward paths to leaf nodes
    void find_paths(const std::string& start, 
                    const std::string& end,
                    PathCallback callback);
    
    // Find all callers of a symbol (backtrace)
    void backtrace(const std::string& symbol, PathCallback callback);
    
    // Find all callees reachable from a symbol (forward trace)
    void forward_trace(const std::string& symbol, PathCallback callback);
    
    // Check if symbol exists
    bool has_symbol(const std::string& name) const;
    
    // Get all symbols matching a pattern (simple substring match)
    std::vector<std::string> find_symbols(const std::string& pattern) const;
    
    // Print path in human-readable format
    static void print_path(const std::vector<std::string>& path);
    
private:
    const Graph& graph_;
    
    // Iterative DFS for forward paths (avoid recursion)
    void dfs_forward(SymbolUID start, SymbolUID end, PathCallback& callback);
    
    // Iterative DFS for backward paths (backtrace)
    void dfs_backward(SymbolUID start, SymbolUID end, PathCallback& callback);
};

} // namespace pioneer
