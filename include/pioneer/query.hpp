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

#pragma once

#include "graph.hpp"
#include <functional>
#include <string>
#include <vector>

namespace pioneer {

using PathCallback = std::function<bool(const std::vector<std::string> &path)>;

class QueryEngine {
public:

    explicit QueryEngine(const Graph &graph);

    void find_paths(const std::string &start, const std::string &end, PathCallback callback);
    void backtrace(const std::string &symbol, PathCallback callback);
    void forward_trace(const std::string &symbol, PathCallback callback);
    bool has_symbol(const std::string &name) const;
    std::vector<std::string> find_symbols(const std::vector<std::string> &patterns) const;
    std::vector<std::string> find_symbols(const std::string &pattern) const;
    static void print_path(const std::vector<std::string> &path);
    static void print_path(const std::vector<std::string> &path, const Graph &graph,
                           bool show_paths);

    // Data flow queries
    std::vector<std::string> data_sources(const std::string &variable) const;
    std::vector<std::string> data_sinks(const std::string &source) const;
    std::vector<std::string> variables_in(const std::string &func_pattern) const;
    void find_data_flow_paths(const std::string &source, const std::string &variable,
                              PathCallback callback);

private:

    const Graph &graph_;

    void dfs_forward(SymbolUID start, SymbolUID end, PathCallback &callback);
    void dfs_backward(SymbolUID start, SymbolUID end, PathCallback &callback);
    void dfs_bidirectional(SymbolUID start, SymbolUID end, PathCallback &callback);
    void dfs_data_flow(SymbolUID source, SymbolUID target, PathCallback &callback);
};

} // namespace pioneer
