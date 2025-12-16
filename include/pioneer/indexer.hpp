#pragma once

#include "graph.hpp"
#include "parser.hpp"
#include <string>
#include <vector>
#include <filesystem>
#include <functional>
#include <atomic>
#include <mutex>
#include <thread>

namespace pioneer {

namespace fs = std::filesystem;

// Callback for progress reporting
using IndexProgressCallback = std::function<void(const std::string& file, size_t current, size_t total)>;

// Indexer configuration
struct IndexerConfig {
    std::string root_path = ".";
    bool verbose = false;
    IndexProgressCallback progress_callback = nullptr;
    
    // Threading config
    unsigned int num_threads = 0;  // 0 = auto-detect
    
    // File patterns to ignore
    std::vector<std::string> ignore_patterns = {
        "build", "node_modules", "__pycache__", ".git", ".venv", "venv",
        "dist", "target", ".cache", "CMakeFiles"
    };
};

// Lightweight function info for Phase 1 (minimal memory)
struct FunctionInfo {
    std::string qualified_name;     // Base qualified name
    std::string file_path;
    std::vector<std::string> param_types;
};

// Call info for Phase 2
struct CallInfo {
    std::string caller_name;
    std::string callee_name;
};

class Indexer {
public:
    explicit Indexer(const IndexerConfig& config = IndexerConfig{});
    
    // Index the codebase and return the call graph
    Graph index();
    
    // Get list of indexed files
    const std::vector<std::string>& indexed_files() const { return indexed_files_; }
    
    // Get statistics
    struct Stats {
        std::atomic<size_t> files_indexed{0};
        std::atomic<size_t> functions_found{0};
        std::atomic<size_t> calls_found{0};
        std::atomic<size_t> symbols_created{0};
    };
    const Stats& stats() const { return stats_; }
    
private:
    IndexerConfig config_;
    std::vector<std::string> indexed_files_;
    Stats stats_;
    
    // Thread synchronization
    std::mutex output_mutex_;
    std::mutex graph_mutex_;
    
    // Discover all source files
    std::vector<fs::path> discover_files();
    
    // Check if path should be ignored
    bool should_ignore(const fs::path& path) const;
    
    // Parse a single file (thread-safe)
    bool parse_file(const fs::path& filepath,
                    std::vector<FunctionInfo>& functions_out,
                    std::vector<CallInfo>& calls_out);
    
    // Worker function for thread pool
    void worker_parse_files(const std::vector<fs::path>& files,
                            size_t start_idx, size_t end_idx,
                            std::vector<FunctionInfo>& all_functions,
                            std::vector<CallInfo>& all_calls,
                            std::mutex& functions_mutex,
                            std::mutex& calls_mutex);
};

} // namespace pioneer
