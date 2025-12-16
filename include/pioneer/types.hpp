#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace pioneer {

// Symbol UID type - 64-bit unsigned integer
using SymbolUID = uint64_t;

// Reserved UIDs
constexpr SymbolUID INVALID_UID = 0;
constexpr SymbolUID END_UID_PLACEHOLDER = UINT64_MAX; // Will be set to actual value after indexing

// Supported languages
enum class Language { Unknown, Python, C, Cpp };

// Convert language enum to string
inline const char *language_to_string(Language lang) {
    switch (lang) {
    case Language::Python:
        return "python";
    case Language::C:
        return "c";
    case Language::Cpp:
        return "cpp";
    default:
        return "unknown";
    }
}

// Get language from file extension
inline Language language_from_extension(const std::string &ext) {
    if (ext == ".py")
        return Language::Python;
    if (ext == ".c" || ext == ".h")
        return Language::C;
    if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".hpp" || ext == ".hh" ||
        ext == ".hxx")
        return Language::Cpp;
    return Language::Unknown;
}

// Symbol information
struct Symbol {
    std::string name;       // Fully qualified name (e.g., "MyClass::foo_int_string")
    std::string short_name; // Just the function name
    std::string file;       // Source file
    uint32_t line;          // Line number
    Language language;      // Source language

    bool operator==(const Symbol &other) const { return name == other.name; }
};

// Hash for Symbol
struct SymbolHash {
    size_t operator()(const Symbol &s) const { return std::hash<std::string>{}(s.name); }
};

// Call site information
struct CallSite {
    std::string caller; // Fully qualified caller name
    std::string callee; // Fully qualified callee name
    std::string file;   // Source file
    uint32_t line;      // Line number
};

// Symbol type classification
enum class SymbolType { Function, Variable, End };

// Variable assignment information
struct VariableAssignment {
    std::string variable;        // Variable being assigned (qualified name)
    std::string value_source;    // What it's assigned from (function call, variable, or literal)
    std::string containing_func; // Function where assignment happens
    uint32_t line;               // Line number
    bool is_function_call;       // True if assigned from function return value
};

// Call graph representation using UIDs
struct CallGraph {
    // Symbol name -> UID mapping
    std::unordered_map<std::string, SymbolUID> symbol_to_uid;

    // UID -> Symbol name mapping (for reverse lookup)
    std::unordered_map<SymbolUID, std::string> uid_to_symbol;

    // Symbol type mapping
    std::unordered_map<SymbolUID, SymbolType> symbol_types;

    // Call graph: caller UID -> set of callee UIDs
    std::unordered_map<SymbolUID, std::unordered_set<SymbolUID>> call_map;

    // Reverse call graph: callee UID -> set of caller UIDs (for backtrace)
    std::unordered_map<SymbolUID, std::unordered_set<SymbolUID>> reverse_call_map;

    // Data flow: variable UID -> set of source UIDs (what it's assigned from)
    std::unordered_map<SymbolUID, std::unordered_set<SymbolUID>> data_flow_map;

    // Reverse data flow: source UID -> set of variable UIDs (what variables it flows to)
    std::unordered_map<SymbolUID, std::unordered_set<SymbolUID>> reverse_data_flow_map;

    // Next available UID (starts at 1, 0 is invalid)
    SymbolUID next_uid = 1;

    // END symbol UID (set after all symbols are indexed)
    SymbolUID end_uid = INVALID_UID;

    // Get or create UID for a symbol
    SymbolUID get_or_create_uid(const std::string &symbol_name,
                                SymbolType type = SymbolType::Function) {
        auto it = symbol_to_uid.find(symbol_name);
        if (it != symbol_to_uid.end()) {
            return it->second;
        }
        SymbolUID uid = next_uid++;
        symbol_to_uid[symbol_name] = uid;
        uid_to_symbol[uid] = symbol_name;
        symbol_types[uid] = type;
        return uid;
    }

    // Get UID for a symbol (returns INVALID_UID if not found)
    SymbolUID get_uid(const std::string &symbol_name) const {
        auto it = symbol_to_uid.find(symbol_name);
        return (it != symbol_to_uid.end()) ? it->second : INVALID_UID;
    }

    // Get symbol name from UID
    std::string get_symbol(SymbolUID uid) const {
        if (uid == end_uid)
            return "END";
        auto it = uid_to_symbol.find(uid);
        return (it != uid_to_symbol.end()) ? it->second : "";
    }

    // Get symbol type
    SymbolType get_type(SymbolUID uid) const {
        auto it = symbol_types.find(uid);
        return (it != symbol_types.end()) ? it->second : SymbolType::Function;
    }

    // Check if symbol is a variable
    bool is_variable(SymbolUID uid) const { return get_type(uid) == SymbolType::Variable; }

    // Add a call edge
    void add_call(SymbolUID caller, SymbolUID callee) {
        call_map[caller].insert(callee);
        reverse_call_map[callee].insert(caller);
    }

    // Add a data flow edge (variable assignment)
    void add_data_flow(SymbolUID variable, SymbolUID source) {
        data_flow_map[variable].insert(source);
        reverse_data_flow_map[source].insert(variable);
    }

    // Finalize the graph - add END node and connect leaf nodes
    void finalize() {
        end_uid = next_uid++;
        symbol_to_uid["END"] = end_uid;
        uid_to_symbol[end_uid] = "END";
        symbol_types[end_uid] = SymbolType::End;

        // Find all leaf nodes (functions that don't call anyone)
        std::unordered_set<SymbolUID> all_symbols;
        for (const auto &[name, uid] : symbol_to_uid) {
            if (uid != end_uid && get_type(uid) == SymbolType::Function) {
                all_symbols.insert(uid);
            }
        }

        // Connect leaf function nodes to END
        for (SymbolUID uid : all_symbols) {
            if (call_map.find(uid) == call_map.end() || call_map[uid].empty()) {
                // This function doesn't call anything - connect to END
                add_call(uid, end_uid);
            }
        }
    }

    // Get number of symbols (excluding END)
    size_t num_symbols() const { return symbol_to_uid.size() - (end_uid != INVALID_UID ? 1 : 0); }

    // Get number of functions
    size_t num_functions() const {
        size_t count = 0;
        for (const auto &[uid, type] : symbol_types) {
            if (type == SymbolType::Function)
                count++;
        }
        return count;
    }

    // Get number of variables
    size_t num_variables() const {
        size_t count = 0;
        for (const auto &[uid, type] : symbol_types) {
            if (type == SymbolType::Variable)
                count++;
        }
        return count;
    }
};

} // namespace pioneer
