#include "pioneer/query.hpp"
#include <iostream>
#include <stack>
#include <unordered_set>
#include <algorithm>

namespace pioneer {

QueryEngine::QueryEngine(const Graph& graph) : graph_(graph) {}

bool QueryEngine::has_symbol(const std::string& name) const {
    return graph_.has_symbol(name);
}

std::vector<std::string> QueryEngine::find_symbols(const std::string& pattern) const {
    std::vector<std::string> matches;
    
    for (const auto& symbol : graph_.get_all_symbols()) {
        if (symbol.find(pattern) != std::string::npos) {
            matches.push_back(symbol);
        }
    }
    
    std::sort(matches.begin(), matches.end());
    return matches;
}

void QueryEngine::print_path(const std::vector<std::string>& path) {
    for (size_t i = 0; i < path.size(); ++i) {
        std::cout << path[i];
        if (i < path.size() - 1) {
            std::cout << " -> ";
        }
    }
    std::cout << std::endl;
}

void QueryEngine::find_paths(const std::string& start,
                              const std::string& end,
                              PathCallback callback) {
    // Handle special START keyword (backtrace mode)
    if (start == "START") {
        backtrace(end, callback);
        return;
    }
    
    // Handle special END keyword (forward trace to all leaves)
    if (end == "END") {
        forward_trace(start, callback);
        return;
    }
    
    // Regular path finding
    SymbolUID start_uid = graph_.get_uid(start);
    SymbolUID end_uid = graph_.get_uid(end);
    
    if (start_uid == INVALID_UID) {
        std::cerr << "Error: Symbol not found: " << start << std::endl;
        return;
    }
    
    if (end_uid == INVALID_UID) {
        std::cerr << "Error: Symbol not found: " << end << std::endl;
        return;
    }
    
    dfs_forward(start_uid, end_uid, callback);
}

void QueryEngine::backtrace(const std::string& symbol, PathCallback callback) {
    SymbolUID target_uid = graph_.get_uid(symbol);
    
    if (target_uid == INVALID_UID) {
        std::cerr << "Error: Symbol not found: " << symbol << std::endl;
        return;
    }
    
    // Find all paths that lead TO this symbol
    // We traverse the reverse call graph
    dfs_backward(target_uid, INVALID_UID, callback);
}

void QueryEngine::forward_trace(const std::string& symbol, PathCallback callback) {
    SymbolUID start_uid = graph_.get_uid(symbol);
    
    if (start_uid == INVALID_UID) {
        std::cerr << "Error: Symbol not found: " << symbol << std::endl;
        return;
    }
    
    // Find all paths from this symbol to END
    SymbolUID end_uid = graph_.end_uid();
    dfs_forward(start_uid, end_uid, callback);
}

// Iterative DFS for forward path finding (caller -> callee direction)
void QueryEngine::dfs_forward(SymbolUID start, SymbolUID end, PathCallback& callback) {
    // State for iterative DFS
    struct State {
        SymbolUID node;
        size_t callee_index;  // Which callee to explore next
        std::vector<SymbolUID> callees;  // Cached callees
    };
    
    std::stack<State> stack;
    std::vector<SymbolUID> current_path;
    std::unordered_set<SymbolUID> in_path;  // For cycle detection
    
    // Initialize
    State initial;
    initial.node = start;
    initial.callee_index = 0;
    const auto& start_callees = graph_.get_callees(start);
    initial.callees.assign(start_callees.begin(), start_callees.end());
    
    stack.push(initial);
    current_path.push_back(start);
    in_path.insert(start);
    
    while (!stack.empty()) {
        State& state = stack.top();
        
        // Check if we've reached the target
        if (state.node == end) {
            // Convert path to strings and invoke callback
            std::vector<std::string> path_str;
            path_str.reserve(current_path.size());
            for (SymbolUID uid : current_path) {
                path_str.push_back(graph_.get_symbol(uid));
            }
            
            if (!callback(path_str)) {
                return;  // Callback requested stop
            }
            
            // Backtrack
            in_path.erase(state.node);
            current_path.pop_back();
            stack.pop();
            continue;
        }
        
        // Find next callee to explore
        bool found_next = false;
        while (state.callee_index < state.callees.size()) {
            SymbolUID callee = state.callees[state.callee_index];
            state.callee_index++;
            
            // Skip if already in path (cycle)
            if (in_path.find(callee) != in_path.end()) {
                continue;
            }
            
            // Push new state
            State next;
            next.node = callee;
            next.callee_index = 0;
            const auto& next_callees = graph_.get_callees(callee);
            next.callees.assign(next_callees.begin(), next_callees.end());
            
            stack.push(next);
            current_path.push_back(callee);
            in_path.insert(callee);
            found_next = true;
            break;
        }
        
        // If no more callees to explore, backtrack
        if (!found_next) {
            in_path.erase(state.node);
            current_path.pop_back();
            stack.pop();
        }
    }
}

// Iterative DFS for backward path finding (callee -> caller direction)
void QueryEngine::dfs_backward(SymbolUID start, SymbolUID end, PathCallback& callback) {
    // State for iterative DFS
    struct State {
        SymbolUID node;
        size_t caller_index;  // Which caller to explore next
        std::vector<SymbolUID> callers;  // Cached callers
    };
    
    std::stack<State> stack;
    std::vector<SymbolUID> current_path;
    std::unordered_set<SymbolUID> in_path;  // For cycle detection
    
    // Initialize
    State initial;
    initial.node = start;
    initial.caller_index = 0;
    const auto& start_callers = graph_.get_callers(start);
    initial.callers.assign(start_callers.begin(), start_callers.end());
    
    stack.push(initial);
    current_path.push_back(start);
    in_path.insert(start);
    
    while (!stack.empty()) {
        State& state = stack.top();
        
        // Check if we've reached a root (no more callers) or specific end
        const auto& callers = graph_.get_callers(state.node);
        bool is_root = callers.empty();
        bool is_end = (end != INVALID_UID && state.node == end);
        
        if (is_root || is_end) {
            // Convert path to strings (reverse to show caller -> callee)
            std::vector<std::string> path_str;
            path_str.reserve(current_path.size());
            for (auto it = current_path.rbegin(); it != current_path.rend(); ++it) {
                path_str.push_back(graph_.get_symbol(*it));
            }
            
            if (!callback(path_str)) {
                return;  // Callback requested stop
            }
            
            // Backtrack
            in_path.erase(state.node);
            current_path.pop_back();
            stack.pop();
            continue;
        }
        
        // Find next caller to explore
        bool found_next = false;
        while (state.caller_index < state.callers.size()) {
            SymbolUID caller = state.callers[state.caller_index];
            state.caller_index++;
            
            // Skip if already in path (cycle)
            if (in_path.find(caller) != in_path.end()) {
                continue;
            }
            
            // Push new state
            State next;
            next.node = caller;
            next.caller_index = 0;
            const auto& next_callers = graph_.get_callers(caller);
            next.callers.assign(next_callers.begin(), next_callers.end());
            
            stack.push(next);
            current_path.push_back(caller);
            in_path.insert(caller);
            found_next = true;
            break;
        }
        
        // If no more callers to explore, backtrack
        if (!found_next) {
            in_path.erase(state.node);
            current_path.pop_back();
            stack.pop();
        }
    }
}

} // namespace pioneer
