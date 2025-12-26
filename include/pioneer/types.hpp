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

#include <algorithm>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace pioneer {

// ============================================================================
// String Pool - Intern strings to avoid duplication
// ============================================================================
class StringPool {
public:
    // Intern a string and return its index
    size_t intern(const std::string& str) {
        auto it = index_.find(str);
        if (it != index_.end()) {
            return it->second;
        }
        size_t idx = strings_.size();
        strings_.push_back(str);
        // Use string_view pointing to the stored string
        index_[strings_.back()] = idx;
        return idx;
    }

    // Get string by index
    const std::string& get(size_t idx) const {
        static const std::string empty;
        return (idx < strings_.size()) ? strings_[idx] : empty;
    }

    // Get string_view by index (zero-copy)
    std::string_view get_view(size_t idx) const {
        return (idx < strings_.size()) ? std::string_view(strings_[idx]) : std::string_view();
    }

    // Check if string exists
    bool contains(const std::string& str) const {
        return index_.find(str) != index_.end();
    }

    // Get index for string (returns SIZE_MAX if not found)
    size_t find(const std::string& str) const {
        auto it = index_.find(str);
        return (it != index_.end()) ? it->second : SIZE_MAX;
    }

    size_t size() const { return strings_.size(); }

    // Shrink internal storage
    void shrink_to_fit() {
        strings_.shrink_to_fit();
    }

    // Clear all strings
    void clear() {
        strings_.clear();
        index_.clear();
    }

    // Iterator support for all strings
    auto begin() const { return strings_.begin(); }
    auto end() const { return strings_.end(); }

private:
    std::vector<std::string> strings_;
    std::unordered_map<std::string_view, size_t> index_;
};

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

struct PathNode {
    std::map<std::string, PathNode> subdirs;
    std::vector<SymbolUID> file_uids;  // File UIDs instead of filenames
};

// Call graph representation using UIDs with string interning
struct CallGraph {
    // String pool for symbol names - single source of truth for strings
    StringPool symbol_pool;

    // Symbol name -> UID mapping (uses string_view into pool)
    std::unordered_map<std::string, SymbolUID> symbol_to_uid;

    // UID -> String pool index (instead of storing duplicate strings)
    std::unordered_map<SymbolUID, size_t> uid_to_string_idx;

    // Symbol type mapping
    std::unordered_map<SymbolUID, SymbolType> symbol_types;

    // File tracking with UIDs - uses separate pool for file paths
    StringPool filepath_pool;
    std::unordered_map<std::string, SymbolUID> filepath_to_uid;  // filepath -> file UID
    std::unordered_map<SymbolUID, size_t> file_uid_to_path_idx;  // file UID -> filepath pool index
    std::unordered_map<SymbolUID, std::vector<SymbolUID>> file_to_symbols;  // file UID -> [symbol UIDs]
    std::unordered_map<SymbolUID, SymbolUID> symbol_to_file;  // symbol UID -> file UID
    SymbolUID next_file_uid = 1;  // Separate counter for file UIDs

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

    // Get or create UID for a symbol (uses string interning)
    SymbolUID get_or_create_uid(const std::string &symbol_name,
                                SymbolType type = SymbolType::Function) {
        auto it = symbol_to_uid.find(symbol_name);
        if (it != symbol_to_uid.end()) {
            return it->second;
        }
        SymbolUID uid = next_uid++;
        size_t str_idx = symbol_pool.intern(symbol_name);
        symbol_to_uid[symbol_name] = uid;
        uid_to_string_idx[uid] = str_idx;
        symbol_types[uid] = type;
        return uid;
    }

    // Get UID for a symbol (returns INVALID_UID if not found)
    SymbolUID get_uid(const std::string &symbol_name) const {
        auto it = symbol_to_uid.find(symbol_name);
        return (it != symbol_to_uid.end()) ? it->second : INVALID_UID;
    }

    // Get symbol name from UID (returns reference to interned string)
    const std::string& get_symbol(SymbolUID uid) const {
        static const std::string end_str = "END";
        static const std::string empty_str;
        if (uid == end_uid)
            return end_str;
        auto it = uid_to_string_idx.find(uid);
        return (it != uid_to_string_idx.end()) ? symbol_pool.get(it->second) : empty_str;
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
        size_t end_str_idx = symbol_pool.intern("END");
        symbol_to_uid["END"] = end_uid;
        uid_to_string_idx[end_uid] = end_str_idx;
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

        // Shrink all internal containers after finalization
        shrink_to_fit();
    }

    // Reclaim unused memory from all containers
    void shrink_to_fit() {
        symbol_pool.shrink_to_fit();
        filepath_pool.shrink_to_fit();
        // Shrink vectors in file_to_symbols
        for (auto& [uid, symbols] : file_to_symbols) {
            symbols.shrink_to_fit();
        }
    }

    // Get file path from file UID
    const std::string& get_file_path(SymbolUID file_uid) const {
        static const std::string empty_str;
        auto it = file_uid_to_path_idx.find(file_uid);
        return (it != file_uid_to_path_idx.end()) ? filepath_pool.get(it->second) : empty_str;
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

// Helper functions for PathNode trie
inline void add_to_path_trie(PathNode &root, const std::string &filepath, SymbolUID file_uid) {
    if (filepath.empty()) return;
    
    PathNode *current = &root;
    std::string remaining = filepath;
    size_t pos = 0;
    
    // Split path by '/' or '\\'
    while (pos < remaining.size()) {
        size_t next_slash = remaining.find_first_of("/\\", pos);
        if (next_slash == std::string::npos) {
            // Last component (filename) - store the file UID
            if (!filepath.empty()) {
                // Check if file UID already exists
                auto it = std::find(current->file_uids.begin(), current->file_uids.end(), file_uid);
                if (it == current->file_uids.end()) {
                    current->file_uids.push_back(file_uid);
                }
            }
            break;
        } else {
            // Directory component
            std::string dir = remaining.substr(pos, next_slash - pos);
            if (!dir.empty() && dir != ".") {
                current = &(current->subdirs[dir]);
            }
            pos = next_slash + 1;
        }
    }
}

inline PathNode build_path_trie(const std::unordered_map<SymbolUID, std::string> &file_uid_to_path) {
    PathNode root;
    for (const auto &[file_uid, filepath] : file_uid_to_path) {
        add_to_path_trie(root, filepath, file_uid);
    }
    return root;
}

} // namespace pioneer
