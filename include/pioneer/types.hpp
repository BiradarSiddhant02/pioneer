#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace pioneer {

// Symbol UID type - 64-bit unsigned integer
using SymbolUID = uint64_t;

// Reserved UIDs
constexpr SymbolUID INVALID_UID = 0;
constexpr SymbolUID END_UID_PLACEHOLDER = UINT64_MAX; // Will be set to actual value after indexing

// Supported languages
enum class Language {
    Unknown,
    Python,
    C,
    Cpp
};

// Convert language enum to string
inline const char* language_to_string(Language lang) {
    switch (lang) {
        case Language::Python: return "python";
        case Language::C: return "c";
        case Language::Cpp: return "cpp";
        default: return "unknown";
    }
}

// Get language from file extension
inline Language language_from_extension(const std::string& ext) {
    if (ext == ".py") return Language::Python;
    if (ext == ".c" || ext == ".h") return Language::C;
    if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || 
        ext == ".hpp" || ext == ".hh" || ext == ".hxx") return Language::Cpp;
    return Language::Unknown;
}

// Symbol information
struct Symbol {
    std::string name;           // Fully qualified name (e.g., "MyClass::foo_int_string")
    std::string short_name;     // Just the function name
    std::string file;           // Source file
    uint32_t line;              // Line number
    Language language;          // Source language
    
    bool operator==(const Symbol& other) const {
        return name == other.name;
    }
};

// Hash for Symbol
struct SymbolHash {
    size_t operator()(const Symbol& s) const {
        return std::hash<std::string>{}(s.name);
    }
};

// Call site information
struct CallSite {
    std::string caller;         // Fully qualified caller name
    std::string callee;         // Fully qualified callee name
    std::string file;           // Source file
    uint32_t line;              // Line number
};

// Call graph representation using UIDs
struct CallGraph {
    // Symbol name -> UID mapping
    std::unordered_map<std::string, SymbolUID> symbol_to_uid;
    
    // UID -> Symbol name mapping (for reverse lookup)
    std::unordered_map<SymbolUID, std::string> uid_to_symbol;
    
    // Call graph: caller UID -> set of callee UIDs
    std::unordered_map<SymbolUID, std::unordered_set<SymbolUID>> call_map;
    
    // Reverse call graph: callee UID -> set of caller UIDs (for backtrace)
    std::unordered_map<SymbolUID, std::unordered_set<SymbolUID>> reverse_call_map;
    
    // Next available UID (starts at 1, 0 is invalid)
    SymbolUID next_uid = 1;
    
    // END symbol UID (set after all symbols are indexed)
    SymbolUID end_uid = INVALID_UID;
    
    // Get or create UID for a symbol
    SymbolUID get_or_create_uid(const std::string& symbol_name) {
        auto it = symbol_to_uid.find(symbol_name);
        if (it != symbol_to_uid.end()) {
            return it->second;
        }
        SymbolUID uid = next_uid++;
        symbol_to_uid[symbol_name] = uid;
        uid_to_symbol[uid] = symbol_name;
        return uid;
    }
    
    // Get UID for a symbol (returns INVALID_UID if not found)
    SymbolUID get_uid(const std::string& symbol_name) const {
        auto it = symbol_to_uid.find(symbol_name);
        return (it != symbol_to_uid.end()) ? it->second : INVALID_UID;
    }
    
    // Get symbol name from UID
    std::string get_symbol(SymbolUID uid) const {
        if (uid == end_uid) return "END";
        auto it = uid_to_symbol.find(uid);
        return (it != uid_to_symbol.end()) ? it->second : "";
    }
    
    // Add a call edge
    void add_call(SymbolUID caller, SymbolUID callee) {
        call_map[caller].insert(callee);
        reverse_call_map[callee].insert(caller);
    }
    
    // Finalize the graph - add END node and connect leaf nodes
    void finalize() {
        end_uid = next_uid++;
        symbol_to_uid["END"] = end_uid;
        uid_to_symbol[end_uid] = "END";
        
        // Find all leaf nodes (callers that don't call anyone, or callees that aren't callers)
        std::unordered_set<SymbolUID> all_symbols;
        for (const auto& [name, uid] : symbol_to_uid) {
            if (uid != end_uid) {
                all_symbols.insert(uid);
            }
        }
        
        // Connect leaf nodes to END
        for (SymbolUID uid : all_symbols) {
            if (call_map.find(uid) == call_map.end() || call_map[uid].empty()) {
                // This symbol doesn't call anything - connect to END
                add_call(uid, end_uid);
            }
        }
    }
    
    // Get number of symbols (excluding END)
    size_t num_symbols() const {
        return symbol_to_uid.size() - (end_uid != INVALID_UID ? 1 : 0);
    }
};

} // namespace pioneer
