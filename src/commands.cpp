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

#include "pioneer/commands.hpp"
#include "pioneer/indexer.hpp"
#include "pioneer/query.hpp"
#include "pioneer/streaming.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <mutex>
#include <regex>
#include <set>
#include <thread>

namespace pioneer {

constexpr const char *INDEX_FILE = ".pioneer.json";

// Grep match result structure
struct GrepMatch {
    std::string filepath;
    size_t line_num;
    std::string line;
};

// Helper function to load the graph from index file (full load)
bool load_graph(Graph &graph) {
    try {
        graph = Graph::load(INDEX_FILE);
        return true;
    } catch (const std::exception &e) {
        std::cerr << "Error loading index: " << e.what() << std::endl;
        std::cerr << "Please run 'pioneer --index' first." << std::endl;
        return false;
    }
}

// Helper function to load graph with specific mode
bool load_graph(Graph &graph, LoadMode mode) {
    try {
        graph = Graph::load(INDEX_FILE, mode);
        return true;
    } catch (const std::exception &e) {
        std::cerr << "Error loading index: " << e.what() << std::endl;
        std::cerr << "Please run 'pioneer --index' first." << std::endl;
        return false;
    }
}

bool validate_symbol(const QueryEngine &engine, const std::string &symbol, const std::string &label,
                     bool nosort) {
    if (engine.has_symbol(symbol))
        return true;

    std::cerr << "Error: " << label << " not found: " << symbol << std::endl;
    auto matches = engine.find_symbols(symbol);
    if (!nosort)
        std::sort(matches.begin(), matches.end());

    if (!matches.empty()) {
        std::cerr << "Did you mean one of these?" << std::endl;
        for (size_t i = 0; i < std::min(matches.size(), size_t(5)); ++i)
            std::cerr << "  " << matches[i] << std::endl;
    }
    return false;
}

bool validate_symbol(const QueryEngine &engine, const std::vector<std::string> &symbols,
                     const std::string &label, bool nosort) {
    for (const auto &symbol : symbols) {
        if (!validate_symbol(engine, symbol, label, nosort))
            return false;
    }
    return true;
}

// Helper to convert SymbolType enum to string
const char *symbol_type_to_string(SymbolType type) {
    switch (type) {
    case SymbolType::Function:
        return "function";
    case SymbolType::Variable:
        return "variable";
    case SymbolType::End:
        return "end";
    default:
        return "unknown";
    }
}

int cmd_index(unsigned int num_threads) {
    std::cout << "Indexing current directory..." << std::endl;

    IndexerConfig config;
    config.root_path = ".";
    config.verbose = true;
    config.num_threads = num_threads;

    Indexer indexer(config);
    Graph graph = indexer.index();

    try {
        graph.save(INDEX_FILE);
        std::cout << "\nIndex saved to: " << INDEX_FILE << std::endl;
    } catch (const std::exception &e) {
        std::cerr << "Error saving index: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

int cmd_search_streaming(const std::vector<std::string> &patterns, bool nosort) {
    try {
        auto matches = stream_search_symbols(INDEX_FILE, patterns);
        
        if (!nosort) {
            std::sort(matches.begin(), matches.end());
        }
        
        std::cout << matches.size() << " Matches found" << std::endl;
        if (matches.empty()) {
            std::cout << "  (none found)" << std::endl;
        } else {
            for (const auto &sym : matches) {
                std::cout << "  " << sym << std::endl;
            }
        }
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        std::cerr << "Please run 'pioneer --index' first." << std::endl;
        return 1;
    }
}

int cmd_list_symbols_streaming(bool nosort) {
    try {
        auto symbols = stream_all_symbols(INDEX_FILE);
        
        if (!nosort) {
            std::sort(symbols.begin(), symbols.end());
        }
        
        std::cout << "Symbols in index (" << symbols.size() << "):" << std::endl;
        for (const auto &sym : symbols) {
            std::cout << "  " << sym << std::endl;
        }
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        std::cerr << "Please run 'pioneer --index' first." << std::endl;
        return 1;
    }
}

int cmd_grep_streaming(const std::string &pattern, unsigned int num_threads, bool use_regex, bool ignore_case) {
    std::vector<std::string> files;
    try {
        files = stream_file_paths(INDEX_FILE);
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        std::cerr << "Please run 'pioneer --index' first." << std::endl;
        return 1;
    }
    
    if (files.empty()) {
        std::cout << "No files found in index." << std::endl;
        return 0;
    }
    
    std::cout << "Searching " << files.size() << " files for pattern: " << pattern << std::endl;
    
    // Auto-detect thread count if not specified
    if (num_threads == 0) {
        num_threads = std::thread::hardware_concurrency();
        if (num_threads == 0) num_threads = 4;
    }
    
    std::vector<GrepMatch> results;
    std::mutex results_mutex;
    
    // Create worker threads
    std::vector<std::thread> threads;
    size_t files_per_thread = (files.size() + num_threads - 1) / num_threads;
    
    for (unsigned int t = 0; t < num_threads; ++t) {
        size_t start_idx = t * files_per_thread;
        size_t end_idx = std::min(start_idx + files_per_thread, files.size());
        
        if (start_idx >= files.size()) break;
        
        threads.emplace_back([&, start_idx, end_idx]() {
            std::vector<GrepMatch> local_results;
            std::regex regex_pattern;
            if (use_regex) {
                auto flags = std::regex::ECMAScript;
                if (ignore_case) flags |= std::regex::icase;
                regex_pattern = std::regex(pattern, flags);
            }
            
            for (size_t i = start_idx; i < end_idx; ++i) {
                const auto &filepath = files[i];
                std::ifstream src_file(filepath);
                if (!src_file.is_open()) continue;
                
                std::string line;
                size_t line_num = 0;
                while (std::getline(src_file, line)) {
                    ++line_num;
                    bool match = false;
                    if (use_regex) {
                        match = std::regex_search(line, regex_pattern);
                    } else if (ignore_case) {
                        std::string lower_line = line;
                        std::string lower_pattern = pattern;
                        std::transform(lower_line.begin(), lower_line.end(), lower_line.begin(), ::tolower);
                        std::transform(lower_pattern.begin(), lower_pattern.end(), lower_pattern.begin(), ::tolower);
                        match = lower_line.find(lower_pattern) != std::string::npos;
                    } else {
                        match = line.find(pattern) != std::string::npos;
                    }
                    
                    if (match) {
                        local_results.push_back({filepath, line_num, line});
                    }
                }
            }
            
            if (!local_results.empty()) {
                std::lock_guard<std::mutex> lock(results_mutex);
                results.insert(results.end(), local_results.begin(), local_results.end());
            }
        });
    }
    
    for (auto &t : threads) {
        t.join();
    }
    
    // Sort results by file path and line number
    std::sort(results.begin(), results.end(), [](const GrepMatch &a, const GrepMatch &b) {
        if (a.filepath != b.filepath) return a.filepath < b.filepath;
        return a.line_num < b.line_num;
    });
    
    // Print results
    std::cout << "\n" << results.size() << " matches found:\n" << std::endl;
    for (const auto &match : results) {
        std::cout << match.filepath << ":" << match.line_num << ": " << match.line << std::endl;
    }
    
    return 0;
}

int cmd_search(const std::vector<std::string> &patterns, const bool nosort, const bool show_path, const Graph &graph) {
    QueryEngine engine(graph);
    auto matches = engine.find_symbols(patterns);

    if (!nosort) {
        std::sort(matches.begin(), matches.end());
    }

    std::cout << matches.size() << " Matches found" << std::endl;
    if (matches.empty()) {
        std::cout << "  (none found)" << std::endl;
    } else {
        for (const auto &sym : matches) {
            std::cout << "  " << sym;
            if (show_path) {
                SymbolUID uid = graph.get_uid(sym);
                if (uid != INVALID_UID) {
                    SymbolUID file_uid = graph.get_symbol_file_uid(uid);
                    if (file_uid != INVALID_UID) {
                        std::string filepath = graph.get_file_path(file_uid);
                        if (!filepath.empty()) {
                            std::cout << " [" << filepath << "]";
                        }
                    }
                }
            }
            std::cout << std::endl;
        }
    }

    return 0;
}

int cmd_query(const std::vector<std::string> &start_chain,
              const std::vector<std::string> &end_chain, bool backtrace, bool pattern_match,
              bool nosort, bool show_path) {
    Graph graph;
    if (!load_graph(graph))
        return 1;

    QueryEngine engine(graph);

    // Handle special cases
    bool is_backtrace = backtrace || (!start_chain.empty() && start_chain[0] == "START");
    bool is_forward = !end_chain.empty() && end_chain[0] == "END";

    // Resolve patterns if needed and validate all symbols
    auto resolve_chain =
        [&](const std::vector<std::string> &chain,
            const std::string &label) -> std::pair<bool, std::vector<std::string>> {
        std::vector<std::string> resolved;
        for (const auto &sym : chain) {
            if (sym == "START" || sym == "END") {
                resolved.push_back(sym);
                continue;
            }
            std::string actual = sym;
            if (pattern_match) {
                auto matches = engine.find_symbols(sym);
                if (!nosort)
                    std::sort(matches.begin(), matches.end());
                if (matches.empty()) {
                    std::cerr << "Error: No symbols matching pattern: " << sym << std::endl;
                    return {false, {}};
                }
                if (matches.size() > 1) {
                    std::cout << "Pattern '" << sym << "' matches:" << std::endl;
                    for (size_t i = 0; i < std::min(matches.size(), size_t(5)); ++i)
                        std::cout << "  [" << (i + 1) << "] " << matches[i] << std::endl;
                    std::cout << "Using: " << matches[0] << std::endl;
                }
                actual = matches[0];
            }
            if (!validate_symbol(engine, actual, label, nosort))
                return {false, {}};
            resolved.push_back(actual);
        }
        return {true, resolved};
    };

    std::vector<std::string> start_resolved, end_resolved;

    if (is_backtrace) {
        start_resolved = {"START"};
        auto [ok, res] = resolve_chain(end_chain, "End chain");
        if (!ok)
            return 1;
        end_resolved = res;
        if (end_resolved.empty()) {
            std::cerr << "Error: --end symbol required for backtrace" << std::endl;
            return 1;
        }
    } else if (is_forward) {
        auto [ok, res] = resolve_chain(start_chain, "Start chain");
        if (!ok)
            return 1;
        start_resolved = res;
        end_resolved = {"END"};
        if (start_resolved.empty()) {
            std::cerr << "Error: --start symbol required for forward trace" << std::endl;
            return 1;
        }
    } else {
        auto [ok1, res1] = resolve_chain(start_chain, "Start chain");
        if (!ok1)
            return 1;
        start_resolved = res1;
        auto [ok2, res2] = resolve_chain(end_chain, "End chain");
        if (!ok2)
            return 1;
        end_resolved = res2;
    }

    // Build description
    auto chain_str = [](const std::vector<std::string> &c) {
        std::string s;
        for (size_t i = 0; i < c.size(); ++i) {
            if (i > 0)
                s += " -> ";
            s += c[i];
        }
        return s;
    };

    std::cout << "Finding paths: " << chain_str(start_resolved);
    if (!end_resolved.empty())
        std::cout << " -> ... -> " << chain_str(end_resolved);
    std::cout << ":\n" << std::endl;

    // Determine query endpoints
    // Find paths from last of start_chain to first of end_chain
    std::string query_start = start_resolved.empty() ? "START" : start_resolved.back();
    std::string query_end = end_resolved.empty() ? "END" : end_resolved.front();

    size_t path_count = 0;
    engine.find_paths(query_start, query_end, [&](const std::vector<std::string> &middle_path) {
        path_count++;
        std::cout << "[" << path_count << "] ";

        // Build full path: start_chain (except last) + middle_path + end_chain (except first)
        std::vector<std::string> full_path;

        // Add start_chain prefix (all but last, since middle_path starts with it)
        for (size_t i = 0; i + 1 < start_resolved.size(); ++i)
            full_path.push_back(start_resolved[i]);

        // Add middle path
        for (const auto &sym : middle_path)
            full_path.push_back(sym);

        // Add end_chain suffix (all but first, since middle_path ends with it)
        for (size_t i = 1; i < end_resolved.size(); ++i)
            full_path.push_back(end_resolved[i]);

        if (show_path) {
            QueryEngine::print_path(full_path, graph, true);
        } else {
            QueryEngine::print_path(full_path);
        }
        return true;
    });

    if (path_count == 0)
        std::cout << "No paths found." << std::endl;
    else
        std::cout << "\nTotal paths found: " << path_count << std::endl;

    return 0;
}

int cmd_list_symbols(const bool nosort) {
    Graph graph;
    if (!load_graph(graph)) {
        return 1;
    }

    const auto &symbol_map = graph.get_symbol_map();

    std::cout << "Symbols in index (" << symbol_map.size() << "):" << std::endl;

    if (nosort) {
        for (const auto &[sym, uid] : symbol_map) {
            std::cout << "  " << sym << std::endl;
        }
    } else {
        std::vector<std::string> symbols;
        symbols.reserve(symbol_map.size());
        for (const auto &[sym, uid] : symbol_map) {
            symbols.push_back(sym);
        }
        std::sort(symbols.begin(), symbols.end());

        for (const auto &sym : symbols) {
            std::cout << "  " << sym << std::endl;
        }
    }

    return 0;
}

int cmd_type(const std::string &symbol, const bool nosort) {
    Graph graph;
    if (!load_graph(graph)) {
        return 1;
    }

    QueryEngine engine(graph);

    if (!validate_symbol(engine, symbol, "Symbol", nosort)) {
        return 1;
    }

    SymbolUID uid = graph.get_uid(symbol);
    SymbolType type = graph.call_graph.get_type(uid);

    std::cout << symbol << ": " << symbol_type_to_string(type) << std::endl;

    return 0;
}

int cmd_data_sources(const std::vector<std::string> &patterns, bool nosort) {
    Graph graph;
    if (!load_graph(graph))
        return 1;

    QueryEngine engine(graph);
    auto matches = engine.find_symbols(patterns);

    if (!nosort)
        std::sort(matches.begin(), matches.end());

    std::set<std::string> all_sources;
    for (const auto &var : matches) {
        for (const auto &src : engine.data_sources(var))
            all_sources.insert(src);
    }

    std::cout << "Data sources (" << all_sources.size() << "):" << std::endl;
    if (all_sources.empty()) {
        std::cout << "  (no sources found)" << std::endl;
    } else {
        for (const auto &src : all_sources)
            std::cout << "  <- " << src << std::endl;
    }
    return 0;
}

int cmd_data_sinks(const std::vector<std::string> &patterns, bool nosort) {
    Graph graph;
    if (!load_graph(graph))
        return 1;

    QueryEngine engine(graph);
    auto matches = engine.find_symbols(patterns);

    if (!nosort)
        std::sort(matches.begin(), matches.end());

    std::set<std::string> all_sinks;
    for (const auto &src : matches) {
        for (const auto &sink : engine.data_sinks(src))
            all_sinks.insert(sink);
    }

    std::cout << "Data sinks (" << all_sinks.size() << "):" << std::endl;
    if (all_sinks.empty()) {
        std::cout << "  (no sinks found)" << std::endl;
    } else {
        for (const auto &sink : all_sinks)
            std::cout << "  -> " << sink << std::endl;
    }
    return 0;
}

int cmd_list_variables(const std::vector<std::string> &patterns, bool nosort) {
    Graph graph;
    if (!load_graph(graph))
        return 1;

    QueryEngine engine(graph);

    // Get variables matching first pattern, then narrow down
    auto vars = engine.variables_in(patterns[0]);
    for (size_t i = 1; i < patterns.size() && !vars.empty(); ++i) {
        std::vector<std::string> filtered;
        for (const auto &v : vars)
            if (v.find(patterns[i]) != std::string::npos)
                filtered.push_back(v);
        vars = std::move(filtered);
    }

    if (!nosort)
        std::sort(vars.begin(), vars.end());

    std::cout << "Variables (" << vars.size() << "):" << std::endl;
    if (vars.empty()) {
        std::cout << "  (none found)" << std::endl;
    } else {
        for (const auto &var : vars)
            std::cout << "  " << var << std::endl;
    }
    return 0;
}

int cmd_find_member(const std::vector<std::string> &patterns, bool nosort) {
    Graph graph;
    if (!load_graph(graph))
        return 1;

    QueryEngine engine(graph);
    std::vector<std::string> matches;

    // First pattern: search in variables
    for (const auto &[symbol, uid] : graph.get_symbol_map()) {
        if (uid == INVALID_UID || !graph.is_variable(uid))
            continue;
        size_t sep = symbol.rfind("::");
        std::string var_part = (sep != std::string::npos) ? symbol.substr(sep + 2) : symbol;
        if (var_part.find(patterns[0]) != std::string::npos ||
            symbol.find(patterns[0]) != std::string::npos)
            matches.push_back(symbol);
    }

    // Narrow down with subsequent patterns
    for (size_t i = 1; i < patterns.size() && !matches.empty(); ++i) {
        std::vector<std::string> filtered;
        for (const auto &sym : matches)
            if (sym.find(patterns[i]) != std::string::npos)
                filtered.push_back(sym);
        matches = std::move(filtered);
    }

    if (!nosort)
        std::sort(matches.begin(), matches.end());

    std::cout << "Assignments (" << matches.size() << "):" << std::endl;
    if (matches.empty()) {
        std::cout << "  (none found)" << std::endl;
    } else {
        for (const auto &var : matches) {
            auto sources = engine.data_sources(var);
            std::cout << "  " << var;
            if (!sources.empty()) {
                std::cout << " <- " << sources[0];
                for (size_t i = 1; i < sources.size(); ++i)
                    std::cout << ", " << sources[i];
            }
            std::cout << std::endl;
        }
    }
    return 0;
}

// Multithreaded grep functionality (old implementation - kept for reference)
static void grep_worker(const std::vector<std::string> &files, size_t start_idx, size_t end_idx,
                        const std::string &pattern, bool use_regex, bool ignore_case,
                        std::vector<GrepMatch> &results, std::mutex &results_mutex) {
    std::vector<GrepMatch> local_results;
    local_results.reserve(100);
    
    std::regex regex_pattern;
    std::regex::flag_type flags = std::regex::ECMAScript;
    if (ignore_case) {
        flags |= std::regex::icase;
    }
    
    if (use_regex) {
        try {
            regex_pattern = std::regex(pattern, flags);
        } catch (const std::regex_error &e) {
            return; // Invalid regex, skip this worker
        }
    }
    
    for (size_t i = start_idx; i < end_idx && i < files.size(); ++i) {
        std::ifstream file(files[i]);
        if (!file.is_open()) continue;
        
        std::string line;
        size_t line_num = 0;
        while (std::getline(file, line)) {
            ++line_num;
            bool match = false;
            
            if (use_regex) {
                match = std::regex_search(line, regex_pattern);
            } else {
                if (ignore_case) {
                    std::string line_lower = line;
                    std::string pattern_lower = pattern;
                    std::transform(line_lower.begin(), line_lower.end(), line_lower.begin(), ::tolower);
                    std::transform(pattern_lower.begin(), pattern_lower.end(), pattern_lower.begin(), ::tolower);
                    match = line_lower.find(pattern_lower) != std::string::npos;
                } else {
                    match = line.find(pattern) != std::string::npos;
                }
            }
            
            if (match) {
                local_results.push_back({files[i], line_num, line});
            }
        }
        
        // Periodically flush to global results to avoid memory buildup
        if (local_results.size() > 1000) {
            std::lock_guard<std::mutex> lock(results_mutex);
            results.insert(results.end(), local_results.begin(), local_results.end());
            local_results.clear();
        }
    }
    
    // Final flush
    if (!local_results.empty()) {
        std::lock_guard<std::mutex> lock(results_mutex);
        results.insert(results.end(), local_results.begin(), local_results.end());
    }
}

int cmd_grep(const std::string &pattern, unsigned int num_threads, bool use_regex, bool ignore_case) {
    Graph graph;
    if (!load_graph(graph)) {
        return 1;
    }
    
    // Collect all unique file paths from the graph (using string pool)
    std::set<std::string> unique_files;
    for (const auto &[file_uid, path_idx] : graph.call_graph.file_uid_to_path_idx) {
        unique_files.insert(graph.call_graph.filepath_pool.get(path_idx));
    }
    
    std::vector<std::string> files(unique_files.begin(), unique_files.end());
    
    if (files.empty()) {
        std::cout << "No files found in index." << std::endl;
        return 0;
    }
    
    std::cout << "Searching " << files.size() << " files for pattern: " << pattern << std::endl;
    
    // Auto-detect thread count if not specified
    if (num_threads == 0) {
        num_threads = std::thread::hardware_concurrency();
        if (num_threads == 0) num_threads = 4;
    }
    
    std::vector<GrepMatch> results;
    std::mutex results_mutex;
    
    // Create worker threads
    std::vector<std::thread> threads;
    size_t files_per_thread = (files.size() + num_threads - 1) / num_threads;
    
    for (unsigned int t = 0; t < num_threads; ++t) {
        size_t start_idx = t * files_per_thread;
        size_t end_idx = std::min(start_idx + files_per_thread, files.size());
        
        if (start_idx >= files.size()) break;
        
        threads.emplace_back(grep_worker, std::cref(files), start_idx, end_idx,
                           std::cref(pattern), use_regex, ignore_case,
                           std::ref(results), std::ref(results_mutex));
    }
    
    // Wait for all threads
    for (auto &t : threads) {
        t.join();
    }
    
    // Display results
    std::cout << "\nFound " << results.size() << " matches:" << std::endl;
    
    if (results.empty()) {
        std::cout << "  (none found)" << std::endl;
    } else {
        for (const auto &match : results) {
            std::cout << match.filepath << ":" << match.line_num << ": " 
                     << match.line << std::endl;
        }
    }
    
    return 0;
}

} // namespace pioneer
