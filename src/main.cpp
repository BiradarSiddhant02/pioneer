#include <cxxopts.hpp>
#include <iostream>
#include <string>

#include "pioneer/graph.hpp"
#include "pioneer/indexer.hpp"
#include "pioneer/query.hpp"
#include "pioneer/types.hpp"

using namespace pioneer;

constexpr const char *VERSION = "1.2.0";
constexpr const char *INDEX_FILE = ".pioneer.json";

void print_banner() {
    std::cout << R"(
  ____  _ ...........................                         
 |  _ \(_) ___  _ __   ___  ___ _ __ 
 | |_) | |/ _ \| '_ \ / _ \/ _ \ '__|
 |  __/| | (_) | | | |  __/  __/ |...   
 |_|...|_|\___/|_| |_|\___|\___|_|...   
                                     
)" << "  Call Graph Analyzer v"
              << VERSION << "\n"
              << std::endl;
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

// Helper function to load the graph from index file
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

bool validate_symbol(const QueryEngine &engine, const std::string &symbol,
                     const std::string &label, bool nosort) {
    if (engine.has_symbol(symbol)) return true;

    std::cerr << "Error: " << label << " not found: " << symbol << std::endl;
    auto matches = engine.find_symbols(symbol);
    if (!nosort) std::sort(matches.begin(), matches.end());

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

int cmd_search(const std::vector<std::string> &patterns, const bool nosort) {
    Graph graph;
    if (!load_graph(graph)) {
        return 1;
    }

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
            std::cout << "  " << sym << std::endl;
        }
    }

    return 0;
}

int cmd_query(const std::vector<std::string> &start_chain, const std::vector<std::string> &end_chain,
              bool backtrace, bool pattern_match, bool nosort) {
    Graph graph;
    if (!load_graph(graph)) return 1;

    QueryEngine engine(graph);

    // Handle special cases
    bool is_backtrace = backtrace || (!start_chain.empty() && start_chain[0] == "START");
    bool is_forward = !end_chain.empty() && end_chain[0] == "END";

    // Resolve patterns if needed and validate all symbols
    auto resolve_chain = [&](const std::vector<std::string> &chain, const std::string &label) 
            -> std::pair<bool, std::vector<std::string>> {
        std::vector<std::string> resolved;
        for (const auto &sym : chain) {
            if (sym == "START" || sym == "END") {
                resolved.push_back(sym);
                continue;
            }
            std::string actual = sym;
            if (pattern_match) {
                auto matches = engine.find_symbols(sym);
                if (!nosort) std::sort(matches.begin(), matches.end());
                if (matches.empty()) {
                    std::cerr << "Error: No symbols matching pattern: " << sym << std::endl;
                    return {false, {}};
                }
                if (matches.size() > 1) {
                    std::cout << "Pattern '" << sym << "' matches:" << std::endl;
                    for (size_t i = 0; i < std::min(matches.size(), size_t(5)); ++i)
                        std::cout << "  [" << (i+1) << "] " << matches[i] << std::endl;
                    std::cout << "Using: " << matches[0] << std::endl;
                }
                actual = matches[0];
            }
            if (!validate_symbol(engine, actual, label, nosort)) return {false, {}};
            resolved.push_back(actual);
        }
        return {true, resolved};
    };

    std::vector<std::string> start_resolved, end_resolved;

    if (is_backtrace) {
        start_resolved = {"START"};
        auto [ok, res] = resolve_chain(end_chain, "End chain");
        if (!ok) return 1;
        end_resolved = res;
        if (end_resolved.empty()) {
            std::cerr << "Error: --end symbol required for backtrace" << std::endl;
            return 1;
        }
    } else if (is_forward) {
        auto [ok, res] = resolve_chain(start_chain, "Start chain");
        if (!ok) return 1;
        start_resolved = res;
        end_resolved = {"END"};
        if (start_resolved.empty()) {
            std::cerr << "Error: --start symbol required for forward trace" << std::endl;
            return 1;
        }
    } else {
        auto [ok1, res1] = resolve_chain(start_chain, "Start chain");
        if (!ok1) return 1;
        start_resolved = res1;
        auto [ok2, res2] = resolve_chain(end_chain, "End chain");
        if (!ok2) return 1;
        end_resolved = res2;
    }

    // Build description
    auto chain_str = [](const std::vector<std::string> &c) {
        std::string s;
        for (size_t i = 0; i < c.size(); ++i) {
            if (i > 0) s += " -> ";
            s += c[i];
        }
        return s;
    };

    std::cout << "Finding paths: " << chain_str(start_resolved);
    if (!end_resolved.empty()) std::cout << " -> ... -> " << chain_str(end_resolved);
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

        QueryEngine::print_path(full_path);
        return true;
    });

    if (path_count == 0) std::cout << "No paths found." << std::endl;
    else std::cout << "\nTotal paths found: " << path_count << std::endl;

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

