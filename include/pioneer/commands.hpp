#pragma once

#include "graph.hpp"
#include <string>
#include <vector>

namespace pioneer {

// Command handlers
int cmd_index(unsigned int num_threads);
int cmd_search(const std::vector<std::string> &patterns, bool nosort, bool show_path,
               const Graph &graph);
int cmd_query(const std::vector<std::string> &start_chain,
              const std::vector<std::string> &end_chain, bool backtrace, bool pattern_match,
              bool nosort, bool show_path);
int cmd_list_symbols(bool nosort);
int cmd_type(const std::string &symbol, bool nosort);
int cmd_data_sources(const std::vector<std::string> &patterns, bool nosort);
int cmd_data_sinks(const std::vector<std::string> &patterns, bool nosort);
int cmd_list_variables(const std::vector<std::string> &patterns, bool nosort);
int cmd_find_member(const std::vector<std::string> &patterns, bool nosort);
int cmd_grep(const std::string &pattern, unsigned int num_threads, bool use_regex,
             bool ignore_case);

// Helper functions
bool load_graph(Graph &graph);
bool validate_symbol(const class QueryEngine &engine, const std::string &symbol,
                     const std::string &label, bool nosort);
bool validate_symbol(const class QueryEngine &engine, const std::vector<std::string> &symbols,
                     const std::string &label, bool nosort);
const char *symbol_type_to_string(SymbolType type);

} // namespace pioneer
