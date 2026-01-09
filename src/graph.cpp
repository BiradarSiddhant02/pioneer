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
#include "pioneer/version.hpp"
#include <fstream>
#include <sstream>
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

SymbolUID Graph::add_symbol(const std::string &qualified_name, SymbolType type) {
    SymbolUID uid = call_graph.get_or_create_uid(qualified_name);
    call_graph.symbol_types[uid] = type;
    return uid;
}

SymbolUID Graph::add_symbol(const std::string &qualified_name, const std::string &filepath,
                            SymbolType type) {
    SymbolUID uid = add_symbol(qualified_name, type);
    SymbolUID file_uid = get_or_create_file_uid(filepath);

    // Link symbol to file
    call_graph.symbol_to_file[uid] = file_uid;
    call_graph.file_to_symbols[file_uid].push_back(uid);

    return uid;
}

SymbolUID Graph::get_or_create_file_uid(const std::string &filepath) {
    auto it = call_graph.filepath_to_uid.find(filepath);
    if (it != call_graph.filepath_to_uid.end()) {
        return it->second;
    }

    SymbolUID file_uid = call_graph.next_file_uid++;
    size_t path_idx = call_graph.filepath_pool.intern(filepath);
    call_graph.filepath_to_uid[filepath] = file_uid;
    call_graph.file_uid_to_path_idx[file_uid] = path_idx;
    return file_uid;
}

std::string Graph::get_file_path(SymbolUID file_uid) const {
    return call_graph.get_file_path(file_uid);
}

SymbolUID Graph::get_symbol_file_uid(SymbolUID symbol_uid) const {
    auto it = call_graph.symbol_to_file.find(symbol_uid);
    return (it != call_graph.symbol_to_file.end()) ? it->second : INVALID_UID;
}

const std::vector<SymbolUID> &Graph::get_file_symbols(SymbolUID file_uid) const {
    static const std::vector<SymbolUID> empty;
    auto it = call_graph.file_to_symbols.find(file_uid);
    return (it != call_graph.file_to_symbols.end()) ? it->second : empty;
}

void Graph::add_call(const std::string &caller, const std::string &callee) {
    SymbolUID caller_uid = call_graph.get_or_create_uid(caller);
    SymbolUID callee_uid = call_graph.get_or_create_uid(callee);
    call_graph.add_call(caller_uid, callee_uid);
}

void Graph::add_data_flow(const std::string &source, const std::string &dest) {
    SymbolUID source_uid = call_graph.get_or_create_uid(source);
    SymbolUID dest_uid = call_graph.get_or_create_uid(dest);
    call_graph.add_data_flow(source_uid, dest_uid);
}

void Graph::finalize() { call_graph.finalize(); }

static void write_json_string(std::ostringstream &out, const std::string &s) {
    out << '"';
    for (char c : s) {
        switch (c) {
        case '"':
            out << "\\\"";
            break;
        case '\\':
            out << "\\\\";
            break;
        case '\b':
            out << "\\b";
            break;
        case '\f':
            out << "\\f";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                // Control characters - use \u00XX format
                out << "\\u00";
                out << "0123456789abcdef"[(c >> 4) & 0xf];
                out << "0123456789abcdef"[c & 0xf];
            } else {
                out << c;
            }
        }
    }
    out << '"';
}

