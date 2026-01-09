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

#include "pioneer/indexer.hpp"
#include <algorithm>
#include <iostream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace pioneer {

Indexer::Indexer(const IndexerConfig &config) : config_(config) {
    if (config_.num_threads == 0) {
        config_.num_threads = std::thread::hardware_concurrency();
        if (config_.num_threads == 0)
            config_.num_threads = 4;
    }
}

bool Indexer::should_ignore(const fs::path &path) const {
    for (const auto &pattern : config_.ignore_patterns) {
        for (const auto &component : path) {
            if (component.string() == pattern) {
                return true;
            }
        }
    }

    for (const auto &component : path) {
        std::string comp = component.string();
        if (!comp.empty() && comp[0] == '.' && comp != "." && comp != "..") {
            return true;
        }
    }

    return false;
}

std::vector<fs::path> Indexer::discover_files() {
    std::vector<fs::path> files;

    fs::path root(config_.root_path);
    if (!fs::exists(root)) {
        std::cerr << "Error: Path does not exist: " << config_.root_path << std::endl;
        return files;
    }

    std::vector<fs::path> dirs_to_visit;
    dirs_to_visit.push_back(root);

    while (!dirs_to_visit.empty()) {
        fs::path current_dir = dirs_to_visit.back();
        dirs_to_visit.pop_back();

        std::error_code ec;
        for (const auto &entry : fs::directory_iterator(current_dir, ec)) {
            if (ec)
                continue;

            const fs::path &path = entry.path();
            if (should_ignore(path))
                continue;

            if (entry.is_directory()) {
                dirs_to_visit.push_back(path);
            } else if (entry.is_regular_file()) {
                std::string ext = path.extension().string();
                Language lang = language_from_extension(ext);
                if (lang != Language::Unknown) {
                    files.push_back(path);
                }
            }
        }
    }

    std::sort(files.begin(), files.end());
    return files;
}

bool Indexer::parse_file(const fs::path &filepath, std::vector<FunctionInfo> &functions_out,
                         std::vector<CallInfo> &calls_out,
                         std::vector<VariableInfo> &variables_out) {
    MemoryMappedFile mmap;
    if (!mmap.open(filepath.string()))
        return false;

    if (mmap.size() == 0)
        return true;

    std::string ext = filepath.extension().string();
    Language lang = language_from_extension(ext);
    if (lang == Language::Unknown)
        return false;

    auto parser = create_parser(lang);
    if (!parser || !parser->parse(mmap.data(), mmap.size()))
        return false;

    std::string file_prefix = filepath.filename().string();
    size_t dot = file_prefix.rfind('.');
    if (dot != std::string::npos) {
        file_prefix = file_prefix.substr(0, dot);
    }

    auto functions = parser->extract_functions();

    for (auto &func : functions) {
        if (func.qualified_name.find("::") == std::string::npos &&
            func.qualified_name.find(".") == std::string::npos) {
            if (lang == Language::C) {
                func.qualified_name = file_prefix + "::" + func.qualified_name;
            }
        }

        FunctionInfo info;
        info.qualified_name = func.qualified_name;
        info.file_path = filepath.string();
        info.param_types = func.param_types;
        functions_out.push_back(std::move(info));

        auto calls = parser->extract_calls(func);
        for (const auto &call : calls) {
            CallInfo ci;
            ci.caller_name = func.qualified_name;
            ci.callee_name = call.qualified_name.empty() ? call.name : call.qualified_name;
            calls_out.push_back(std::move(ci));
        }

        auto vars = parser->extract_variables(func);
        for (const auto &var : vars) {
            VariableInfo vi;
            vi.qualified_name = var.qualified_name;
            vi.containing_func = var.containing_func;
            vi.value_source = var.value_source;
            vi.from_function_call = var.from_function_call;
            variables_out.push_back(std::move(vi));
        }
    }

    return true;
}

