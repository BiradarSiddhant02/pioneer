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

#include "pioneer/graph.hpp"
#include <fstream>
#include <stdexcept>

namespace pioneer {

// Helper function to serialize PathNode trie to JSON
static json path_node_to_json(const PathNode &node) {
    json j = json::object();
    
    // Serialize subdirectories
    if (!node.subdirs.empty()) {
        json subdirs = json::object();
        for (const auto &[name, subnode] : node.subdirs) {
            subdirs[name] = path_node_to_json(subnode);
        }
        j["subdirs"] = subdirs;
    }
    
    // Serialize file UIDs
    if (!node.file_uids.empty()) {
        j["files"] = node.file_uids;
    }
    
    return j;
}

// Helper function to deserialize PathNode trie from JSON
static PathNode path_node_from_json(const json &j) {
    PathNode node;
    
    // Deserialize subdirectories
    if (j.contains("subdirs")) {
        for (auto it = j["subdirs"].begin(); it != j["subdirs"].end(); ++it) {
            node.subdirs[it.key()] = path_node_from_json(it.value());
        }
    }
    
    // Deserialize file UIDs
    if (j.contains("files")) {
        node.file_uids = j["files"].get<std::vector<SymbolUID>>();
    }
    
    return node;
}

// Add a symbol definition
SymbolUID Graph::add_symbol(const std::string &qualified_name, SymbolType type) {
    SymbolUID uid = call_graph.get_or_create_uid(qualified_name);
    call_graph.symbol_types[uid] = type;
    return uid;
}

// Add a symbol with file path
SymbolUID Graph::add_symbol(const std::string &qualified_name, const std::string &filepath,
                            SymbolType type) {
    SymbolUID uid = add_symbol(qualified_name, type);
    SymbolUID file_uid = get_or_create_file_uid(filepath);
    
    // Link symbol to file
    call_graph.symbol_to_file[uid] = file_uid;
    call_graph.file_to_symbols[file_uid].push_back(uid);
    
    return uid;
}

// Get or create file UID for a filepath
SymbolUID Graph::get_or_create_file_uid(const std::string &filepath) {
    auto it = call_graph.filepath_to_uid.find(filepath);
    if (it != call_graph.filepath_to_uid.end()) {
        return it->second;
    }
    
    SymbolUID file_uid = call_graph.next_file_uid++;
    call_graph.filepath_to_uid[filepath] = file_uid;
    call_graph.file_uid_to_path[file_uid] = filepath;
    return file_uid;
}

// Get file path for a file UID
std::string Graph::get_file_path(SymbolUID file_uid) const {
    auto it = call_graph.file_uid_to_path.find(file_uid);
    return (it != call_graph.file_uid_to_path.end()) ? it->second : "";
}

// Get file UID for a symbol
SymbolUID Graph::get_symbol_file_uid(SymbolUID symbol_uid) const {
    auto it = call_graph.symbol_to_file.find(symbol_uid);
    return (it != call_graph.symbol_to_file.end()) ? it->second : INVALID_UID;
}

// Get all symbols in a file
const std::vector<SymbolUID>& Graph::get_file_symbols(SymbolUID file_uid) const {
    static const std::vector<SymbolUID> empty;
    auto it = call_graph.file_to_symbols.find(file_uid);
    return (it != call_graph.file_to_symbols.end()) ? it->second : empty;
}

// Add a call relationship
void Graph::add_call(const std::string &caller, const std::string &callee) {
    SymbolUID caller_uid = call_graph.get_or_create_uid(caller);
    SymbolUID callee_uid = call_graph.get_or_create_uid(callee);
    call_graph.add_call(caller_uid, callee_uid);
}

// Add a data flow relationship (source -> destination)
void Graph::add_data_flow(const std::string &source, const std::string &dest) {
    SymbolUID source_uid = call_graph.get_or_create_uid(source);
    SymbolUID dest_uid = call_graph.get_or_create_uid(dest);
    call_graph.add_data_flow(source_uid, dest_uid);
}

// Finalize graph (add END nodes)
void Graph::finalize() {
    call_graph.finalize();
}

// Serialize to JSON
json Graph::to_json() const {
    json j;

    // Metadata
    j["metadata"]["version"] = "1.2.0";  // Increment version for file tracking
    j["metadata"]["num_symbols"] = call_graph.num_symbols();
    j["metadata"]["num_functions"] = call_graph.num_functions();
    j["metadata"]["num_variables"] = call_graph.num_variables();
    j["metadata"]["end_uid"] = call_graph.end_uid;
    j["metadata"]["num_files"] = call_graph.file_uid_to_path.size();

    // UIDs mapping
    json uids = json::object();
    for (const auto &[name, uid] : call_graph.symbol_to_uid) {
        uids[name] = uid;
    }
    j["metadata"]["UIDs"] = uids;

    // Symbol types
    json types = json::object();
    for (const auto &[uid, type] : call_graph.symbol_types) {
        types[std::to_string(uid)] = static_cast<int>(type);
    }
    j["symbol_types"] = types;

    // Call mapping (UID -> list of UIDs)
    json call_mapping = json::object();
    for (const auto &[caller_uid, callees] : call_graph.call_map) {
        std::vector<SymbolUID> callee_list(callees.begin(), callees.end());
        call_mapping[std::to_string(caller_uid)] = callee_list;
    }
    j["call_mapping"] = call_mapping;

    // Data flow mapping (v1.1.0)
    json data_flow = json::object();
    for (const auto &[source_uid, dests] : call_graph.data_flow_map) {
        std::vector<SymbolUID> dest_list(dests.begin(), dests.end());
        data_flow[std::to_string(source_uid)] = dest_list;
    }
    j["data_flow"] = data_flow;

    // File system (v1.2.0)
    // File UID to path mapping
    json file_paths = json::object();
    for (const auto &[file_uid, path] : call_graph.file_uid_to_path) {
        file_paths[std::to_string(file_uid)] = path;
    }
    j["file_paths"] = file_paths;

    // File to symbols mapping
    json file_symbols = json::object();
    for (const auto &[file_uid, symbol_uids] : call_graph.file_to_symbols) {
        file_symbols[std::to_string(file_uid)] = symbol_uids;
    }
    j["file_symbols"] = file_symbols;

    // Symbol to file mapping
    json symbol_files = json::object();
    for (const auto &[symbol_uid, file_uid] : call_graph.symbol_to_file) {
        symbol_files[std::to_string(symbol_uid)] = file_uid;
    }
    j["symbol_files"] = symbol_files;

    // Build and serialize path trie
    PathNode path_trie = build_path_trie(call_graph.file_uid_to_path);
    j["path_trie"] = path_node_to_json(path_trie);

    return j;
}

// Save to file
void Graph::save(const std::string &filepath) const {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file for writing: " + filepath);
    }
    file << to_json().dump(2);
}

