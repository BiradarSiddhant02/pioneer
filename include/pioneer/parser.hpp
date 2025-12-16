#pragma once

#include "types.hpp"
#include <tree_sitter/api.h>
#include <string>
#include <vector>
#include <memory>
#include <functional>

// Forward declarations for tree-sitter language functions
extern "C" {
    const TSLanguage* tree_sitter_python();
    const TSLanguage* tree_sitter_c();
    const TSLanguage* tree_sitter_cpp();
}

namespace pioneer {

// Parsed function definition
struct FunctionDef {
    std::string name;               // Simple name
    std::string qualified_name;     // Fully qualified name
    std::string containing_class;   // Containing class/struct (if any)
    std::string namespace_path;     // Namespace path
    std::vector<std::string> param_types; // Parameter types for overload disambiguation
    uint32_t start_line;
    uint32_t end_line;
    TSNode node;                    // Original tree-sitter node
};

// Parsed function call
struct FunctionCall {
    std::string name;               // Callee name as written
    std::string qualified_name;     // Best-effort qualified name
    uint32_t line;
    TSNode node;
};

// Parser for a single language
class LanguageParser {
public:
    explicit LanguageParser(Language lang);
    ~LanguageParser();
    
    // Non-copyable
    LanguageParser(const LanguageParser&) = delete;
    LanguageParser& operator=(const LanguageParser&) = delete;
    
    // Movable
    LanguageParser(LanguageParser&& other) noexcept;
    LanguageParser& operator=(LanguageParser&& other) noexcept;
    
    // Parse source code
    bool parse(const std::string& source);
    
    // Extract function definitions
    std::vector<FunctionDef> extract_functions() const;
    
    // Extract function calls within a function
    std::vector<FunctionCall> extract_calls(const FunctionDef& func) const;
    
    // Get root node
    TSNode root() const;
    
    // Get source code
    const std::string& source() const { return source_; }
    
    // Get language
    Language language() const { return language_; }
    
private:
    Language language_;
    TSParser* parser_ = nullptr;
    TSTree* tree_ = nullptr;
    std::string source_;
    
    // Helper to get node text
    std::string node_text(TSNode node) const;
    
    // Language-specific extractors
    std::vector<FunctionDef> extract_functions_python() const;
    std::vector<FunctionDef> extract_functions_c() const;
    std::vector<FunctionDef> extract_functions_cpp() const;
    
    std::vector<FunctionCall> extract_calls_python(const FunctionDef& func) const;
    std::vector<FunctionCall> extract_calls_c(const FunctionDef& func) const;
    std::vector<FunctionCall> extract_calls_cpp(const FunctionDef& func) const;
    
    // Recursive node visitor
    void visit_nodes(TSNode node, const std::function<void(TSNode)>& visitor) const;
    
    // Build qualified name (base, without signature)
    std::string build_qualified_name(const std::string& base_name, 
                                      const std::vector<std::string>& param_types) const;
    
    // Build signature string from param types, e.g., "(int, char*)"
    std::string build_signature(const std::vector<std::string>& param_types) const;
};

// Factory to create parser for a language
std::unique_ptr<LanguageParser> create_parser(Language lang);

// Build signature from param types (standalone helper)
std::string build_param_signature(const std::vector<std::string>& param_types);

} // namespace pioneer
