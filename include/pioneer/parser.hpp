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
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <tree_sitter/api.h>
#include <vector>

extern "C" {
const TSLanguage *tree_sitter_python();
const TSLanguage *tree_sitter_c();
const TSLanguage *tree_sitter_cpp();
}

namespace pioneer {

struct FunctionDef {
    std::string name;
    std::string qualified_name;
    std::string containing_class;
    std::string namespace_path;
    std::vector<std::string> param_types;
    uint32_t start_line;
    uint32_t end_line;
    TSNode node;
};

struct FunctionCall {
    std::string name;
    std::string qualified_name;
    uint32_t line;
    TSNode node;
};

struct VariableDef {
    std::string name;
    std::string qualified_name;
    std::string containing_func;
    std::string value_source;
    bool from_function_call;
    uint32_t line;
    TSNode node;
};

class LanguageParser {
public:

    explicit LanguageParser(Language lang);
    ~LanguageParser();

    LanguageParser(const LanguageParser &) = delete;
    LanguageParser &operator=(const LanguageParser &) = delete;
    LanguageParser(LanguageParser &&other) noexcept;
    LanguageParser &operator=(LanguageParser &&other) noexcept;

    bool parse(const std::string &source);
    bool parse(const char *data, size_t length);

    std::vector<FunctionDef> extract_functions() const;
    std::vector<FunctionCall> extract_calls(const FunctionDef &func) const;
    std::vector<VariableDef> extract_variables(const FunctionDef &func) const;

    TSNode root() const;
    const std::string &source() const { return source_; }
    std::string_view source_view() const { return std::string_view(source_ptr_, source_len_); }
    Language language() const { return language_; }

private:

    Language language_;
    TSParser *parser_ = nullptr;
    TSTree *tree_ = nullptr;
    std::string source_;
    const char *source_ptr_ = nullptr;
    size_t source_len_ = 0;

    std::string node_text(TSNode node) const;

    std::vector<FunctionDef> extract_functions_python() const;
    std::vector<FunctionDef> extract_functions_c() const;
    std::vector<FunctionDef> extract_functions_cpp() const;

    std::vector<FunctionCall> extract_calls_python(const FunctionDef &func) const;
    std::vector<FunctionCall> extract_calls_c(const FunctionDef &func) const;
    std::vector<FunctionCall> extract_calls_cpp(const FunctionDef &func) const;

    std::vector<VariableDef> extract_variables_python(const FunctionDef &func) const;
    std::vector<VariableDef> extract_variables_c(const FunctionDef &func) const;
    std::vector<VariableDef> extract_variables_cpp(const FunctionDef &func) const;

    void visit_nodes(TSNode node, const std::function<void(TSNode)> &visitor) const;
    std::string build_qualified_name(const std::string &base_name,
                                     const std::vector<std::string> &param_types) const;
    std::string build_signature(const std::vector<std::string> &param_types) const;
};

std::unique_ptr<LanguageParser> create_parser(Language lang);
std::string build_param_signature(const std::vector<std::string> &param_types);

} // namespace pioneer