// Load from JSON
Graph Graph::from_json(const json &j) {
    Graph g;

    // Load UIDs
    const auto &uids = j["metadata"]["UIDs"];
    for (auto it = uids.begin(); it != uids.end(); ++it) {
        SymbolUID uid = it.value().get<SymbolUID>();
        g.call_graph.symbol_to_uid[it.key()] = uid;
        g.call_graph.uid_to_symbol[uid] = it.key();
        if (uid >= g.call_graph.next_uid) {
            g.call_graph.next_uid = uid + 1;
        }
    }

    // Load END UID
    g.call_graph.end_uid = j["metadata"]["end_uid"].get<SymbolUID>();

    // Load symbol types (v1.1.0)
    if (j.contains("symbol_types")) {
        const auto &types = j["symbol_types"];
        for (auto it = types.begin(); it != types.end(); ++it) {
            SymbolUID uid = std::stoull(it.key());
            g.call_graph.symbol_types[uid] = static_cast<SymbolType>(it.value().get<int>());
        }
    }

    // Load call mapping
    const auto &call_mapping = j["call_mapping"];
    for (auto it = call_mapping.begin(); it != call_mapping.end(); ++it) {
        SymbolUID caller = std::stoull(it.key());
        for (SymbolUID callee : it.value()) {
            g.call_graph.call_map[caller].insert(callee);
            g.call_graph.reverse_call_map[callee].insert(caller);
        }
    }

    // Load data flow mapping (v1.1.0)
    if (j.contains("data_flow")) {
        const auto &data_flow = j["data_flow"];
        for (auto it = data_flow.begin(); it != data_flow.end(); ++it) {
            SymbolUID source = std::stoull(it.key());
            for (SymbolUID dest : it.value()) {
                g.call_graph.data_flow_map[source].insert(dest);
                g.call_graph.reverse_data_flow_map[dest].insert(source);
            }
        }
    }

    // Load file system (v1.2.0)
    if (j.contains("file_paths")) {
        const auto &file_paths = j["file_paths"];
        for (auto it = file_paths.begin(); it != file_paths.end(); ++it) {
            SymbolUID file_uid = std::stoull(it.key());
            std::string path = it.value().get<std::string>();
            g.call_graph.file_uid_to_path[file_uid] = path;
            g.call_graph.filepath_to_uid[path] = file_uid;
            if (file_uid >= g.call_graph.next_file_uid) {
                g.call_graph.next_file_uid = file_uid + 1;
            }
        }
    }

    if (j.contains("file_symbols")) {
        const auto &file_symbols = j["file_symbols"];
        for (auto it = file_symbols.begin(); it != file_symbols.end(); ++it) {
            SymbolUID file_uid = std::stoull(it.key());
            g.call_graph.file_to_symbols[file_uid] = it.value().get<std::vector<SymbolUID>>();
        }
    }

    if (j.contains("symbol_files")) {
        const auto &symbol_files = j["symbol_files"];
        for (auto it = symbol_files.begin(); it != symbol_files.end(); ++it) {
            SymbolUID symbol_uid = std::stoull(it.key());
            SymbolUID file_uid = it.value().get<SymbolUID>();
            g.call_graph.symbol_to_file[symbol_uid] = file_uid;
        }
    }

    // Note: path_trie is not loaded as it can be rebuilt from file_uid_to_path if needed

    return g;
}

