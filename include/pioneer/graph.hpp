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
                         SymbolType type = SymbolType::Function) {
        SymbolUID uid = call_graph.get_or_create_uid(qualified_name);
        call_graph.symbol_types[uid] = type;
        return uid;
    }

    // Add a call relationship
    void add_call(const std::string &caller, const std::string &callee) {
        SymbolUID caller_uid = call_graph.get_or_create_uid(caller);
        SymbolUID callee_uid = call_graph.get_or_create_uid(callee);
        call_graph.add_call(caller_uid, callee_uid);
    }

    // Add a data flow relationship (source -> destination)
    void add_data_flow(const std::string &source, const std::string &dest) {
        SymbolUID source_uid = call_graph.get_or_create_uid(source);
        SymbolUID dest_uid = call_graph.get_or_create_uid(dest);
        call_graph.add_data_flow(source_uid, dest_uid);
    }

    // Finalize graph (add END nodes)
    void finalize() { call_graph.finalize(); }

    // Serialize to JSON
    json to_json() const {
        json j;

        // Metadata
        j["metadata"]["version"] = "1.1.0";
        j["metadata"]["num_symbols"] = call_graph.num_symbols();
        j["metadata"]["num_functions"] = call_graph.num_functions();
        j["metadata"]["num_variables"] = call_graph.num_variables();
        j["metadata"]["end_uid"] = call_graph.end_uid;

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

        return j;
    }

    // Save to file
    void save(const std::string &filepath) const {
        std::ofstream file(filepath);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file for writing: " + filepath);
        }
        file << to_json().dump(2);
    }

    // Load from JSON
    static Graph from_json(const json &j) {
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

        return g;
    }

    // Load from file
    static Graph load(const std::string &filepath) {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file for reading: " + filepath);
        }
        json j;
        file >> j;
        return from_json(j);
    }

    // Get UID for symbol (for querying)
    SymbolUID get_uid(const std::string &name) const { return call_graph.get_uid(name); }

    // Get symbol name from UID
    std::string get_symbol(SymbolUID uid) const { return call_graph.get_symbol(uid); }

    // Get callees for a caller
    const std::unordered_set<SymbolUID> &get_callees(SymbolUID caller) const {
        static const std::unordered_set<SymbolUID> empty;
        auto it = call_graph.call_map.find(caller);
        return (it != call_graph.call_map.end()) ? it->second : empty;
    }

    // Get callers for a callee (for backtrace)
    const std::unordered_set<SymbolUID> &get_callers(SymbolUID callee) const {
        static const std::unordered_set<SymbolUID> empty;
        auto it = call_graph.reverse_call_map.find(callee);
        return (it != call_graph.reverse_call_map.end()) ? it->second : empty;
    }

    // Get data flow sources for a variable (what it's assigned from)
    const std::unordered_set<SymbolUID> &get_data_sources(SymbolUID variable) const {
        static const std::unordered_set<SymbolUID> empty;
        auto it = call_graph.reverse_data_flow_map.find(variable);
        return (it != call_graph.reverse_data_flow_map.end()) ? it->second : empty;
    }

    // Get data flow sinks (what variables a source flows to)
    const std::unordered_set<SymbolUID> &get_data_sinks(SymbolUID source) const {
        static const std::unordered_set<SymbolUID> empty;
        auto it = call_graph.data_flow_map.find(source);
        return (it != call_graph.data_flow_map.end()) ? it->second : empty;
    }

    // Check if a symbol is a variable
    bool is_variable(SymbolUID uid) const { return call_graph.is_variable(uid); }

    // Check if symbol exists
    bool has_symbol(const std::string &name) const {
        return call_graph.symbol_to_uid.find(name) != call_graph.symbol_to_uid.end();
    }

    // Get END UID
    SymbolUID end_uid() const { return call_graph.end_uid; }

    // Get all symbols
    std::vector<std::string> get_all_symbols() const {
        std::vector<std::string> symbols;
        symbols.reserve(call_graph.symbol_to_uid.size());
        for (const auto &[name, uid] : call_graph.symbol_to_uid) {
            symbols.push_back(name);
        }
        return symbols;
    }

    // Get direct access to symbol map (more efficient than copying all symbols)
    const std::unordered_map<std::string, SymbolUID>& get_symbol_map() const {
        return call_graph.symbol_to_uid;
    }
};

} // namespace pioneer