void Graph::save(const std::string &filepath) const {
    std::ofstream file(filepath, std::ios::out | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file for writing: " + filepath);
    }

    // Use a large string buffer to minimize system calls
    std::ostringstream buffer;
    buffer.str().reserve(64 * 1024 * 1024); // Pre-allocate 64MB

    // Lambda to flush buffer periodically
    auto flush_if_large = [&]() {
        if (buffer.tellp() > 32 * 1024 * 1024) { // Flush every 32MB
            file << buffer.str();
            buffer.str("");
            buffer.clear();
        }
    };

    // Write JSON incrementally - compact format (no whitespace)
    buffer << "{\"metadata\":{";
    buffer << "\"version\":\"" << INDEX_SCHEMA_VERSION << "\",";
    buffer << "\"num_symbols\":" << call_graph.num_symbols() << ",";
    buffer << "\"num_functions\":" << call_graph.num_functions() << ",";
    buffer << "\"num_variables\":" << call_graph.num_variables() << ",";
    buffer << "\"end_uid\":" << call_graph.end_uid << ",";
    buffer << "\"num_files\":" << call_graph.file_uid_to_path_idx.size() << ",";

    // UIDs mapping - use custom string escaping
    buffer << "\"UIDs\":{";
    bool first = true;
    for (const auto &[name, uid] : call_graph.symbol_to_uid) {
        if (!first)
            buffer << ",";
        first = false;
        write_json_string(buffer, name);
        buffer << ":" << uid;
    }
    buffer << "}},";
    flush_if_large();

    // Symbol types
    buffer << "\"symbol_types\":{";
    first = true;
    for (const auto &[uid, type] : call_graph.symbol_types) {
        if (!first)
            buffer << ",";
        first = false;
        buffer << "\"" << uid << "\":" << static_cast<int>(type);
    }
    buffer << "},";
    flush_if_large();

    // Call mapping
    buffer << "\"call_mapping\":{";
    first = true;
    for (const auto &[caller_uid, callees] : call_graph.call_map) {
        if (!first)
            buffer << ",";
        first = false;
        buffer << "\"" << caller_uid << "\":[";
        bool first_callee = true;
        for (SymbolUID callee : callees) {
            if (!first_callee)
                buffer << ",";
            first_callee = false;
            buffer << callee;
        }
        buffer << "]";
    }
    buffer << "},";
    flush_if_large();

    // Data flow mapping
    buffer << "\"data_flow\":{";
    first = true;
    for (const auto &[source_uid, dests] : call_graph.data_flow_map) {
        if (!first)
            buffer << ",";
        first = false;
        buffer << "\"" << source_uid << "\":[";
        bool first_dest = true;
        for (SymbolUID dest : dests) {
            if (!first_dest)
                buffer << ",";
            first_dest = false;
            buffer << dest;
        }
        buffer << "]";
    }
    buffer << "},";
    flush_if_large();

    // File paths - use custom string escaping
    buffer << "\"file_paths\":{";
    first = true;
    for (const auto &[file_uid, path_idx] : call_graph.file_uid_to_path_idx) {
        if (!first)
            buffer << ",";
        first = false;
        buffer << "\"" << file_uid << "\":";
        write_json_string(buffer, call_graph.filepath_pool.get(path_idx));
    }
    buffer << "},";
    flush_if_large();

    // File to symbols mapping
    buffer << "\"file_symbols\":{";
    first = true;
    for (const auto &[file_uid, symbol_uids] : call_graph.file_to_symbols) {
        if (!first)
            buffer << ",";
        first = false;
        buffer << "\"" << file_uid << "\":[";
        bool first_sym = true;
        for (SymbolUID sym : symbol_uids) {
            if (!first_sym)
                buffer << ",";
            first_sym = false;
            buffer << sym;
        }
        buffer << "]";
    }
    buffer << "},";
    flush_if_large();

    // Symbol to file mapping
    buffer << "\"symbol_files\":{";
    first = true;
    for (const auto &[symbol_uid, file_uid] : call_graph.symbol_to_file) {
        if (!first)
            buffer << ",";
        first = false;
        buffer << "\"" << symbol_uid << "\":" << file_uid;
    }
    buffer << "},";
    flush_if_large();

    // Path trie - build and serialize directly
    std::unordered_map<SymbolUID, std::string> file_uid_to_path;
    file_uid_to_path.reserve(call_graph.file_uid_to_path_idx.size());
    for (const auto &[file_uid, path_idx] : call_graph.file_uid_to_path_idx) {
        file_uid_to_path[file_uid] = call_graph.filepath_pool.get(path_idx);
    }
    PathNode path_trie = build_path_trie(file_uid_to_path);
    buffer << "\"path_trie\":" << path_node_to_json(path_trie).dump() << "}";

    // Final flush
    file << buffer.str();
}

