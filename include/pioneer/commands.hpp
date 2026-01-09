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

#include "graph.hpp"
#include <string>
#include <vector>

namespace pioneer {

int cmd_index(unsigned int num_threads);
int cmd_search(const std::vector<std::string> &patterns, bool nosort, bool show_path,
               const Graph &graph);
int cmd_search_streaming(const std::vector<std::string> &patterns, bool nosort);
int cmd_query(const std::vector<std::string> &start_chain,
              const std::vector<std::string> &end_chain, bool backtrace, bool pattern_match,
              bool nosort, bool show_path);
int cmd_list_symbols(bool nosort);
int cmd_list_symbols_streaming(bool nosort);
int cmd_type(const std::string &symbol, bool nosort);
int cmd_data_sources(const std::vector<std::string> &patterns, bool nosort);
int cmd_data_sinks(const std::vector<std::string> &patterns, bool nosort);
int cmd_list_variables(const std::vector<std::string> &patterns, bool nosort);
int cmd_find_member(const std::vector<std::string> &patterns, bool nosort);
int cmd_grep(const std::string &pattern, unsigned int num_threads, bool use_regex,
             bool ignore_case);
int cmd_grep_streaming(const std::string &pattern, unsigned int num_threads, bool use_regex,
                       bool ignore_case);

bool load_graph(Graph &graph);
bool load_graph(Graph &graph, LoadMode mode);
bool validate_symbol(const class QueryEngine &engine, const std::string &symbol,
                     const std::string &label, bool nosort);
bool validate_symbol(const class QueryEngine &engine, const std::vector<std::string> &symbols,
                     const std::string &label, bool nosort);
const char *symbol_type_to_string(SymbolType type);

} // namespace pioneer