void Indexer::worker_parse_files(const std::vector<fs::path> &files, size_t start_idx,
                                 size_t end_idx, std::vector<FunctionInfo> &all_functions,
                                 std::vector<CallInfo> &all_calls,
                                 std::vector<VariableInfo> &all_variables,
                                 std::mutex &functions_mutex, std::mutex &calls_mutex,
                                 std::mutex &variables_mutex) {
    std::vector<FunctionInfo> local_functions;
    std::vector<CallInfo> local_calls;
    std::vector<VariableInfo> local_variables;
    local_functions.reserve(500);
    local_calls.reserve(2000);
    local_variables.reserve(1000);

    for (size_t i = start_idx; i < end_idx; ++i) {
        const auto &filepath = files[i];

        std::vector<FunctionInfo> file_functions;
        std::vector<CallInfo> file_calls;
        std::vector<VariableInfo> file_variables;

        if (parse_file(filepath, file_functions, file_calls, file_variables)) {
            // Accumulate locally
            for (auto &f : file_functions) {
                local_functions.push_back(std::move(f));
            }
            for (auto &c : file_calls) {
                local_calls.push_back(std::move(c));
            }
            for (auto &v : file_variables) {
                local_variables.push_back(std::move(v));
            }

            stats_.files_indexed++;
            stats_.functions_found += file_functions.size();
            stats_.calls_found += file_calls.size();
            stats_.variables_found += file_variables.size();

            {
                std::lock_guard<std::mutex> lock(output_mutex_);
                std::cout << "Parsed: " << filepath.string() << std::endl;
            }
        }

        if (local_functions.size() > 2000) {
            {
                std::lock_guard<std::mutex> lock(functions_mutex);
                all_functions.insert(all_functions.end(),
                                     std::make_move_iterator(local_functions.begin()),
                                     std::make_move_iterator(local_functions.end()));
            }
            local_functions.clear();
            local_functions.reserve(500);
        }
        if (local_calls.size() > 10000) {
            {
                std::lock_guard<std::mutex> lock(calls_mutex);
                all_calls.insert(all_calls.end(), std::make_move_iterator(local_calls.begin()),
                                 std::make_move_iterator(local_calls.end()));
            }
            local_calls.clear();
            local_calls.reserve(2000);
        }
        if (local_variables.size() > 5000) {
            {
                std::lock_guard<std::mutex> lock(variables_mutex);
                all_variables.insert(all_variables.end(),
                                     std::make_move_iterator(local_variables.begin()),
                                     std::make_move_iterator(local_variables.end()));
            }
            local_variables.clear();
            local_variables.reserve(1000);
        }
    }

    // Final flush
    if (!local_functions.empty()) {
        std::lock_guard<std::mutex> lock(functions_mutex);
        all_functions.insert(all_functions.end(), std::make_move_iterator(local_functions.begin()),
                             std::make_move_iterator(local_functions.end()));
    }
    if (!local_calls.empty()) {
        std::lock_guard<std::mutex> lock(calls_mutex);
        all_calls.insert(all_calls.end(), std::make_move_iterator(local_calls.begin()),
                         std::make_move_iterator(local_calls.end()));
    }
    if (!local_variables.empty()) {
        std::lock_guard<std::mutex> lock(variables_mutex);
        all_variables.insert(all_variables.end(), std::make_move_iterator(local_variables.begin()),
                             std::make_move_iterator(local_variables.end()));
    }
}