json Graph::to_json() const {
    json j;

    // Metadata
    j["metadata"]["version"] = INDEX_SCHEMA_VERSION;
    j["metadata"]["num_symbols"] = call_graph.num_symbols();
    j["metadata"]["num_functions"] = call_graph.num_functions();
    j["metadata"]["num_variables"] = call_graph.num_variables();
    j["metadata"]["end_uid"] = call_graph.end_uid;
    j["metadata"]["num_files"] = call_graph.file_uid_to_path_idx.size();

    // UIDs mapping
    json uids = json::object();
    for (const auto &[name, uid] : call_graph.symbol_to_uid) {
        uids[name] = uid;
    }
    j["metadata"]["UIDs"] = std::move(uids);

    // Symbol types
    json types = json::object();
    for (const auto &[uid, type] : call_graph.symbol_types) {
        types[std::to_string(uid)] = static_cast<int>(type);
    }
    j["symbol_types"] = std::move(types);

    // Call mapping (UID -> list of UIDs) - avoid intermediate vector
    json call_mapping = json::object();
    for (const auto &[caller_uid, callees] : call_graph.call_map) {
        call_mapping[std::to_string(caller_uid)] = json::array();
        for (SymbolUID callee : callees) {
            call_mapping[std::to_string(caller_uid)].push_back(callee);
        }
    }
    j["call_mapping"] = std::move(call_mapping);

    // Data flow mapping - avoid intermediate vector
    json data_flow = json::object();
    for (const auto &[source_uid, dests] : call_graph.data_flow_map) {
        data_flow[std::to_string(source_uid)] = json::array();
        for (SymbolUID dest : dests) {
            data_flow[std::to_string(source_uid)].push_back(dest);
        }
    }
    j["data_flow"] = std::move(data_flow);

    // File paths from string pool
    json file_paths = json::object();
    for (const auto &[file_uid, path_idx] : call_graph.file_uid_to_path_idx) {
        file_paths[std::to_string(file_uid)] = call_graph.filepath_pool.get(path_idx);
    }
    j["file_paths"] = std::move(file_paths);

    // File to symbols mapping
    json file_symbols = json::object();
    for (const auto &[file_uid, symbol_uids] : call_graph.file_to_symbols) {
        file_symbols[std::to_string(file_uid)] = symbol_uids;
    }
    j["file_symbols"] = std::move(file_symbols);

    // Symbol to file mapping
    json symbol_files = json::object();
    for (const auto &[symbol_uid, file_uid] : call_graph.symbol_to_file) {
        symbol_files[std::to_string(symbol_uid)] = file_uid;
    }
    j["symbol_files"] = std::move(symbol_files);

    // Path trie
    std::unordered_map<SymbolUID, std::string> file_uid_to_path;
    file_uid_to_path.reserve(call_graph.file_uid_to_path_idx.size());
    for (const auto &[file_uid, path_idx] : call_graph.file_uid_to_path_idx) {
        file_uid_to_path[file_uid] = call_graph.filepath_pool.get(path_idx);
    }
    PathNode path_trie = build_path_trie(file_uid_to_path);
    j["path_trie"] = path_node_to_json(path_trie);

    return j;
}

