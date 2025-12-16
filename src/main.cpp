#include <iostream>
#include <string>
#include <cxxopts.hpp>

#include "pioneer/types.hpp"
#include "pioneer/graph.hpp"
#include "pioneer/indexer.hpp"
#include "pioneer/query.hpp"

using namespace pioneer;

constexpr const char* VERSION = "1.0.0";
constexpr const char* INDEX_FILE = ".pioneer.json";

void print_banner() {
    std::cout << R"(
  ____  _                           
 |  _ \(_) ___  _ __   ___  ___ _ __ 
 | |_) | |/ _ \| '_ \ / _ \/ _ \ '__|
 |  __/| | (_) | | | |  __/  __/ |   
 |_|   |_|\___/|_| |_|\___|\___|_|   
                                     
)" << "  Call Graph Analyzer v" << VERSION << "\n" << std::endl;
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
    } catch (const std::exception& e) {
        std::cerr << "Error saving index: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}

int cmd_query(const std::string& start, const std::string& end, bool backtrace) {
    // Load index
    Graph graph;
    try {
        graph = Graph::load(INDEX_FILE);
    } catch (const std::exception& e) {
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
    engine.find_paths(actual_start, actual_end, [&](const std::vector<std::string>& path) {
        path_count++;
        std::cout << "[" << path_count << "] ";
        QueryEngine::print_path(path);
        return true;  // Continue searching
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
    } catch (const std::exception& e) {
        std::cerr << "Error loading index: " << e.what() << std::endl;
        std::cerr << "Please run 'pioneer --index' first." << std::endl;
        return 1;
    }
    
    auto symbols = graph.get_all_symbols();
    std::cout << "Symbols in index (" << symbols.size() << "):" << std::endl;
    for (const auto& sym : symbols) {
        std::cout << "  " << sym << std::endl;
    }
    
    return 0;
}

int main(int argc, char* argv[]) {
    cxxopts::Options options("pioneer", "Call Graph Analyzer - Build and query call graphs for Python, C, and C++ code");
    
    options.add_options()
        ("h,help", "Print help")
        ("v,version", "Print version")
        ("index", "Build call graph index for current directory")
        ("j,jobs", "Number of threads for indexing (0 = auto)", cxxopts::value<unsigned int>()->default_value("0"))
        ("s,start", "Start symbol (use START for backtrace)", cxxopts::value<std::string>()->default_value(""))
        ("e,end", "End symbol (use END to find all paths)", cxxopts::value<std::string>()->default_value(""))
        ("b,backtrace", "Enable backtrace mode (find all callers)")
        ("l,list", "List all indexed symbols")
    ;
    
    try {
        auto result = options.parse(argc, argv);
        
        if (result.count("help")) {
            print_banner();
            std::cout << options.help() << std::endl;
            std::cout << "Examples:" << std::endl;
            std::cout << "  pioneer --index                    Build index for current directory" << std::endl;
            std::cout << "  pioneer --index -j 8               Build index using 8 threads" << std::endl;
            std::cout << "  pioneer --start foo --end bar      Find paths from foo to bar" << std::endl;
            std::cout << "  pioneer --start foo --end END      Find all call paths from foo" << std::endl;
            std::cout << "  pioneer --start START --end bar    Backtrace: find all callers of bar" << std::endl;
            std::cout << "  pioneer --backtrace --end bar      Same as above (backtrace mode)" << std::endl;
            std::cout << "  pioneer --list                     List all indexed symbols" << std::endl;
            return 0;
        }
        
        if (result.count("version")) {
            std::cout << "pioneer v" << VERSION << std::endl;
            return 0;
        }
        
        if (result.count("index")) {
            print_banner();
            unsigned int num_threads = result["jobs"].as<unsigned int>();
            return cmd_index(num_threads);
        }
        
        if (result.count("list")) {
            return cmd_list_symbols();
        }
        
        std::string start = result["start"].as<std::string>();
        std::string end = result["end"].as<std::string>();
        bool backtrace = result.count("backtrace") > 0;
        
        if (!start.empty() || !end.empty() || backtrace) {
            print_banner();
            return cmd_query(start, end, backtrace);
        }
        
        // No command specified
        print_banner();
        std::cout << options.help() << std::endl;
        return 0;
        
    } catch (const cxxopts::exceptions::exception& e) {
        std::cerr << "Error parsing options: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
