#include "pioneer/parser.hpp"
#include <stdexcept>
#include <sstream>
#include <algorithm>
#include <cstring>

namespace pioneer {

LanguageParser::LanguageParser(Language lang) : language_(lang) {
    parser_ = ts_parser_new();
    if (!parser_) {
        throw std::runtime_error("Failed to create tree-sitter parser");
    }
    
    const TSLanguage* ts_lang = nullptr;
    switch (lang) {
        case Language::Python:
            ts_lang = tree_sitter_python();
            break;
        case Language::C:
            ts_lang = tree_sitter_c();
            break;
        case Language::Cpp:
            ts_lang = tree_sitter_cpp();
            break;
        default:
            ts_parser_delete(parser_);
            throw std::runtime_error("Unsupported language");
    }
    
    if (!ts_parser_set_language(parser_, ts_lang)) {
        ts_parser_delete(parser_);
        throw std::runtime_error("Failed to set parser language");
    }
}

LanguageParser::~LanguageParser() {
    if (tree_) ts_tree_delete(tree_);
    if (parser_) ts_parser_delete(parser_);
}

LanguageParser::LanguageParser(LanguageParser&& other) noexcept
    : language_(other.language_)
    , parser_(other.parser_)
    , tree_(other.tree_)
    , source_(std::move(other.source_)) {
    other.parser_ = nullptr;
    other.tree_ = nullptr;
}

LanguageParser& LanguageParser::operator=(LanguageParser&& other) noexcept {
    if (this != &other) {
        if (tree_) ts_tree_delete(tree_);
        if (parser_) ts_parser_delete(parser_);
        
        language_ = other.language_;
        parser_ = other.parser_;
        tree_ = other.tree_;
        source_ = std::move(other.source_);
        
        other.parser_ = nullptr;
        other.tree_ = nullptr;
    }
    return *this;
}

bool LanguageParser::parse(const std::string& source) {
    source_ = source;
    
    if (tree_) {
        ts_tree_delete(tree_);
        tree_ = nullptr;
    }
    
    tree_ = ts_parser_parse_string(parser_, nullptr, source_.c_str(), source_.size());
    return tree_ != nullptr;
}

TSNode LanguageParser::root() const {
    if (!tree_) {
        return TSNode{};
    }
    return ts_tree_root_node(tree_);
}

std::string LanguageParser::node_text(TSNode node) const {
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    if (start < source_.size() && end <= source_.size()) {
        return source_.substr(start, end - start);
    }
    return "";
}

void LanguageParser::visit_nodes(TSNode node, const std::function<void(TSNode)>& visitor) const {
    // Use iterative approach with explicit stack to avoid recursion
    std::vector<TSNode> stack;
    stack.push_back(node);
    
    while (!stack.empty()) {
        TSNode current = stack.back();
        stack.pop_back();
        
        visitor(current);
        
        uint32_t child_count = ts_node_child_count(current);
        // Add children in reverse order so they're processed in order
        for (uint32_t i = child_count; i > 0; --i) {
            stack.push_back(ts_node_child(current, i - 1));
        }
    }
}

std::string LanguageParser::build_qualified_name(const std::string& base_name,
                                                   const std::vector<std::string>& param_types) const {
    // Just return base name - overload signatures added by indexer if needed
    (void)param_types;  // Unused here, handled by indexer
    return base_name;
}

std::string LanguageParser::build_signature(const std::vector<std::string>& param_types) const {
    if (param_types.empty()) {
        return "()";
    }
    
    std::ostringstream oss;
    oss << "(";
    for (size_t i = 0; i < param_types.size(); ++i) {
        if (i > 0) oss << ", ";
        // Simplify type names (remove qualifiers but keep readable)
        std::string type = param_types[i];
        // Remove leading/trailing whitespace
        size_t start = type.find_first_not_of(" \t");
        size_t end = type.find_last_not_of(" \t");
        if (start != std::string::npos && end != std::string::npos) {
            type = type.substr(start, end - start + 1);
        }
        // Remove 'const ' prefix and ' const' suffix for brevity
        size_t pos;
        while ((pos = type.find("const ")) != std::string::npos) {
            type.erase(pos, 6);
        }
        while ((pos = type.find(" const")) != std::string::npos) {
            type.erase(pos, 6);
        }
        // Normalize multiple spaces
        while ((pos = type.find("  ")) != std::string::npos) {
            type.erase(pos, 1);
        }
        oss << type;
    }
    oss << ")";
    return oss.str();
}

std::vector<FunctionDef> LanguageParser::extract_functions() const {
    switch (language_) {
        case Language::Python:
            return extract_functions_python();
        case Language::C:
            return extract_functions_c();
        case Language::Cpp:
            return extract_functions_cpp();
        default:
            return {};
    }
}

std::vector<FunctionCall> LanguageParser::extract_calls(const FunctionDef& func) const {
    switch (language_) {
        case Language::Python:
            return extract_calls_python(func);
        case Language::C:
            return extract_calls_c(func);
        case Language::Cpp:
            return extract_calls_cpp(func);
        default:
            return {};
    }
}

// ============ Python Implementation ============

std::vector<FunctionDef> LanguageParser::extract_functions_python() const {
    std::vector<FunctionDef> functions;
    if (!tree_) return functions;
    
    // Track class context using a stack
    struct Context {
        std::string class_name;
        uint32_t end_byte;
    };
    std::vector<Context> class_stack;
    
    visit_nodes(root(), [&](TSNode node) {
        const char* type = ts_node_type(node);
        uint32_t start_byte = ts_node_start_byte(node);
        
        // Pop classes that we've exited
        while (!class_stack.empty() && start_byte >= class_stack.back().end_byte) {
            class_stack.pop_back();
        }
        
        if (strcmp(type, "class_definition") == 0) {
            // Get class name
            TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
            if (!ts_node_is_null(name_node)) {
                Context ctx;
                ctx.class_name = node_text(name_node);
                ctx.end_byte = ts_node_end_byte(node);
                class_stack.push_back(ctx);
            }
        }
        else if (strcmp(type, "function_definition") == 0) {
            FunctionDef func;
            
            // Get function name
            TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
            if (!ts_node_is_null(name_node)) {
                func.name = node_text(name_node);
            }
            
            // Get parameters for type hints (if available)
            TSNode params_node = ts_node_child_by_field_name(node, "parameters", 10);
            if (!ts_node_is_null(params_node)) {
                uint32_t param_count = ts_node_named_child_count(params_node);
                for (uint32_t i = 0; i < param_count; ++i) {
                    TSNode param = ts_node_named_child(params_node, i);
                    const char* param_type = ts_node_type(param);
                    
                    if (strcmp(param_type, "typed_parameter") == 0 ||
                        strcmp(param_type, "typed_default_parameter") == 0) {
                        TSNode type_node = ts_node_child_by_field_name(param, "type", 4);
                        if (!ts_node_is_null(type_node)) {
                            func.param_types.push_back(node_text(type_node));
                        }
                    }
                }
            }
            
            // Build qualified name
            std::string prefix;
            if (!class_stack.empty()) {
                func.containing_class = class_stack.back().class_name;
                prefix = func.containing_class + ".";
            }
            
            func.qualified_name = prefix + build_qualified_name(func.name, func.param_types);
            func.start_line = ts_node_start_point(node).row + 1;
            func.end_line = ts_node_end_point(node).row + 1;
            func.node = node;
            
            functions.push_back(func);
        }
    });
    
    return functions;
}

std::vector<FunctionCall> LanguageParser::extract_calls_python(const FunctionDef& func) const {
    std::vector<FunctionCall> calls;
    
    visit_nodes(func.node, [&](TSNode node) {
        const char* type = ts_node_type(node);
        
        if (strcmp(type, "call") == 0) {
            FunctionCall call;
            call.line = ts_node_start_point(node).row + 1;
            call.node = node;
            
            TSNode func_node = ts_node_child_by_field_name(node, "function", 8);
            if (!ts_node_is_null(func_node)) {
                const char* func_type = ts_node_type(func_node);
                
                if (strcmp(func_type, "identifier") == 0) {
                    call.name = node_text(func_node);
                    call.qualified_name = call.name;
                }
                else if (strcmp(func_type, "attribute") == 0) {
                    // obj.method() - get the full attribute chain
                    call.name = node_text(func_node);
                    call.qualified_name = call.name;
                }
                
                if (!call.name.empty()) {
                    calls.push_back(call);
                }
            }
        }
    });
    
    return calls;
}

// ============ C Implementation ============

std::vector<FunctionDef> LanguageParser::extract_functions_c() const {
    std::vector<FunctionDef> functions;
    if (!tree_) return functions;
    
    visit_nodes(root(), [&](TSNode node) {
        const char* type = ts_node_type(node);
        
        if (strcmp(type, "function_definition") == 0) {
            FunctionDef func;
            
            // Get declarator which contains function name and parameters
            TSNode declarator = ts_node_child_by_field_name(node, "declarator", 10);
            if (!ts_node_is_null(declarator)) {
                // Navigate to function_declarator
                const char* decl_type = ts_node_type(declarator);
                TSNode func_decl = declarator;
                
                // Handle pointer declarators
                while (strcmp(decl_type, "pointer_declarator") == 0) {
                    func_decl = ts_node_child_by_field_name(func_decl, "declarator", 10);
                    if (ts_node_is_null(func_decl)) break;
                    decl_type = ts_node_type(func_decl);
                }
                
                if (strcmp(decl_type, "function_declarator") == 0) {
                    // Get function name
                    TSNode name_decl = ts_node_child_by_field_name(func_decl, "declarator", 10);
                    if (!ts_node_is_null(name_decl)) {
                        func.name = node_text(name_decl);
                    }
                    
                    // Get parameters
                    TSNode params = ts_node_child_by_field_name(func_decl, "parameters", 10);
                    if (!ts_node_is_null(params)) {
                        uint32_t param_count = ts_node_named_child_count(params);
                        for (uint32_t i = 0; i < param_count; ++i) {
                            TSNode param = ts_node_named_child(params, i);
                            const char* param_type_str = ts_node_type(param);
                            
                            if (strcmp(param_type_str, "parameter_declaration") == 0) {
                                TSNode type_node = ts_node_child_by_field_name(param, "type", 4);
                                if (!ts_node_is_null(type_node)) {
                                    func.param_types.push_back(node_text(type_node));
                                }
                            }
                        }
                    }
                }
            }
            
            if (!func.name.empty()) {
                func.qualified_name = build_qualified_name(func.name, func.param_types);
                func.start_line = ts_node_start_point(node).row + 1;
                func.end_line = ts_node_end_point(node).row + 1;
                func.node = node;
                functions.push_back(func);
            }
        }
    });
    
    return functions;
}

std::vector<FunctionCall> LanguageParser::extract_calls_c(const FunctionDef& func) const {
    std::vector<FunctionCall> calls;
    
    visit_nodes(func.node, [&](TSNode node) {
        const char* type = ts_node_type(node);
        
        if (strcmp(type, "call_expression") == 0) {
            FunctionCall call;
            call.line = ts_node_start_point(node).row + 1;
            call.node = node;
            
            TSNode func_node = ts_node_child_by_field_name(node, "function", 8);
            if (!ts_node_is_null(func_node)) {
                const char* func_type = ts_node_type(func_node);
                
                if (strcmp(func_type, "identifier") == 0) {
                    call.name = node_text(func_node);
                    call.qualified_name = call.name;
                }
                else if (strcmp(func_type, "field_expression") == 0) {
                    // struct->func or struct.func
                    TSNode field = ts_node_child_by_field_name(func_node, "field", 5);
                    if (!ts_node_is_null(field)) {
                        call.name = node_text(field);
                        call.qualified_name = call.name;
                    }
                }
                else if (strcmp(func_type, "parenthesized_expression") == 0) {
                    // Function pointer call (*func_ptr)()
                    call.name = node_text(func_node);
                    call.qualified_name = call.name;
                }
                
                if (!call.name.empty()) {
                    calls.push_back(call);
                }
            }
        }
    });
    
    return calls;
}

// ============ C++ Implementation ============

std::vector<FunctionDef> LanguageParser::extract_functions_cpp() const {
    std::vector<FunctionDef> functions;
    if (!tree_) return functions;
    
    // Track namespace and class context
    struct Context {
        std::string name;
        bool is_class;
        uint32_t end_byte;
    };
    std::vector<Context> context_stack;
    
    visit_nodes(root(), [&](TSNode node) {
        const char* type = ts_node_type(node);
        uint32_t start_byte = ts_node_start_byte(node);
        
        // Pop contexts that we've exited
        while (!context_stack.empty() && start_byte >= context_stack.back().end_byte) {
            context_stack.pop_back();
        }
        
        if (strcmp(type, "namespace_definition") == 0) {
            TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
            if (!ts_node_is_null(name_node)) {
                Context ctx;
                ctx.name = node_text(name_node);
                ctx.is_class = false;
                ctx.end_byte = ts_node_end_byte(node);
                context_stack.push_back(ctx);
            }
        }
        else if (strcmp(type, "class_specifier") == 0 || 
                 strcmp(type, "struct_specifier") == 0) {
            TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
            if (!ts_node_is_null(name_node)) {
                Context ctx;
                ctx.name = node_text(name_node);
                ctx.is_class = true;
                ctx.end_byte = ts_node_end_byte(node);
                context_stack.push_back(ctx);
            }
        }
        else if (strcmp(type, "function_definition") == 0) {
            FunctionDef func;
            
            // Get declarator
            TSNode declarator = ts_node_child_by_field_name(node, "declarator", 10);
            if (!ts_node_is_null(declarator)) {
                const char* decl_type = ts_node_type(declarator);
                TSNode func_decl = declarator;
                
                // Handle reference/pointer declarators
                while (strcmp(decl_type, "pointer_declarator") == 0 ||
                       strcmp(decl_type, "reference_declarator") == 0) {
                    func_decl = ts_node_child_by_field_name(func_decl, "declarator", 10);
                    if (ts_node_is_null(func_decl)) break;
                    decl_type = ts_node_type(func_decl);
                }
                
                if (strcmp(decl_type, "function_declarator") == 0) {
                    TSNode name_decl = ts_node_child_by_field_name(func_decl, "declarator", 10);
                    if (!ts_node_is_null(name_decl)) {
                        const char* name_type = ts_node_type(name_decl);
                        
                        if (strcmp(name_type, "qualified_identifier") == 0 ||
                            strcmp(name_type, "scoped_identifier") == 0) {
                            // Already qualified (e.g., Class::method)
                            func.name = node_text(name_decl);
                        }
                        else if (strcmp(name_type, "destructor_name") == 0) {
                            func.name = node_text(name_decl);
                        }
                        else if (strcmp(name_type, "identifier") == 0 ||
                                 strcmp(name_type, "field_identifier") == 0) {
                            func.name = node_text(name_decl);
                        }
                        else if (strcmp(name_type, "operator_name") == 0) {
                            func.name = node_text(name_decl);
                        }
                        else {
                            func.name = node_text(name_decl);
                        }
                    }
                    
                    // Get parameters
                    TSNode params = ts_node_child_by_field_name(func_decl, "parameters", 10);
                    if (!ts_node_is_null(params)) {
                        uint32_t param_count = ts_node_named_child_count(params);
                        for (uint32_t i = 0; i < param_count; ++i) {
                            TSNode param = ts_node_named_child(params, i);
                            const char* param_type_str = ts_node_type(param);
                            
                            if (strcmp(param_type_str, "parameter_declaration") == 0 ||
                                strcmp(param_type_str, "optional_parameter_declaration") == 0) {
                                TSNode type_node = ts_node_child_by_field_name(param, "type", 4);
                                if (!ts_node_is_null(type_node)) {
                                    func.param_types.push_back(node_text(type_node));
                                }
                            }
                        }
                    }
                }
            }
            
            if (!func.name.empty()) {
                // Build qualified name from context stack
                std::string prefix;
                for (const auto& ctx : context_stack) {
                    prefix += ctx.name + "::";
                    if (ctx.is_class) {
                        func.containing_class = ctx.name;
                    }
                }
                
                // If name already contains ::, don't add prefix
                if (func.name.find("::") == std::string::npos) {
                    func.qualified_name = prefix + build_qualified_name(func.name, func.param_types);
                } else {
                    func.qualified_name = build_qualified_name(func.name, func.param_types);
                }
                
                func.namespace_path = prefix;
                func.start_line = ts_node_start_point(node).row + 1;
                func.end_line = ts_node_end_point(node).row + 1;
                func.node = node;
                functions.push_back(func);
            }
        }
    });
    
    return functions;
}

std::vector<FunctionCall> LanguageParser::extract_calls_cpp(const FunctionDef& func) const {
    std::vector<FunctionCall> calls;
    
    visit_nodes(func.node, [&](TSNode node) {
        const char* type = ts_node_type(node);
        
        if (strcmp(type, "call_expression") == 0) {
            FunctionCall call;
            call.line = ts_node_start_point(node).row + 1;
            call.node = node;
            
            TSNode func_node = ts_node_child_by_field_name(node, "function", 8);
            if (!ts_node_is_null(func_node)) {
                const char* func_type = ts_node_type(func_node);
                
                if (strcmp(func_type, "identifier") == 0) {
                    call.name = node_text(func_node);
                    call.qualified_name = call.name;
                }
                else if (strcmp(func_type, "qualified_identifier") == 0 ||
                         strcmp(func_type, "scoped_identifier") == 0) {
                    call.name = node_text(func_node);
                    call.qualified_name = call.name;
                }
                else if (strcmp(func_type, "field_expression") == 0) {
                    // obj.method() or obj->method()
                    TSNode field = ts_node_child_by_field_name(func_node, "field", 5);
                    if (!ts_node_is_null(field)) {
                        call.name = node_text(field);
                        call.qualified_name = call.name;
                    }
                }
                else if (strcmp(func_type, "template_function") == 0) {
                    TSNode name = ts_node_child_by_field_name(func_node, "name", 4);
                    if (!ts_node_is_null(name)) {
                        call.name = node_text(name);
                        call.qualified_name = call.name;
                    }
                }
                else {
                    call.name = node_text(func_node);
                    call.qualified_name = call.name;
                }
                
                if (!call.name.empty()) {
                    calls.push_back(call);
                }
            }
        }
        // Constructor calls (new expressions)
        else if (strcmp(type, "new_expression") == 0) {
            FunctionCall call;
            call.line = ts_node_start_point(node).row + 1;
            call.node = node;
            
            TSNode type_node = ts_node_child_by_field_name(node, "type", 4);
            if (!ts_node_is_null(type_node)) {
                call.name = node_text(type_node);
                call.qualified_name = call.name;
                calls.push_back(call);
            }
        }
    });
    
    return calls;
}

std::unique_ptr<LanguageParser> create_parser(Language lang) {
    if (lang == Language::Unknown) {
        return nullptr;
    }
    return std::make_unique<LanguageParser>(lang);
}

std::string build_param_signature(const std::vector<std::string>& param_types) {
    if (param_types.empty()) {
        return "()";
    }
    
    std::ostringstream oss;
    oss << "(";
    for (size_t i = 0; i < param_types.size(); ++i) {
        if (i > 0) oss << ", ";
        std::string type = param_types[i];
        // Trim whitespace
        size_t start = type.find_first_not_of(" \t");
        size_t end = type.find_last_not_of(" \t");
        if (start != std::string::npos && end != std::string::npos) {
            type = type.substr(start, end - start + 1);
        }
        // Remove const qualifiers for brevity
        size_t pos;
        while ((pos = type.find("const ")) != std::string::npos) {
            type.erase(pos, 6);
        }
        while ((pos = type.find(" const")) != std::string::npos) {
            type.erase(pos, 6);
        }
        while ((pos = type.find("  ")) != std::string::npos) {
            type.erase(pos, 1);
        }
        oss << type;
    }
    oss << ")";
    return oss.str();
}

} // namespace pioneer