Graph Graph::from_json(const json &j) {
    Graph g;

    // Check schema version compatibility
    if (j.contains("metadata") && j["metadata"].contains("version")) {
        std::string file_version = j["metadata"]["version"].get<std::string>();
        int major = 0, minor = 0, patch = 0;
        if (parse_version(file_version, major, minor, patch)) {
            if (!is_schema_compatible(major, minor, patch)) {
                throw std::runtime_error(
                    "Index file version " + file_version +
                    " is not compatible with this version of pioneer (requires >= " +
                    std::to_string(MIN_COMPAT_SCHEMA_MAJOR) + "." +
                    std::to_string(MIN_COMPAT_SCHEMA_MINOR) + "." +
                    std::to_string(MIN_COMPAT_SCHEMA_PATCH) + "). Please re-index.");
            }
        }
    }

    // Load UIDs - use string interning
    const auto &uids = j["metadata"]["UIDs"];
    for (auto it = uids.begin(); it != uids.end(); ++it) {
        SymbolUID uid = it.value().get<SymbolUID>();
        const std::string &symbol_name = it.key();
        size_t str_idx = g.call_graph.symbol_pool.intern(symbol_name);
        g.call_graph.symbol_to_uid[symbol_name] = uid;
        g.call_graph.uid_to_string_idx[uid] = str_idx;
        if (uid >= g.call_graph.next_uid) {
            g.call_graph.next_uid = uid + 1;
        }
    }

    // Load END UID
    g.call_graph.end_uid = j["metadata"]["end_uid"].get<SymbolUID>();

    // Load symbol types
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

    // Load file system (v1.2.0) - use string interning for file paths
    if (j.contains("file_paths")) {
        const auto &file_paths = j["file_paths"];
        for (auto it = file_paths.begin(); it != file_paths.end(); ++it) {
            SymbolUID file_uid = std::stoull(it.key());
            std::string path = it.value().get<std::string>();
            size_t path_idx = g.call_graph.filepath_pool.intern(path);
            g.call_graph.file_uid_to_path_idx[file_uid] = path_idx;
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

    // Shrink string pools after loading
    g.call_graph.shrink_to_fit();

    // Note: path_trie is not loaded as it can be rebuilt from file_uid_to_path if needed

    return g;
}

// SAX-style JSON parser handler for memory-efficient loading
class GraphSaxHandler : public json::json_sax_t {
public:

    Graph &graph;
    LoadMode mode;

    // Parser state machine
    enum class Section {
        None,
        Metadata,
        UIDs,
        SymbolTypes,
        CallMapping,
        DataFlow,
        FilePaths,
        FileSymbols,
        SymbolFiles,
        PathTrie
    };
    Section current_section = Section::None;

    // Temporary state for parsing
    std::string current_key;
    SymbolUID current_uid = 0;
    std::vector<SymbolUID> current_array;
    int depth = 0;
    int section_depth = 0;
    bool in_array = false;
    int skip_depth = 0;

    explicit GraphSaxHandler(Graph &g, LoadMode m = LoadMode::Full) : graph(g), mode(m) {}

    // Check if we should skip a section based on load mode
    bool should_skip_section(Section s) const {
        switch (mode) {
        case LoadMode::SymbolsOnly:
            // Only load UIDs, SymbolTypes, Metadata
            return s == Section::CallMapping || s == Section::DataFlow || s == Section::FilePaths ||
                   s == Section::FileSymbols || s == Section::SymbolFiles || s == Section::PathTrie;
        case LoadMode::WithPaths:
            // Load symbols + file paths (for search --show-path)
            return s == Section::CallMapping || s == Section::DataFlow || s == Section::PathTrie;
        case LoadMode::Full:
        default:
            // Only skip path_trie
            return s == Section::PathTrie;
        }
    }

    bool null() override { return true; }

    bool boolean(bool) override { return true; }

    bool number_integer(number_integer_t val) override {
        return handle_number(static_cast<SymbolUID>(val));
    }

    bool number_unsigned(number_unsigned_t val) override {
        return handle_number(static_cast<SymbolUID>(val));
    }

    bool number_float(number_float_t, const string_t &) override { return true; }

    bool handle_number(SymbolUID val) {
        if (skip_depth > 0)
            return true;

        if (in_array) {
            current_array.push_back(val);
            return true;
        }

        switch (current_section) {
        case Section::UIDs:
            // current_key is symbol name, val is UID
            {
                size_t str_idx = graph.call_graph.symbol_pool.intern(current_key);
                graph.call_graph.symbol_to_uid[current_key] = val;
                graph.call_graph.uid_to_string_idx[val] = str_idx;
                if (val >= graph.call_graph.next_uid) {
                    graph.call_graph.next_uid = val + 1;
                }
            }
            break;
        case Section::SymbolTypes: {
            SymbolUID uid = std::stoull(current_key);
            graph.call_graph.symbol_types[uid] = static_cast<SymbolType>(val);
        } break;
        case Section::SymbolFiles: {
            SymbolUID symbol_uid = std::stoull(current_key);
            graph.call_graph.symbol_to_file[symbol_uid] = val;
        } break;
        case Section::Metadata:
            if (current_key == "end_uid") {
                graph.call_graph.end_uid = val;
            }
            break;
        default:
            break;
        }
        return true;
    }

    bool string(string_t &val) override {
        if (skip_depth > 0)
            return true;

        switch (current_section) {
        case Section::FilePaths: {
            SymbolUID file_uid = std::stoull(current_key);
            size_t path_idx = graph.call_graph.filepath_pool.intern(val);
            graph.call_graph.file_uid_to_path_idx[file_uid] = path_idx;
            graph.call_graph.filepath_to_uid[val] = file_uid;
            if (file_uid >= graph.call_graph.next_file_uid) {
                graph.call_graph.next_file_uid = file_uid + 1;
            }
        } break;
        case Section::Metadata:
            if (current_key == "version") {
                int major = 0, minor = 0, patch = 0;
                if (parse_version(val, major, minor, patch)) {
                    if (!is_schema_compatible(major, minor, patch)) {
                        throw std::runtime_error("Index file version " + val +
                                                 " is not compatible. Please re-index.");
                    }
                }
            }
            break;
        default:
            break;
        }
        return true;
    }

    bool binary(binary_t &) override { return true; }

    bool start_object(std::size_t) override {
        depth++;
        if (skip_depth > 0) {
            skip_depth++;
            return true;
        }
        // Check if we should skip this section based on load mode
        if (should_skip_section(current_section)) {
            skip_depth = 1;
        }
        return true;
    }

    bool end_object() override {
        depth--;
        if (skip_depth > 0) {
            skip_depth--;
            return true;
        }
        if (depth == 1) {
            current_section = Section::None;
        }
        return true;
    }

    bool start_array(std::size_t) override {
        if (skip_depth > 0)
            return true;
        in_array = true;
        current_array.clear();
        return true;
    }

    bool end_array() override {
        if (skip_depth > 0)
            return true;
        in_array = false;

        // Process the completed array
        switch (current_section) {
        case Section::CallMapping: {
            SymbolUID caller = std::stoull(current_key);
            for (SymbolUID callee : current_array) {
                graph.call_graph.call_map[caller].insert(callee);
                graph.call_graph.reverse_call_map[callee].insert(caller);
            }
        } break;
        case Section::DataFlow: {
            SymbolUID source = std::stoull(current_key);
            for (SymbolUID dest : current_array) {
                graph.call_graph.data_flow_map[source].insert(dest);
                graph.call_graph.reverse_data_flow_map[dest].insert(source);
            }
        } break;
        case Section::FileSymbols: {
            SymbolUID file_uid = std::stoull(current_key);
            graph.call_graph.file_to_symbols[file_uid] = std::move(current_array);
        } break;
        default:
            break;
        }
        current_array.clear();
        return true;
    }

    bool key(string_t &key) override {
        if (skip_depth > 0)
            return true;

        current_key = key;

        // Detect section changes at depth 1
        if (depth == 1) {
            if (key == "metadata")
                current_section = Section::Metadata;
            else if (key == "symbol_types")
                current_section = Section::SymbolTypes;
            else if (key == "call_mapping")
                current_section = Section::CallMapping;
            else if (key == "data_flow")
                current_section = Section::DataFlow;
            else if (key == "file_paths")
                current_section = Section::FilePaths;
            else if (key == "file_symbols")
                current_section = Section::FileSymbols;
            else if (key == "symbol_files")
                current_section = Section::SymbolFiles;
            else if (key == "path_trie")
                current_section = Section::PathTrie;
        } else if (depth == 2 && current_section == Section::Metadata && key == "UIDs") {
            current_section = Section::UIDs;
        }

        return true;
    }

    bool parse_error(std::size_t position, const std::string &last_token,
                     const json::exception &ex) override {
        throw std::runtime_error("JSON parse error at position " + std::to_string(position) + ": " +
                                 ex.what());
    }
};

// Load from file using streaming SAX parser (memory efficient)
Graph Graph::load(const std::string &filepath) { return load(filepath, LoadMode::Full); }

Graph Graph::load(const std::string &filepath, LoadMode mode) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file for reading: " + filepath);
    }

    Graph g;
    GraphSaxHandler handler(g, mode);

    // Use SAX parser - processes JSON without building DOM
    bool result = json::sax_parse(file, &handler);
    if (!result) {
        throw std::runtime_error("Failed to parse index file: " + filepath);
    }

    g.call_graph.shrink_to_fit();
    return g;
}

