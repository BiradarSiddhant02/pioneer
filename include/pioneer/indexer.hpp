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
#include "parser.hpp"
#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace pioneer {

namespace fs = std::filesystem;

using IndexProgressCallback =
    std::function<void(const std::string &file, size_t current, size_t total)>;

struct IndexerConfig {
    std::string root_path = ".";
    bool verbose = false;
    IndexProgressCallback progress_callback = nullptr;
    unsigned int num_threads = 0; // 0 = auto-detect
    std::vector<std::string> ignore_patterns = {"build",  "node_modules", "__pycache__", ".git",
                                                ".venv",  "venv",         "dist",        "target",
                                                ".cache", "CMakeFiles"};
};

struct FunctionInfo {
    std::string qualified_name;
    std::string file_path;
    std::vector<std::string> param_types;

    FunctionInfo() = default;
    FunctionInfo(FunctionInfo &&) noexcept = default;
    FunctionInfo &operator=(FunctionInfo &&) noexcept = default;
    FunctionInfo(const FunctionInfo &) = default;
    FunctionInfo &operator=(const FunctionInfo &) = default;
};

struct CallInfo {
    std::string caller_name;
    std::string callee_name;

    CallInfo() = default;
    CallInfo(CallInfo &&) noexcept = default;
    CallInfo &operator=(CallInfo &&) noexcept = default;
    CallInfo(const CallInfo &) = default;
    CallInfo &operator=(const CallInfo &) = default;
};

struct VariableInfo {
    std::string qualified_name;
    std::string containing_func;
    std::string value_source;
    bool from_function_call = false;

    VariableInfo() = default;
    VariableInfo(VariableInfo &&) noexcept = default;
    VariableInfo &operator=(VariableInfo &&) noexcept = default;
    VariableInfo(const VariableInfo &) = default;
    VariableInfo &operator=(const VariableInfo &) = default;
};

class Indexer {
public:

    explicit Indexer(const IndexerConfig &config = IndexerConfig{});

    Graph index();
    const std::vector<std::string> &indexed_files() const { return indexed_files_; }

    struct Stats {
        std::atomic<size_t> files_indexed{0};
        std::atomic<size_t> functions_found{0};
        std::atomic<size_t> calls_found{0};
        std::atomic<size_t> variables_found{0};
        std::atomic<size_t> symbols_created{0};
    };
    const Stats &stats() const { return stats_; }

private:

    IndexerConfig config_;
    std::vector<std::string> indexed_files_;
    Stats stats_;
    std::mutex output_mutex_;
    std::mutex graph_mutex_;

    std::vector<fs::path> discover_files();
    bool should_ignore(const fs::path &path) const;
    bool parse_file(const fs::path &filepath, std::vector<FunctionInfo> &functions_out,
                    std::vector<CallInfo> &calls_out, std::vector<VariableInfo> &variables_out);
    void worker_parse_files(const std::vector<fs::path> &files, size_t start_idx, size_t end_idx,
                            std::vector<FunctionInfo> &all_functions,
                            std::vector<CallInfo> &all_calls,
                            std::vector<VariableInfo> &all_variables, std::mutex &functions_mutex,
                            std::mutex &calls_mutex, std::mutex &variables_mutex);
};

} // namespace pioneer