int cmd_data_sources(const std::string &variable, const bool nosort) {
    Graph graph;
    if (!load_graph(graph)) {
        return 1;
    }

    QueryEngine engine(graph);

    if (!validate_symbol(engine, variable, "Variable", nosort)) {
        return 1;
    }

    auto sources = engine.data_sources(variable);

    if (!nosort) {
        std::sort(sources.begin(), sources.end());
    }

    std::cout << "Data sources for " << variable << ":" << std::endl;
    if (sources.empty()) {
        std::cout << "  (no sources found)" << std::endl;
    } else {
        for (const auto &src : sources) {
            std::cout << "  <- " << src << std::endl;
        }
    }

    return 0;
}

int cmd_data_sinks(const std::string &source, const bool nosort) {
    Graph graph;
    if (!load_graph(graph)) {
        return 1;
    }

    QueryEngine engine(graph);

    if (!validate_symbol(engine, source, "Symbol", nosort)) {
        return 1;
    }

    auto sinks = engine.data_sinks(source);

    if (!nosort) {
        std::sort(sinks.begin(), sinks.end());
    }

    std::cout << "Data flows from " << source << " to:" << std::endl;
    if (sinks.empty()) {
        std::cout << "  (no sinks found)" << std::endl;
    } else {
        for (const auto &sink : sinks) {
            std::cout << "  -> " << sink << std::endl;
        }
    }

    return 0;
}

int cmd_list_variables(const std::string &func_pattern, const bool nosort) {
    Graph graph;
    if (!load_graph(graph)) {
        return 1;
    }

    QueryEngine engine(graph);
    auto vars = engine.variables_in(func_pattern);

    if (!nosort) {
        std::sort(vars.begin(), vars.end());
    }

    std::cout << "Variables matching '" << func_pattern << "':" << std::endl;
    if (vars.empty()) {
        std::cout << "  (none found)" << std::endl;
    } else {
        for (const auto &var : vars) {
            std::cout << "  " << var << std::endl;
        }
    }

    return 0;
}

int cmd_find_member(const std::string &member_pattern, const bool nosort) {
    Graph graph;
    if (!load_graph(graph)) {
        return 1;
    }

    QueryEngine engine(graph);

    std::vector<std::string> matches;
    matches.reserve(256);

    for (const auto &[symbol, uid] : graph.get_symbol_map()) {
        if (uid == INVALID_UID)
            continue;
        if (!graph.is_variable(uid))
            continue;

        size_t last_sep = symbol.rfind("::");
        std::string var_part =
            (last_sep != std::string::npos) ? symbol.substr(last_sep + 2) : symbol;

        if (var_part.find(member_pattern) != std::string::npos ||
            symbol.find(member_pattern) != std::string::npos) {
            matches.push_back(symbol);
        }
    }

    if (!nosort) {
        std::sort(matches.begin(), matches.end());
    }

    std::cout << "Assignments to '" << member_pattern << "':" << std::endl;
    if (matches.empty()) {
        std::cout << "  (none found)" << std::endl;
    } else {
        for (const auto &var : matches) {
            auto sources = engine.data_sources(var);
            std::cout << "  " << var;
            if (!sources.empty()) {
                std::cout << " <- " << sources[0];
                for (size_t i = 1; i < sources.size(); ++i) {
                    std::cout << ", " << sources[i];
                }
            }
            std::cout << std::endl;
        }
    }

    return 0;
}