Graph Indexer::index() {
    Graph graph;

    auto files = discover_files();
    if (files.empty()) {
        std::cout << "No source files found to index." << std::endl;
        return graph;
    }

    std::cout << "Found " << files.size() << " source files to index." << std::endl;
    std::cout << "Using " << config_.num_threads << " threads." << std::endl;

    const size_t BATCH_SIZE = files.size() > 50000 ? 2000 : files.size() > 10000 ? 5000 : 10000;

    std::cout << "Processing in batches of " << BATCH_SIZE << " files." << std::endl;

    std::unordered_map<std::string, std::string> short_to_qualified;
    short_to_qualified.reserve(std::min(files.size() * 5, (size_t)500000));

    size_t total_batches = (files.size() + BATCH_SIZE - 1) / BATCH_SIZE;

    for (size_t batch = 0; batch < total_batches; ++batch) {
        size_t batch_start = batch * BATCH_SIZE;
        size_t batch_end = std::min(batch_start + BATCH_SIZE, files.size());
        size_t batch_file_count = batch_end - batch_start;

        std::cout << "\n=== Batch " << (batch + 1) << "/" << total_batches << " (files "
                  << batch_start << "-" << batch_end << ") ===" << std::endl;

        std::vector<FunctionInfo> batch_functions;
        std::vector<CallInfo> batch_calls;
        std::vector<VariableInfo> batch_variables;
        std::mutex functions_mutex, calls_mutex, variables_mutex;

        batch_functions.reserve(batch_file_count * 10);
        batch_calls.reserve(batch_file_count * 50);
        batch_variables.reserve(batch_file_count * 25);

        std::vector<std::thread> threads;
        size_t files_per_thread =
            (batch_file_count + config_.num_threads - 1) / config_.num_threads;

        for (unsigned int t = 0; t < config_.num_threads; ++t) {
            size_t start_idx = batch_start + t * files_per_thread;
            size_t end_idx = std::min(start_idx + files_per_thread, batch_end);

            if (start_idx >= batch_end)
                break;

            threads.emplace_back(&Indexer::worker_parse_files, this, std::cref(files), start_idx,
                                 end_idx, std::ref(batch_functions), std::ref(batch_calls),
                                 std::ref(batch_variables), std::ref(functions_mutex),
                                 std::ref(calls_mutex), std::ref(variables_mutex));
        }

        for (auto &t : threads) {
            t.join();
        }

        std::cout << "Batch parsed: " << batch_functions.size() << " functions, "
                  << batch_calls.size() << " calls, " << batch_variables.size() << " variables."
                  << std::endl;

        // Process this batch: add functions to graph and build short name mapping
        for (const auto &func : batch_functions) {
            graph.add_symbol(func.qualified_name, func.file_path);

            std::string short_name = func.qualified_name;
            size_t last_sep = short_name.rfind("::");
            if (last_sep != std::string::npos) {
                short_name = short_name.substr(last_sep + 2);
            }
            if (short_to_qualified.find(short_name) == short_to_qualified.end()) {
                short_to_qualified[short_name] = func.qualified_name;
            }
        }

        for (const auto &call : batch_calls) {
            const std::string &caller = call.caller_name;

            std::string_view callee_view = call.callee_name;
            size_t pos = callee_view.rfind("::");
            if (pos != std::string_view::npos) {
                callee_view = callee_view.substr(pos + 2);
            }
            pos = callee_view.rfind('.');
            if (pos != std::string_view::npos) {
                callee_view = callee_view.substr(pos + 1);
            }
            std::string callee_short(callee_view);

            const std::string *callee_ptr = &call.callee_name;
            auto it = short_to_qualified.find(callee_short);
            if (it != short_to_qualified.end()) {
                callee_ptr = &it->second;
            }

            if (!graph.has_symbol(*callee_ptr)) {
                graph.add_symbol(*callee_ptr);
            }
            if (!graph.has_symbol(caller)) {
                graph.add_symbol(caller);
            }

            graph.add_call(caller, *callee_ptr);
        }

        for (const auto &var : batch_variables) {
            std::string var_file;
            auto file_uid = graph.get_uid(var.containing_func);
            if (file_uid != INVALID_UID) {
                auto sym_file_uid = graph.get_symbol_file_uid(file_uid);
                if (sym_file_uid != INVALID_UID) {
                    var_file = graph.get_file_path(sym_file_uid);
                }
            }

            graph.add_symbol(var.qualified_name, var_file, SymbolType::Variable);

            if (!var.value_source.empty()) {
                const std::string *source_ptr = &var.value_source;

                if (var.from_function_call) {
                    std::string_view source_view = var.value_source;
                    size_t pos = source_view.rfind("::");
                    if (pos != std::string_view::npos) {
                        source_view = source_view.substr(pos + 2);
                    }
                    pos = source_view.rfind('.');
                    if (pos != std::string_view::npos) {
                        source_view = source_view.substr(pos + 1);
                    }
                    std::string source_short(source_view);

                    auto it = short_to_qualified.find(source_short);
                    if (it != short_to_qualified.end()) {
                        source_ptr = &it->second;
                    }
                }

                if (!graph.has_symbol(*source_ptr)) {
                    graph.add_symbol(*source_ptr, var_file,
                                     var.from_function_call ? SymbolType::Function
                                                            : SymbolType::Variable);
                }

                graph.add_data_flow(*source_ptr, var.qualified_name);
            }
        }

        batch_functions.clear();
        std::vector<FunctionInfo>().swap(batch_functions);
        batch_calls.clear();
        std::vector<CallInfo>().swap(batch_calls);
        batch_variables.clear();
        std::vector<VariableInfo>().swap(batch_variables);

        std::cout << "Batch " << (batch + 1) << " complete." << std::endl;
    }

    short_to_qualified.clear();
    std::unordered_map<std::string, std::string>().swap(short_to_qualified);

    indexed_files_.reserve(files.size());
    for (const auto &f : files) {
        indexed_files_.push_back(f.string());
    }
    indexed_files_.shrink_to_fit();

    std::cout << "  Functions found: " << stats_.functions_found.load() << std::endl;
    std::cout << "  Calls found: " << stats_.calls_found.load() << std::endl;
    std::cout << "  Variables found: " << stats_.variables_found.load() << std::endl;
    std::cout << "  Symbols created: " << graph.call_graph.symbol_to_uid.size() << std::endl;

    return graph;
}

} // namespace pioneer
