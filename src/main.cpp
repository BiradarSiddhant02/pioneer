#include <cxxopts.hpp>
#include <iostream>
#include <string>

#include "pioneer/graph.hpp"
#include "pioneer/indexer.hpp"
#include "pioneer/query.hpp"
#include "pioneer/types.hpp"

using namespace pioneer;

constexpr const char *VERSION = "1.1.0";
constexpr const char *INDEX_FILE = ".pioneer.json";

void print_banner() {
    std::cout << R"(
  ____  _                           
 |  _ \(_) ___  _ __   ___  ___ _ __ 
 | |_) | |/ _ \| '_ \ / _ \/ _ \ '__|
 |  __/| | (_) | | | |  __/  __/ |   
 |_|   |_|\___/|_| |_|\___|\___|_|   
                                     
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

    // Save to file
    try {
        graph.save(INDEX_FILE);
        std::cout << "\nIndex saved to: " << INDEX_FILE << std::endl;
    } catch (const std::exception &e) {
        std::cerr << "Error saving index: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

int cmd_search(const std::string &pattern) {
    Graph graph;
    try {
        graph = Graph::load(INDEX_FILE);
    } catch (const std::exception &e) {
        std::cerr << "Error loading index: " << e.what() << std::endl;
        std::cerr << "Please run 'pioneer --index' first." << std::endl;
        return 1;
    }

    QueryEngine engine(graph);
    auto matches = engine.find_symbols(pattern);

    std::cout << "Symbols matching '" << pattern << "' (" << matches.size() << "):" << std::endl;
    if (matches.empty()) {
        std::cout << "  (none found)" << std::endl;
    } else {
        for (const auto &sym : matches) {
            std::cout << "  " << sym << std::endl;
        }
    }

    return 0;
}

int cmd_query(const std::string &start, const std::string &end, bool backtrace,
              bool pattern_match) {
    // Load index
    Graph graph;
    try {
        graph = Graph::load(INDEX_FILE);
    } catch (const std::exception &e) {
        std::cerr << "Error loading index: " << e.what() << std::endl;
        std::cerr << "Please run 'pioneer --index' first." << std::endl;
        return 1;
    }

    QueryEngine engine(graph);

    // Determine query mode
    std::string actual_start = start;
    std::string actual_end = end;

    // Backtrace mode: either explicit flag or start == "START"
    if (backtrace || start == "START") {
        actual_start = "START";
        if (actual_end.empty()) {
            std::cerr << "Error: --end symbol required for backtrace" << std::endl;
            return 1;
        }
    }

    // Forward trace: end == "END"
    if (actual_end == "END" && actual_start != "START") {
        // Forward trace from start to all endpoints
    }

    // Pattern matching mode: resolve patterns to actual symbols
    if (pattern_match && actual_start != "START") {
        auto matches = engine.find_symbols(actual_start);
        if (matches.empty()) {
            std::cerr << "Error: No symbols matching pattern: " << actual_start << std::endl;
            return 1;
        }
        if (matches.size() > 1) {
            std::cout << "Start pattern '" << actual_start
                      << "' matches multiple symbols:" << std::endl;
            for (size_t i = 0; i < matches.size(); ++i) {
                std::cout << "  [" << (i + 1) << "] " << matches[i] << std::endl;
            }
            std::cout << "Using first match: " << matches[0] << std::endl;
        }
        actual_start = matches[0];
    }

    if (pattern_match && actual_end != "END") {
        auto matches = engine.find_symbols(actual_end);
        if (matches.empty()) {
            std::cerr << "Error: No symbols matching pattern: " << actual_end << std::endl;
            return 1;
        }
        if (matches.size() > 1) {
            std::cout << "End pattern '" << actual_end
                      << "' matches multiple symbols:" << std::endl;
            for (size_t i = 0; i < matches.size(); ++i) {
                std::cout << "  [" << (i + 1) << "] " << matches[i] << std::endl;
            }
            std::cout << "Using first match: " << matches[0] << std::endl;
        }
        actual_end = matches[0];
    }

    // Validate symbols
    if (actual_start != "START" && !engine.has_symbol(actual_start)) {
        std::cerr << "Error: Start symbol not found: " << actual_start << std::endl;

        // Suggest similar symbols
        auto matches = engine.find_symbols(actual_start);
        if (!matches.empty()) {
            std::cerr << "Did you mean one of these?" << std::endl;
            for (size_t i = 0; i < std::min(matches.size(), size_t(5)); ++i) {
                std::cerr << "  " << matches[i] << std::endl;
            }
        }
        return 1;
    }

    if (actual_end != "END" && !engine.has_symbol(actual_end)) {
        std::cerr << "Error: End symbol not found: " << actual_end << std::endl;

        // Suggest similar symbols
        auto matches = engine.find_symbols(actual_end);
        if (!matches.empty()) {
            std::cerr << "Did you mean one of these?" << std::endl;
            for (size_t i = 0; i < std::min(matches.size(), size_t(5)); ++i) {
                std::cerr << "  " << matches[i] << std::endl;
            }
        }
        return 1;
    }

    // Execute query
    std::cout << "Finding paths";
    if (actual_start == "START") {
        std::cout << " (backtrace to " << actual_end << ")";
    } else if (actual_end == "END") {
        std::cout << " (forward from " << actual_start << ")";
    } else {
        std::cout << " from " << actual_start << " to " << actual_end;
    }
    std::cout << ":\n" << std::endl;

    size_t path_count = 0;
    engine.find_paths(actual_start, actual_end, [&](const std::vector<std::string> &path) {
        path_count++;
        std::cout << "[" << path_count << "] ";
        QueryEngine::print_path(path);
        return true; // Continue searching
    });

    if (path_count == 0) {
        std::cout << "No paths found." << std::endl;
    } else {
        std::cout << "\nTotal paths found: " << path_count << std::endl;
    }

    return 0;
}

int cmd_list_symbols() {
    // Load index
    Graph graph;
    try {
        graph = Graph::load(INDEX_FILE);
    } catch (const std::exception &e) {
        std::cerr << "Error loading index: " << e.what() << std::endl;
        std::cerr << "Please run 'pioneer --index' first." << std::endl;
        return 1;
    }

    auto symbols = graph.get_all_symbols();
    std::cout << "Symbols in index (" << symbols.size() << "):" << std::endl;
    for (const auto &sym : symbols) {
        std::cout << "  " << sym << std::endl;
    }

    return 0;
}

int cmd_data_sources(const std::string &variable) {
    Graph graph;
    try {
        graph = Graph::load(INDEX_FILE);
    } catch (const std::exception &e) {
        std::cerr << "Error loading index: " << e.what() << std::endl;
        std::cerr << "Please run 'pioneer --index' first." << std::endl;
        return 1;
    }

    QueryEngine engine(graph);

    if (!engine.has_symbol(variable)) {
        std::cerr << "Error: Variable not found: " << variable << std::endl;
        auto matches = engine.find_symbols(variable);
        if (!matches.empty()) {
            std::cerr << "Did you mean one of these?" << std::endl;
            for (size_t i = 0; i < std::min(matches.size(), size_t(5)); ++i) {
                std::cerr << "  " << matches[i] << std::endl;
            }
        }
        return 1;
    }

    auto sources = engine.data_sources(variable);
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

int cmd_data_sinks(const std::string &source) {
    Graph graph;
    try {
        graph = Graph::load(INDEX_FILE);
    } catch (const std::exception &e) {
        std::cerr << "Error loading index: " << e.what() << std::endl;
        std::cerr << "Please run 'pioneer --index' first." << std::endl;
        return 1;
    }

    QueryEngine engine(graph);

    if (!engine.has_symbol(source)) {
        std::cerr << "Error: Symbol not found: " << source << std::endl;
        auto matches = engine.find_symbols(source);
        if (!matches.empty()) {
            std::cerr << "Did you mean one of these?" << std::endl;
            for (size_t i = 0; i < std::min(matches.size(), size_t(5)); ++i) {
                std::cerr << "  " << matches[i] << std::endl;
            }
        }
        return 1;
    }

    auto sinks = engine.data_sinks(source);
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

int cmd_list_variables(const std::string &func_pattern) {
    Graph graph;
    try {
        graph = Graph::load(INDEX_FILE);
    } catch (const std::exception &e) {
        std::cerr << "Error loading index: " << e.what() << std::endl;
        std::cerr << "Please run 'pioneer --index' first." << std::endl;
        return 1;
    }

    QueryEngine engine(graph);
    auto vars = engine.variables_in(func_pattern);

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

int cmd_find_member(const std::string &member_pattern) {
    Graph graph;
    try {
        graph = Graph::load(INDEX_FILE);
    } catch (const std::exception &e) {
        std::cerr << "Error loading index: " << e.what() << std::endl;
        std::cerr << "Please run 'pioneer --index' first." << std::endl;
        return 1;
    }

    QueryEngine engine(graph);

    // Find all variables that contain the member pattern
    // e.g., searching for "gib_dev->gctx" matches "func::gib_dev->gctx"
    std::vector<std::string> matches;
    for (const auto &symbol : graph.get_all_symbols()) {
        SymbolUID uid = graph.get_uid(symbol);
        if (uid == INVALID_UID)
            continue;
        if (!graph.is_variable(uid))
            continue;

        // Check if the variable name contains the member pattern
        // Also check if the last part of the qualified name matches
        size_t last_sep = symbol.rfind("::");
        std::string var_part =
            (last_sep != std::string::npos) ? symbol.substr(last_sep + 2) : symbol;

        if (var_part.find(member_pattern) != std::string::npos ||
            symbol.find(member_pattern) != std::string::npos) {
            matches.push_back(symbol);
        }
    }

    std::sort(matches.begin(), matches.end());

    std::cout << "Assignments to '" << member_pattern << "':" << std::endl;
    if (matches.empty()) {
        std::cout << "  (none found)" << std::endl;
    } else {
        for (const auto &var : matches) {
            // Show the function and the source value
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
    opts("s,start", "Start symbol (use START for backtrace)",
         cxxopts::value<std::string>()->default_value(""));
    opts("e,end", "End symbol (use END to find all paths)",
         cxxopts::value<std::string>()->default_value(""));
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
         cxxopts::value<std::string>()->default_value(""));
    opts("p,pattern", "Enable pattern matching for --start and --end (substring match)");

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

        if (result.count("index")) {
            unsigned int num_threads = result["jobs"].as<unsigned int>();
            return cmd_index(num_threads);
        }

        if (result.count("list")) {
            return cmd_list_symbols();
        }

        // Search for symbols by pattern
        std::string search_pattern = result["search"].as<std::string>();
        if (!search_pattern.empty()) {
            return cmd_search(search_pattern);
        }

        // Data flow queries (v1.1.0)
        std::string data_sources_var = result["data-sources"].as<std::string>();
        if (!data_sources_var.empty()) {
            return cmd_data_sources(data_sources_var);
        }

        std::string data_sinks_src = result["data-sinks"].as<std::string>();
        if (!data_sinks_src.empty()) {
            return cmd_data_sinks(data_sinks_src);
        }

        std::string vars_pattern = result["vars"].as<std::string>();
        if (!vars_pattern.empty()) {
            return cmd_list_variables(vars_pattern);
        }

        std::string member_pattern = result["member"].as<std::string>();
        if (!member_pattern.empty()) {
            return cmd_find_member(member_pattern);
        }

        std::string start = result["start"].as<std::string>();
        std::string end = result["end"].as<std::string>();
        bool backtrace = result.count("backtrace") > 0;
        bool pattern_match = result.count("pattern") > 0;

        if (!start.empty() || !end.empty() || backtrace) {
            return cmd_query(start, end, backtrace, pattern_match);
        }

        // No command specified
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