int main(int argc, char *argv[]) {
    cxxopts::Options options(
        "pioneer", "Call Graph Analyzer - Build and query call graphs for Python, C, and C++ code");

    auto opts = options.add_options();
    opts("h,help", "Print help");
    opts("v,version", "Print version");
    opts("index", "Build call graph index for current directory");
    opts("j,jobs", "Number of threads for indexing (0 = auto)",
         cxxopts::value<unsigned int>()->default_value("0"));
    opts("s,start", "Start symbol chain (comma-separated or repeat -s)",
         cxxopts::value<std::vector<std::string>>());
    opts("e,end", "End symbol chain (comma-separated, use END for forward trace)",
         cxxopts::value<std::vector<std::string>>());
    opts("b,backtrace", "Enable backtrace mode (find all callers)");

    opts("l,list", "List all indexed symbols");
    opts("data-sources", "Find what a variable is assigned from",
         cxxopts::value<std::string>()->default_value(""));
    opts("data-sinks", "Find what variables a function/symbol flows to",
         cxxopts::value<std::string>()->default_value(""));
    opts("vars", "List variables in a function (pattern match)",
         cxxopts::value<std::string>()->default_value(""));
    opts("member", "Find all assignments to a struct member pattern",
         cxxopts::value<std::string>()->default_value(""));
    opts("search", "Search for symbols matching a pattern",
         cxxopts::value<std::vector<std::string>>());
    opts("p,pattern", "Enable pattern matching for --start and --end (substring match)");
    opts("nosort", "Do not sort the list of symbols");
    opts("type", "Prints type of symbol (function/variable)",
         cxxopts::value<std::string>()->default_value(""));

    try {
        auto result = options.parse(argc, argv);

        if (result.count("help")) {
            print_banner();
            std::cout << options.help() << std::endl;
            std::cout << "Examples:" << std::endl;
            std::cout << "  pioneer --index                    Build index for current directory"
                      << std::endl;
            std::cout << "  pioneer --index -j 8               Build index using 8 threads"
                      << std::endl;
            std::cout << "  pioneer --start foo --end bar      Find paths from foo to bar"
                      << std::endl;
            std::cout << "  pioneer --start foo --end END      Find all call paths from foo"
                      << std::endl;
            std::cout << "  pioneer --start START --end bar    Backtrace: find all callers of bar"
                      << std::endl;
            std::cout << "  pioneer --backtrace --end bar      Same as above (backtrace mode)"
                      << std::endl;
            std::cout << "  pioneer --list                     List all indexed symbols"
                      << std::endl;
            std::cout << "  pioneer --search 'init'            Search for symbols matching 'init'"
                      << std::endl;
            std::cout << "  pioneer -p --start foo --end bar   Pattern match start/end symbols"
                      << std::endl;
            std::cout << "\nData Flow Queries (v1.1.0):" << std::endl;
            std::cout
                << "  pioneer --data-sources 'func::x'   Find what variable x is assigned from"
                << std::endl;
            std::cout
                << "  pioneer --data-sinks 'get_data'    Find variables assigned from get_data()"
                << std::endl;
            std::cout << "  pioneer --vars 'MyClass'           List all variables in functions "
                         "matching 'MyClass'"
                      << std::endl;
            std::cout << "  pioneer --member 'dev->field'      Find ALL assignments to dev->field"
                      << std::endl;
            return 0;
        }

        if (result.count("version")) {
            std::cout << "pioneer v" << VERSION << std::endl;
            return 0;
        }

        bool nosort = result.count("nosort") > 0;

        if (result.count("index")) {
            unsigned int num_threads = result["jobs"].as<unsigned int>();
            return cmd_index(num_threads);
        }

        if (result.count("list")) {
            return cmd_list_symbols(nosort);
        }

        if (result.count("type")) {
            std::string type_symbol = result["type"].as<std::string>();
            if (!type_symbol.empty()) {
                return cmd_type(type_symbol, nosort);
            }
        }

        if (result.count("search")) {
            auto search_patterns = result["search"].as<std::vector<std::string>>();
            if (!search_patterns.empty()) {
                return cmd_search(search_patterns, nosort);
            }
        }

        std::string data_sources_var = result["data-sources"].as<std::string>();
        if (!data_sources_var.empty()) {
            return cmd_data_sources(data_sources_var, nosort);
        }

        std::string data_sinks_src = result["data-sinks"].as<std::string>();
        if (!data_sinks_src.empty()) {
            return cmd_data_sinks(data_sinks_src, nosort);
        }

        std::string vars_pattern = result["vars"].as<std::string>();
        if (!vars_pattern.empty()) {
            return cmd_list_variables(vars_pattern, nosort);
        }

        std::string member_pattern = result["member"].as<std::string>();
        if (!member_pattern.empty()) {
            return cmd_find_member(member_pattern, nosort);
        }

        std::vector<std::string> start_chain, end_chain;
        if (result.count("start")) start_chain = result["start"].as<std::vector<std::string>>();
        if (result.count("end")) end_chain = result["end"].as<std::vector<std::string>>();
        bool backtrace = result.count("backtrace") > 0;
        bool pattern_match = result.count("pattern") > 0;

        if (!start_chain.empty() || !end_chain.empty() || backtrace) {
            return cmd_query(start_chain, end_chain, backtrace, pattern_match, nosort);
        }

        print_banner();
        std::cout << options.help() << std::endl;
        return 0;

    } catch (const cxxopts::exceptions::exception &e) {
        std::cerr << "Error parsing options: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
