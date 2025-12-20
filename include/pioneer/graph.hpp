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

class Graph {
public:
    CallGraph call_graph;

    // Add a symbol definition
    SymbolUID add_symbol(const std::string &qualified_name,
                         SymbolType type = SymbolType::Function);

    // Add a symbol with file path
    SymbolUID add_symbol(const std::string &qualified_name, const std::string &filepath,
                         SymbolType type = SymbolType::Function);

    // Get or create file UID for a filepath
    SymbolUID get_or_create_file_uid(const std::string &filepath);

    // Get file path for a file UID
    std::string get_file_path(SymbolUID file_uid) const;

    // Get file UID for a symbol
    SymbolUID get_symbol_file_uid(SymbolUID symbol_uid) const;

    // Get all symbols in a file
    const std::vector<SymbolUID>& get_file_symbols(SymbolUID file_uid) const;

    // Add a call relationship
    void add_call(const std::string &caller, const std::string &callee);

    // Add a data flow relationship (source -> destination)
    void add_data_flow(const std::string &source, const std::string &dest);

    // Finalize graph (add END nodes)
    void finalize();

    // Serialize to JSON
    json to_json() const;

    // Save to file
    void save(const std::string &filepath) const;

    // Load from JSON
    static Graph from_json(const json &j);

    // Load from file
    static Graph load(const std::string &filepath);

    // Get UID for symbol (for querying)
    SymbolUID get_uid(const std::string &name) const;

    // Get symbol name from UID
    std::string get_symbol(SymbolUID uid) const;

    // Get callees for a caller
    const std::unordered_set<SymbolUID> &get_callees(SymbolUID caller) const;

    // Get callers for a callee (for backtrace)
    const std::unordered_set<SymbolUID> &get_callers(SymbolUID callee) const;

    // Get data flow sources for a variable (what it's assigned from)
    const std::unordered_set<SymbolUID> &get_data_sources(SymbolUID variable) const;

    // Get data flow sinks (what variables a source flows to)
    const std::unordered_set<SymbolUID> &get_data_sinks(SymbolUID source) const;

    // Check if a symbol is a variable
    bool is_variable(SymbolUID uid) const;

    // Check if symbol exists
    bool has_symbol(const std::string &name) const;

    // Get END UID
    SymbolUID end_uid() const;

    // Get all symbols
    std::vector<std::string> get_all_symbols() const;

    // Get direct access to symbol map (more efficient than copying all symbols)
    const std::unordered_map<std::string, SymbolUID> &get_symbol_map() const;
};

} // namespace pioneer
