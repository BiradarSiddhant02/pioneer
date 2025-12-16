#pragma once

#include "types.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>

namespace pioneer {

using json = nlohmann::json;

class Graph {
public:
    CallGraph call_graph;
    
    // Add a symbol definition
    SymbolUID add_symbol(const std::string& qualified_name) {
        return call_graph.get_or_create_uid(qualified_name);
    }
    
    // Add a call relationship
    void add_call(const std::string& caller, const std::string& callee) {
        SymbolUID caller_uid = call_graph.get_or_create_uid(caller);
        SymbolUID callee_uid = call_graph.get_or_create_uid(callee);
        call_graph.add_call(caller_uid, callee_uid);
    }
    
    // Finalize graph (add END nodes)
    void finalize() {
        call_graph.finalize();
    }
    
    // Serialize to JSON
    json to_json() const {
        json j;
        
        // Metadata
        j["metadata"]["num_symbols"] = call_graph.num_symbols();
        j["metadata"]["end_uid"] = call_graph.end_uid;
        
        // UIDs mapping
        json uids = json::object();
        for (const auto& [name, uid] : call_graph.symbol_to_uid) {
            uids[name] = uid;
        }
        j["metadata"]["UIDs"] = uids;
        
        // Call mapping (UID -> list of UIDs)
        json call_mapping = json::object();
        for (const auto& [caller_uid, callees] : call_graph.call_map) {
            std::vector<SymbolUID> callee_list(callees.begin(), callees.end());
            call_mapping[std::to_string(caller_uid)] = callee_list;
        }
        j["call_mapping"] = call_mapping;
        
        return j;
    }
    
    // Save to file
    void save(const std::string& filepath) const {
        std::ofstream file(filepath);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file for writing: " + filepath);
        }
        file << to_json().dump(2);
    }
    
    // Load from JSON
    static Graph from_json(const json& j) {
        Graph g;
        
        // Load UIDs
        const auto& uids = j["metadata"]["UIDs"];
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
        
        // Load call mapping
        const auto& call_mapping = j["call_mapping"];
        for (auto it = call_mapping.begin(); it != call_mapping.end(); ++it) {
            SymbolUID caller = std::stoull(it.key());
            for (SymbolUID callee : it.value()) {
                g.call_graph.call_map[caller].insert(callee);
                g.call_graph.reverse_call_map[callee].insert(caller);
            }
        }
        
        return g;
    }
    
    // Load from file
    static Graph load(const std::string& filepath) {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file for reading: " + filepath);
        }
        json j;
        file >> j;
        return from_json(j);
    }
    
    // Get UID for symbol (for querying)
    SymbolUID get_uid(const std::string& name) const {
        return call_graph.get_uid(name);
    }
    
    // Get symbol name from UID
    std::string get_symbol(SymbolUID uid) const {
        return call_graph.get_symbol(uid);
    }
    
    // Get callees for a caller
    const std::unordered_set<SymbolUID>& get_callees(SymbolUID caller) const {
        static const std::unordered_set<SymbolUID> empty;
        auto it = call_graph.call_map.find(caller);
        return (it != call_graph.call_map.end()) ? it->second : empty;
    }
    
    // Get callers for a callee (for backtrace)
    const std::unordered_set<SymbolUID>& get_callers(SymbolUID callee) const {
        static const std::unordered_set<SymbolUID> empty;
        auto it = call_graph.reverse_call_map.find(callee);
        return (it != call_graph.reverse_call_map.end()) ? it->second : empty;
    }
    
    // Check if symbol exists
    bool has_symbol(const std::string& name) const {
        return call_graph.symbol_to_uid.find(name) != call_graph.symbol_to_uid.end();
    }
    
    // Get END UID
    SymbolUID end_uid() const {
        return call_graph.end_uid;
    }
    
    // Get all symbols
    std::vector<std::string> get_all_symbols() const {
        std::vector<std::string> symbols;
        symbols.reserve(call_graph.symbol_to_uid.size());
        for (const auto& [name, uid] : call_graph.symbol_to_uid) {
            symbols.push_back(name);
        }
        return symbols;
    }
};

} // namespace pioneer