SymbolUID Graph::get_uid(const std::string &name) const { return call_graph.get_uid(name); }

const std::string &Graph::get_symbol(SymbolUID uid) const { return call_graph.get_symbol(uid); }

const std::unordered_set<SymbolUID> &Graph::get_callees(SymbolUID caller) const {
    static const std::unordered_set<SymbolUID> empty;
    auto it = call_graph.call_map.find(caller);
    return (it != call_graph.call_map.end()) ? it->second : empty;
}

const std::unordered_set<SymbolUID> &Graph::get_callers(SymbolUID callee) const {
    static const std::unordered_set<SymbolUID> empty;
    auto it = call_graph.reverse_call_map.find(callee);
    return (it != call_graph.reverse_call_map.end()) ? it->second : empty;
}

const std::unordered_set<SymbolUID> &Graph::get_data_sources(SymbolUID variable) const {
    static const std::unordered_set<SymbolUID> empty;
    auto it = call_graph.reverse_data_flow_map.find(variable);
    return (it != call_graph.reverse_data_flow_map.end()) ? it->second : empty;
}

const std::unordered_set<SymbolUID> &Graph::get_data_sinks(SymbolUID source) const {
    static const std::unordered_set<SymbolUID> empty;
    auto it = call_graph.data_flow_map.find(source);
    return (it != call_graph.data_flow_map.end()) ? it->second : empty;
}

bool Graph::is_variable(SymbolUID uid) const { return call_graph.is_variable(uid); }

bool Graph::has_symbol(const std::string &name) const {
    return call_graph.symbol_to_uid.find(name) != call_graph.symbol_to_uid.end();
}

SymbolUID Graph::end_uid() const { return call_graph.end_uid; }

std::vector<std::string> Graph::get_all_symbols() const {
    std::vector<std::string> symbols;
    symbols.reserve(call_graph.symbol_to_uid.size());
    for (const auto &[name, uid] : call_graph.symbol_to_uid) {
        symbols.push_back(name);
    }
    return symbols;
}

const std::unordered_map<std::string, SymbolUID> &Graph::get_symbol_map() const {
    return call_graph.symbol_to_uid;
}

} // namespace pioneer
