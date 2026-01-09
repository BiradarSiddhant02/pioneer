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

#include "types.hpp"
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace pioneer {

using json = nlohmann::json;

enum class LoadMode { Full, SymbolsOnly, WithPaths };

class Graph {
public:

    CallGraph call_graph;

    SymbolUID add_symbol(const std::string &qualified_name, SymbolType type = SymbolType::Function);
    SymbolUID add_symbol(const std::string &qualified_name, const std::string &filepath,
                         SymbolType type = SymbolType::Function);
    SymbolUID get_or_create_file_uid(const std::string &filepath);
    std::string get_file_path(SymbolUID file_uid) const;
    SymbolUID get_symbol_file_uid(SymbolUID symbol_uid) const;
    const std::vector<SymbolUID> &get_file_symbols(SymbolUID file_uid) const;
    void add_call(const std::string &caller, const std::string &callee);
    void add_data_flow(const std::string &source, const std::string &dest);
    void finalize();

    json to_json() const;
    void save(const std::string &filepath) const;
    static Graph from_json(const json &j);
    static Graph load(const std::string &filepath);
    static Graph load(const std::string &filepath, LoadMode mode);

    SymbolUID get_uid(const std::string &name) const;
    const std::string &get_symbol(SymbolUID uid) const;
    const std::unordered_set<SymbolUID> &get_callees(SymbolUID caller) const;
    const std::unordered_set<SymbolUID> &get_callers(SymbolUID callee) const;
    const std::unordered_set<SymbolUID> &get_data_sources(SymbolUID variable) const;
    const std::unordered_set<SymbolUID> &get_data_sinks(SymbolUID source) const;
    bool is_variable(SymbolUID uid) const;
    bool has_symbol(const std::string &name) const;
    SymbolUID end_uid() const;
    std::vector<std::string> get_all_symbols() const;
    const std::unordered_map<std::string, SymbolUID> &get_symbol_map() const;
};

} // namespace pioneer
