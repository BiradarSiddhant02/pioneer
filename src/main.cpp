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

#include <cxxopts.hpp>
#include <iostream>

#include "pioneer/commands.hpp"
#include "pioneer/version.hpp"

using namespace pioneer;

void print_banner() {
    std::cout << R"(
  ____  _                           
 |  _ \(_) ___  _ __   ___  ___ _ __ 
 | |_) | |/ _ \| '_ \ / _ \/ _ \ '__|
 |  __/| | (_) | | | |  __/  __/ |   
 |_|   |_|\___/|_| |_|\___|\___|_|   
                                     
)" << "  Call Graph Analyzer v"
              << VERSION_STRING << "\n"
              << std::endl;
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
    opts("s,start", "Start symbol chain (comma-separated, no spaces)",
         cxxopts::value<std::vector<std::string>>());
    opts("e,end", "End symbol chain (comma-separated, no spaces)",
         cxxopts::value<std::vector<std::string>>());
    opts("b,backtrace", "Enable backtrace mode (find all callers)");

    opts("l,list", "List all indexed symbols");
    opts("data-sources", "Find data sources (comma-separated, no spaces)",
         cxxopts::value<std::vector<std::string>>());
    opts("data-sinks", "Find data sinks (comma-separated, no spaces)",
         cxxopts::value<std::vector<std::string>>());
    opts("vars", "List variables (comma-separated, no spaces)",
         cxxopts::value<std::vector<std::string>>());
    opts("member", "Find member assignments (comma-separated, no spaces)",
         cxxopts::value<std::vector<std::string>>());
    opts("search", "Search symbols (comma-separated, no spaces)",
         cxxopts::value<std::vector<std::string>>());
    opts("grep", "Search pattern in source files", cxxopts::value<std::string>());
    opts("regex", "Use regex for grep (instead of plain text)");
    opts("ignore-case", "Case-insensitive grep");
    opts("path", "Show file paths for symbols");
    opts("p,pattern", "Enable pattern matching for --start and --end");
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
            std::cout << "  pioneer --search 'init' --path     Search with file paths" << std::endl;
            std::cout << "  pioneer -p --start foo --end bar   Pattern match start/end symbols"
                      << std::endl;
            std::cout << "  pioneer --start foo --end bar --path   Show file paths in trace"
                      << std::endl;
            std::cout << "  pioneer --grep 'pattern'           Search pattern in all indexed files"
                      << std::endl;
            std::cout << "  pioneer --grep 'pattern' -j 8      Use 8 threads for grep" << std::endl;
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
            std::cout << "pioneer v" << VERSION_STRING << std::endl;
            return 0;
        }

        bool nosort = result.count("nosort") > 0;
        bool show_path = result.count("path") > 0;

        if (result.count("index")) {
            unsigned int num_threads = result["jobs"].as<unsigned int>();
            return cmd_index(num_threads);
        }

        if (result.count("list")) {
            return cmd_list_symbols_streaming(nosort);
        }

        if (result.count("type")) {
            std::string type_symbol = result["type"].as<std::string>();
            if (!type_symbol.empty()) {
                return cmd_type(type_symbol, nosort);
            }
        }

        if (result.count("search")) {
            auto patterns = result["search"].as<std::vector<std::string>>();
            if (!patterns.empty()) {
                // Use streaming search when show_path not needed (much lower memory)
                if (!show_path) {
                    return cmd_search_streaming(patterns, nosort);
                }
                // Fall back to full load for --show-path
                Graph graph;
                if (!load_graph(graph, LoadMode::WithPaths)) {
                    return 1;
                }
                return cmd_search(patterns, nosort, show_path, graph);
            }
        }

        if (result.count("grep")) {
            std::string pattern = result["grep"].as<std::string>();
            if (!pattern.empty()) {
                unsigned int num_threads = result["jobs"].as<unsigned int>();
                bool use_regex = result.count("regex") > 0;
                bool ignore_case = result.count("ignore-case") > 0;
                return cmd_grep_streaming(pattern, num_threads, use_regex, ignore_case);
            }
        }

        if (result.count("data-sources")) {
            auto patterns = result["data-sources"].as<std::vector<std::string>>();
            if (!patterns.empty())
                return cmd_data_sources(patterns, nosort);
        }

        if (result.count("data-sinks")) {
            auto patterns = result["data-sinks"].as<std::vector<std::string>>();
            if (!patterns.empty())
                return cmd_data_sinks(patterns, nosort);
        }

        if (result.count("vars")) {
            auto patterns = result["vars"].as<std::vector<std::string>>();
            if (!patterns.empty())
                return cmd_list_variables(patterns, nosort);
        }

        if (result.count("member")) {
            auto patterns = result["member"].as<std::vector<std::string>>();
            if (!patterns.empty())
                return cmd_find_member(patterns, nosort);
        }

        std::vector<std::string> start_chain, end_chain;
        if (result.count("start"))
            start_chain = result["start"].as<std::vector<std::string>>();
        if (result.count("end"))
            end_chain = result["end"].as<std::vector<std::string>>();
        bool backtrace = result.count("backtrace") > 0;
        bool pattern_match = result.count("pattern") > 0;

        if (!start_chain.empty() || !end_chain.empty() || backtrace) {
            return cmd_query(start_chain, end_chain, backtrace, pattern_match, nosort, show_path);
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
