// Copyright 2025 Siddhant Biradar
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "pioneer/query.hpp"
#include <algorithm>
#include <iostream>
#include <stack>
#include <unordered_set>

namespace pioneer {

QueryEngine::QueryEngine(const Graph &graph) : graph_(graph) {}

bool QueryEngine::has_symbol(const std::string &name) const { return graph_.has_symbol(name); }

std::vector<std::string> QueryEngine::find_symbols(const std::vector<std::string> &patterns) const {
    if (patterns.empty())
        return {};

    // Start with first pattern
    std::vector<std::string> matches = find_symbols(patterns[0]);

    // Narrow down with each subsequent pattern
    for (size_t i = 1; i < patterns.size() && !matches.empty(); ++i) {
        std::vector<std::string> filtered;
        for (const auto &sym : matches) {
            if (sym.find(patterns[i]) != std::string::npos) {
                filtered.push_back(sym);
            }
        }
        matches = std::move(filtered);
    }

    return matches;
}

std::vector<std::string> QueryEngine::find_symbols(const std::string &pattern) const {
    std::vector<std::string> matches;

    for (const auto &[symbol, uid] : graph_.get_symbol_map()) {
        if (symbol.find(pattern) != std::string::npos) {
            matches.push_back(symbol);
        }
    }

    return matches;
}

void QueryEngine::print_path(const std::vector<std::string> &path) {
    for (size_t i = 0; i < path.size(); ++i) {
        std::cout << path[i];
        if (i < path.size() - 1) {
            std::cout << " -> ";
        }
    }
    std::cout << std::endl;
}

void QueryEngine::print_path(const std::vector<std::string> &path, const Graph &graph, bool show_paths) {
    if (!show_paths) {
        print_path(path);  // Use the simple version
        return;
    }

    // Print each symbol on its own line with file path
    for (size_t i = 0; i < path.size(); ++i) {
        std::cout << "  ";
        if (i < path.size() - 1) {
            std::cout << "└─> ";
        } else {
            std::cout << "    ";
        }
        
        std::cout << path[i];
        
        // Get file path for this symbol
        SymbolUID uid = graph.get_uid(path[i]);
        if (uid != INVALID_UID) {
            SymbolUID file_uid = graph.get_symbol_file_uid(uid);
            if (file_uid != INVALID_UID) {
                std::string filepath = graph.get_file_path(file_uid);
                if (!filepath.empty()) {
                    std::cout << " [" << filepath << "]";
                }
            }
        }
        std::cout << std::endl;
    }
}

void QueryEngine::find_paths(const std::string &start, const std::string &end,
                             PathCallback callback) {
    if (start == "START") {
        backtrace(end, callback);
        return;
    }

    if (end == "END") {
        forward_trace(start, callback);
        return;
    }

    if (start == "START" && end == "END") {
        std::cerr << "Error: Cannot use both START and END - at least one must be a specific symbol"
                  << std::endl;
        return;
    }

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

    // Use bidirectional search for specific A→B queries (faster for large graphs)
    dfs_bidirectional(start_uid, end_uid, callback);
}

void QueryEngine::backtrace(const std::string &symbol, PathCallback callback) {
    SymbolUID target_uid = graph_.get_uid(symbol);

    if (target_uid == INVALID_UID) {
        std::cerr << "Error: Symbol not found: " << symbol << std::endl;
        return;
    }

    dfs_backward(target_uid, INVALID_UID, callback);
}

void QueryEngine::forward_trace(const std::string &symbol, PathCallback callback) {
    SymbolUID start_uid = graph_.get_uid(symbol);

    if (start_uid == INVALID_UID) {
        std::cerr << "Error: Symbol not found: " << symbol << std::endl;
        return;
    }

    SymbolUID end_uid = graph_.end_uid();
    dfs_forward(start_uid, end_uid, callback);
}

// Optimized DFS using iterators instead of copying callee vectors
void QueryEngine::dfs_forward(SymbolUID start, SymbolUID end, PathCallback &callback) {
    // State stores iterators into the graph's callee sets - NO COPYING
    struct State {
        SymbolUID node;
        std::unordered_set<SymbolUID>::const_iterator current_it;
        std::unordered_set<SymbolUID>::const_iterator end_it;
    };

    std::vector<State> stack;  // Use vector for better cache locality
    stack.reserve(256);  // Pre-allocate reasonable depth
    
    std::vector<SymbolUID> current_path;
    current_path.reserve(256);
    
    std::unordered_set<SymbolUID> in_path;
    in_path.reserve(256);

    // Initialize with start node
    const auto &start_callees = graph_.get_callees(start);
    stack.push_back({start, start_callees.begin(), start_callees.end()});
    current_path.push_back(start);
    in_path.insert(start);

    while (!stack.empty()) {
        State &state = stack.back();

        // Check if we've reached the target
        if (state.node == end) {
            // Convert path to strings and invoke callback (stream immediately)
            std::vector<std::string> path_str;
            path_str.reserve(current_path.size());
            for (SymbolUID uid : current_path) {
                path_str.push_back(graph_.get_symbol(uid));
            }

            if (!callback(path_str)) {
                return; // Callback requested stop
            }

            // Backtrack
            in_path.erase(state.node);
            current_path.pop_back();
            stack.pop_back();
            continue;
        }

        // Find next callee to explore using iterator
        bool found_next = false;
        while (state.current_it != state.end_it) {
            SymbolUID callee = *state.current_it;
            ++state.current_it;

            // Skip if already in path (cycle)
            if (in_path.count(callee)) {
                continue;
            }

            // Push new state with iterators into callee's adjacency set
            const auto &next_callees = graph_.get_callees(callee);
            stack.push_back({callee, next_callees.begin(), next_callees.end()});
            current_path.push_back(callee);
            in_path.insert(callee);
            found_next = true;
            break;
        }

        if (!found_next) {
            in_path.erase(state.node);
            current_path.pop_back();
            stack.pop_back();
        }
    }
}

// Optimized backward DFS using iterators instead of copying caller vectors
void QueryEngine::dfs_backward(SymbolUID start, SymbolUID end, PathCallback &callback) {
    // State stores iterators into the graph's caller sets - NO COPYING
    struct State {
        SymbolUID node;
        std::unordered_set<SymbolUID>::const_iterator current_it;
        std::unordered_set<SymbolUID>::const_iterator end_it;
    };

    std::vector<State> stack;
    stack.reserve(256);
    
    std::vector<SymbolUID> current_path;
    current_path.reserve(256);
    
    std::unordered_set<SymbolUID> in_path;
    in_path.reserve(256);

    // Initialize with start node
    const auto &start_callers = graph_.get_callers(start);
    stack.push_back({start, start_callers.begin(), start_callers.end()});
    current_path.push_back(start);
    in_path.insert(start);

    while (!stack.empty()) {
        State &state = stack.back();

        // Check if we've reached a root (no more callers) or specific end
        const auto &callers = graph_.get_callers(state.node);
        bool is_root = callers.empty();
        bool is_end = (end != INVALID_UID && state.node == end);

        if (is_root || is_end) {
            // Convert path to strings (reverse to show caller -> callee) and stream immediately
            std::vector<std::string> path_str;
            path_str.reserve(current_path.size());
            for (auto it = current_path.rbegin(); it != current_path.rend(); ++it) {
                path_str.push_back(graph_.get_symbol(*it));
            }

            if (!callback(path_str)) {
                return; // Callback requested stop
            }

            // Backtrack
            in_path.erase(state.node);
            current_path.pop_back();
            stack.pop_back();
            continue;
        }

        // Find next caller to explore using iterator
        bool found_next = false;
        while (state.current_it != state.end_it) {
            SymbolUID caller = *state.current_it;
            ++state.current_it;

            // Skip if already in path (cycle)
            if (in_path.count(caller)) {
                continue;
            }

            // Push new state with iterators
            const auto &next_callers = graph_.get_callers(caller);
            stack.push_back({caller, next_callers.begin(), next_callers.end()});
            current_path.push_back(caller);
            in_path.insert(caller);
            found_next = true;
            break;
        }

        if (!found_next) {
            in_path.erase(state.node);
            current_path.pop_back();
            stack.pop_back();
        }
    }
}

// Bidirectional DFS: search from both start and end, meet in the middle
// Streams paths as soon as they are found
void QueryEngine::dfs_bidirectional(SymbolUID start, SymbolUID end, PathCallback &callback) {
    // First, do a backward BFS from end to find all nodes reachable backward
    // Then do forward DFS from start, pruning branches that can't reach end
    
    // Phase 1: Build backward reachability set from end (limited BFS)
    std::unordered_set<SymbolUID> can_reach_end;
    std::unordered_map<SymbolUID, std::vector<SymbolUID>> backward_paths;  // node -> predecessors toward end
    
    {
        std::vector<SymbolUID> queue;
        queue.push_back(end);
        can_reach_end.insert(end);
        
        size_t head = 0;
        while (head < queue.size()) {
            SymbolUID node = queue[head++];
            
            // Get callers of this node (going backward)
            const auto &callers = graph_.get_callers(node);
            for (SymbolUID caller : callers) {
                if (can_reach_end.insert(caller).second) {
                    queue.push_back(caller);
                }
            }
        }
        queue.shrink_to_fit();
    }
    
    // If start can't reach end at all, return early
    if (!can_reach_end.count(start)) {
        return;  // No paths exist
    }
    
    // Phase 2: Forward DFS from start, only exploring nodes that can reach end
    struct State {
        SymbolUID node;
        std::unordered_set<SymbolUID>::const_iterator current_it;
        std::unordered_set<SymbolUID>::const_iterator end_it;
    };

    std::vector<State> stack;
    stack.reserve(256);
    
    std::vector<SymbolUID> current_path;
    current_path.reserve(256);
    
    std::unordered_set<SymbolUID> in_path;
    in_path.reserve(256);

    const auto &start_callees = graph_.get_callees(start);
    stack.push_back({start, start_callees.begin(), start_callees.end()});
    current_path.push_back(start);
    in_path.insert(start);

    while (!stack.empty()) {
        State &state = stack.back();

        // Check if we've reached the target
        if (state.node == end) {
            // Convert path to strings and stream immediately
            std::vector<std::string> path_str;
            path_str.reserve(current_path.size());
            for (SymbolUID uid : current_path) {
                path_str.push_back(graph_.get_symbol(uid));
            }

            if (!callback(path_str)) {
                return; // Callback requested stop
            }

            // Backtrack
            in_path.erase(state.node);
            current_path.pop_back();
            stack.pop_back();
            continue;
        }

        // Find next callee to explore (only if it can reach end)
        bool found_next = false;
        while (state.current_it != state.end_it) {
            SymbolUID callee = *state.current_it;
            ++state.current_it;

            // Skip if already in path (cycle)
            if (in_path.count(callee)) {
                continue;
            }
            
            // PRUNING: Skip if this callee can't reach end
            if (!can_reach_end.count(callee)) {
                continue;
            }

            // Push new state
            const auto &next_callees = graph_.get_callees(callee);
            stack.push_back({callee, next_callees.begin(), next_callees.end()});
            current_path.push_back(callee);
            in_path.insert(callee);
            found_next = true;
            break;
        }

        if (!found_next) {
            in_path.erase(state.node);
            current_path.pop_back();
            stack.pop_back();
        }
    }
}

std::vector<std::string> QueryEngine::data_sources(const std::string &variable) const {
    std::vector<std::string> sources;

    SymbolUID var_uid = graph_.get_uid(variable);
    if (var_uid == INVALID_UID)
        return sources;

    const auto &source_uids = graph_.get_data_sources(var_uid);
    for (SymbolUID uid : source_uids) {
        std::string name = graph_.get_symbol(uid);
        if (!name.empty()) {
            sources.push_back(name);
        }
    }

    return sources;
}

std::vector<std::string> QueryEngine::data_sinks(const std::string &source) const {
    std::vector<std::string> sinks;

    SymbolUID src_uid = graph_.get_uid(source);
    if (src_uid == INVALID_UID)
        return sinks;

    const auto &sink_uids = graph_.get_data_sinks(src_uid);
    for (SymbolUID uid : sink_uids) {
        std::string name = graph_.get_symbol(uid);
        if (!name.empty()) {
            sinks.push_back(name);
        }
    }

    return sinks;
}

std::vector<std::string> QueryEngine::variables_in(const std::string &func_pattern) const {
    std::vector<std::string> vars;
    vars.reserve(128);

    for (const auto &[symbol, uid] : graph_.get_symbol_map()) {
        if (uid == INVALID_UID)
            continue;
        if (!graph_.is_variable(uid))
            continue;

        if (symbol.find(func_pattern) != std::string::npos) {
            vars.push_back(symbol);
        }
    }

    vars.shrink_to_fit();
    return vars;
}

void QueryEngine::find_data_flow_paths(const std::string &source, const std::string &variable,
                                       PathCallback callback) {
    SymbolUID src_uid = graph_.get_uid(source);
    SymbolUID var_uid = graph_.get_uid(variable);

    if (src_uid == INVALID_UID) {
        std::cerr << "Error: Source symbol not found: " << source << std::endl;
        return;
    }

    if (var_uid == INVALID_UID) {
        std::cerr << "Error: Variable not found: " << variable << std::endl;
        return;
    }

    dfs_data_flow(src_uid, var_uid, callback);
}

// Optimized data flow DFS using iterators instead of copying sink vectors
void QueryEngine::dfs_data_flow(SymbolUID source, SymbolUID target, PathCallback &callback) {
    struct State {
        SymbolUID node;
        std::unordered_set<SymbolUID>::const_iterator current_it;
        std::unordered_set<SymbolUID>::const_iterator end_it;
    };

    std::vector<State> stack;
    stack.reserve(128);
    
    std::vector<SymbolUID> current_path;
    current_path.reserve(128);
    
    std::unordered_set<SymbolUID> in_path;
    in_path.reserve(128);
    
    const auto &init_sinks = graph_.get_data_sinks(source);
    stack.push_back({source, init_sinks.begin(), init_sinks.end()});
    current_path.push_back(source);
    in_path.insert(source);

    while (!stack.empty()) {
        State &state = stack.back();

        // Found target
        if (state.node == target && current_path.size() > 1) {
            // Build path with symbol names and stream immediately
            std::vector<std::string> path;
            path.reserve(current_path.size());
            for (SymbolUID uid : current_path) {
                path.push_back(graph_.get_symbol(uid));
            }

            if (!callback(path))
                return;

            // Backtrack
            in_path.erase(state.node);
            current_path.pop_back();
            stack.pop_back();
            continue;
        }

        // Explore next sink using iterator
        bool found_next = false;
        while (state.current_it != state.end_it) {
            SymbolUID sink = *state.current_it;
            ++state.current_it;

            // Skip if already in path (cycle)
            if (in_path.count(sink)) {
                continue;
            }

            // Push new state with iterators
            const auto &next_sinks = graph_.get_data_sinks(sink);
            stack.push_back({sink, next_sinks.begin(), next_sinks.end()});
            current_path.push_back(sink);
            in_path.insert(sink);
            found_next = true;
            break;
        }

        if (!found_next) {
            in_path.erase(state.node);
            current_path.pop_back();
            stack.pop_back();
        }
    }
}

} // namespace pioneer