// Load from file
Graph Graph::load(const std::string &filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file for reading: " + filepath);
    }
    json j;
    file >> j;
    return from_json(j);
}

// Get UID for symbol (for querying)
SymbolUID Graph::get_uid(const std::string &name) const {
    return call_graph.get_uid(name);
}

// Get symbol name from UID
std::string Graph::get_symbol(SymbolUID uid) const {
    return call_graph.get_symbol(uid);
}

// Get callees for a caller
const std::unordered_set<SymbolUID> &Graph::get_callees(SymbolUID caller) const {
    static const std::unordered_set<SymbolUID> empty;
    auto it = call_graph.call_map.find(caller);
    return (it != call_graph.call_map.end()) ? it->second : empty;
}

// Get callers for a callee (for backtrace)
const std::unordered_set<SymbolUID> &Graph::get_callers(SymbolUID callee) const {
    static const std::unordered_set<SymbolUID> empty;
    auto it = call_graph.reverse_call_map.find(callee);
    return (it != call_graph.reverse_call_map.end()) ? it->second : empty;
}

// Get data flow sources for a variable (what it's assigned from)
const std::unordered_set<SymbolUID> &Graph::get_data_sources(SymbolUID variable) const {
    static const std::unordered_set<SymbolUID> empty;
    auto it = call_graph.reverse_data_flow_map.find(variable);
    return (it != call_graph.reverse_data_flow_map.end()) ? it->second : empty;
}

// Get data flow sinks (what variables a source flows to)
const std::unordered_set<SymbolUID> &Graph::get_data_sinks(SymbolUID source) const {
    static const std::unordered_set<SymbolUID> empty;
    auto it = call_graph.data_flow_map.find(source);
    return (it != call_graph.data_flow_map.end()) ? it->second : empty;
}

// Check if a symbol is a variable
bool Graph::is_variable(SymbolUID uid) const {
    return call_graph.is_variable(uid);
}

// Check if symbol exists
bool Graph::has_symbol(const std::string &name) const {
    return call_graph.symbol_to_uid.find(name) != call_graph.symbol_to_uid.end();
}

// Get END UID
SymbolUID Graph::end_uid() const {
    return call_graph.end_uid;
}

// Get all symbols
std::vector<std::string> Graph::get_all_symbols() const {
    std::vector<std::string> symbols;
    symbols.reserve(call_graph.symbol_to_uid.size());
    for (const auto &[name, uid] : call_graph.symbol_to_uid) {
        symbols.push_back(name);
    }
    return symbols;
}

// Get direct access to symbol map (more efficient than copying all symbols)
const std::unordered_map<std::string, SymbolUID> &Graph::get_symbol_map() const {
    return call_graph.symbol_to_uid;
}

} // namespace pioneer
