#include "pioneer/indexer.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace pioneer {

Indexer::Indexer(const IndexerConfig &config) : config_(config) {
    // Auto-detect thread count if not specified
    if (config_.num_threads == 0) {
        config_.num_threads = std::thread::hardware_concurrency();
        if (config_.num_threads == 0)
            config_.num_threads = 4; // Fallback
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

    // Ignore hidden files/directories
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

    // Iterative directory traversal
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
    // Read file
    std::ifstream file(filepath);
    if (!file.is_open())
        return false;

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();

    // Determine language
    std::string ext = filepath.extension().string();
    Language lang = language_from_extension(ext);
    if (lang == Language::Unknown)
        return false;

    // Parse
    auto parser = create_parser(lang);
    if (!parser || !parser->parse(source))
        return false;

    // Get file prefix for C
    std::string file_prefix = filepath.filename().string();
    size_t dot = file_prefix.rfind('.');
    if (dot != std::string::npos) {
        file_prefix = file_prefix.substr(0, dot);
    }

    // Extract functions
    auto functions = parser->extract_functions();

    for (auto &func : functions) {
        // Add file prefix for C functions
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

        // Extract calls for this function
        auto calls = parser->extract_calls(func);
        for (const auto &call : calls) {
            CallInfo ci;
            ci.caller_name = func.qualified_name;
            ci.callee_name = call.qualified_name.empty() ? call.name : call.qualified_name;
            calls_out.push_back(std::move(ci));
        }

        // Extract variables for this function (v1.1.0)
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
    // Thread-local storage to minimize lock contention
    std::vector<FunctionInfo> local_functions;
    std::vector<CallInfo> local_calls;
    std::vector<VariableInfo> local_variables;
    local_functions.reserve(1000);
    local_calls.reserve(5000);
    local_variables.reserve(2000);

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

            // Print progress (with lock to avoid garbled output)
            {
                std::lock_guard<std::mutex> lock(output_mutex_);
                std::cout << "Parsed: " << filepath.string() << std::endl;
            }
        }

        // Periodically flush to global to limit peak memory
        if (local_functions.size() > 10000) {
            {
                std::lock_guard<std::mutex> lock(functions_mutex);
                all_functions.insert(all_functions.end(),
                                     std::make_move_iterator(local_functions.begin()),
                                     std::make_move_iterator(local_functions.end()));
            }
            local_functions.clear();
            local_functions.reserve(1000);
        }
        if (local_calls.size() > 50000) {
            {
                std::lock_guard<std::mutex> lock(calls_mutex);
                all_calls.insert(all_calls.end(), std::make_move_iterator(local_calls.begin()),
                                 std::make_move_iterator(local_calls.end()));
            }
            local_calls.clear();
            local_calls.reserve(5000);
        }
        if (local_variables.size() > 20000) {
            {
                std::lock_guard<std::mutex> lock(variables_mutex);
                all_variables.insert(all_variables.end(),
                                     std::make_move_iterator(local_variables.begin()),
                                     std::make_move_iterator(local_variables.end()));
            }
            local_variables.clear();
            local_variables.reserve(2000);
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

    // Phase 1: Discover files
    auto files = discover_files();

    if (files.empty()) {
        std::cout << "No source files found to index." << std::endl;
        return graph;
    }

    std::cout << "Found " << files.size() << " source files to index." << std::endl;
    std::cout << "Using " << config_.num_threads << " threads." << std::endl;

    // Phase 2: Parallel parsing
    std::vector<FunctionInfo> all_functions;
    std::vector<CallInfo> all_calls;
    std::vector<VariableInfo> all_variables;
    std::mutex functions_mutex;
    std::mutex calls_mutex;
    std::mutex variables_mutex;

    // Reserve estimated space
    all_functions.reserve(files.size() * 20);
    all_calls.reserve(files.size() * 100);
    all_variables.reserve(files.size() * 50);

    // Create worker threads
    std::vector<std::thread> threads;
    size_t files_per_thread = (files.size() + config_.num_threads - 1) / config_.num_threads;

    for (unsigned int t = 0; t < config_.num_threads; ++t) {
        size_t start_idx = t * files_per_thread;
        size_t end_idx = std::min(start_idx + files_per_thread, files.size());

        if (start_idx >= files.size())
            break;

        threads.emplace_back(&Indexer::worker_parse_files, this, std::cref(files), start_idx,
                             end_idx, std::ref(all_functions), std::ref(all_calls),
                             std::ref(all_variables), std::ref(functions_mutex),
                             std::ref(calls_mutex), std::ref(variables_mutex));
    }

    // Wait for all threads
    for (auto &t : threads) {
        t.join();
    }

    std::cout << "\nParsing complete. Processing " << all_functions.size() << " functions, "
              << all_calls.size() << " calls, and " << all_variables.size() << " variables."
              << std::endl;

    // Phase 3: Detect overloads (single-threaded, but fast)
    std::unordered_map<std::string, std::vector<FunctionInfo *>> name_to_functions;
    name_to_functions.reserve(all_functions.size());

    for (auto &func : all_functions) {
        name_to_functions[func.qualified_name].push_back(&func);
    }

    // Build final name map (original base name -> final name with signature if overloaded)
    // Key: "base_name|file_path" for overloaded, just "base_name" for unique
    std::unordered_map<std::string, std::string> original_to_final;
    original_to_final.reserve(all_functions.size());

    for (auto &[base_name, funcs] : name_to_functions) {
        if (funcs.size() == 1) {
            // No overload
            original_to_final[base_name] = base_name;
            graph.add_symbol(base_name);
        } else {
            // Overloaded - add signatures
            for (auto *func : funcs) {
                std::string sig = build_param_signature(func->param_types);
                std::string final_name = base_name + sig;
                original_to_final[base_name + "|" + func->file_path] = final_name;
                graph.add_symbol(final_name);
            }
        }
    }

    // Build lookup for call resolution: base name -> list of final names
    std::unordered_map<std::string, std::vector<std::string>> base_to_final_names;
    for (const auto &[key, final_name] : original_to_final) {
        std::string base = key;
        size_t pipe = base.find('|');
        if (pipe != std::string::npos) {
            base = base.substr(0, pipe);
        }
        // Get just function name for matching calls
        std::string func_name = base;
        size_t last_sep = func_name.rfind("::");
        if (last_sep != std::string::npos) {
            func_name = func_name.substr(last_sep + 2);
        }
        base_to_final_names[func_name].push_back(final_name);
    }

    // Phase 4: Build call graph
    std::cout << "Building call graph..." << std::endl;

    // Map caller base names to their final names
    std::unordered_map<std::string, std::string> caller_to_final;
    for (const auto &func : all_functions) {
        auto it = original_to_final.find(func.qualified_name);
        if (it != original_to_final.end()) {
            caller_to_final[func.qualified_name] = it->second;
        } else {
            auto it2 = original_to_final.find(func.qualified_name + "|" + func.file_path);
            if (it2 != original_to_final.end()) {
                caller_to_final[func.qualified_name + "|" + func.file_path] = it2->second;
            }
        }
    }

    // Create a set of all caller qualified_name|file_path for quick lookup
    std::unordered_map<std::string, std::string> caller_lookup; // base -> final
    for (const auto &func : all_functions) {
        std::string key = func.qualified_name + "|" + func.file_path;
        auto it = original_to_final.find(func.qualified_name);
        if (it != original_to_final.end()) {
            caller_lookup[func.qualified_name] = it->second;
        }
        auto it2 = original_to_final.find(key);
        if (it2 != original_to_final.end()) {
            caller_lookup[key] = it2->second;
        }
    }

    // Process calls
    for (const auto &call : all_calls) {
        // Find caller's final name
        std::string caller_final = call.caller_name;
        auto it = caller_lookup.find(call.caller_name);
        if (it != caller_lookup.end()) {
            caller_final = it->second;
        }

        // Find callee - strip qualifiers to get base name
        std::string callee_base = call.callee_name;
        size_t pos = callee_base.rfind("::");
        if (pos != std::string::npos) {
            callee_base = callee_base.substr(pos + 2);
        }
        pos = callee_base.rfind(".");
        if (pos != std::string::npos) {
            callee_base = callee_base.substr(pos + 1);
        }

        // Look for matching defined function
        std::string callee_final = call.callee_name;
        auto match_it = base_to_final_names.find(callee_base);
        if (match_it != base_to_final_names.end() && !match_it->second.empty()) {
            callee_final = match_it->second[0]; // Best effort: use first match
        }

        // Add symbols if not exists
        if (!graph.has_symbol(callee_final)) {
            graph.add_symbol(callee_final);
        }
        if (!graph.has_symbol(caller_final)) {
            graph.add_symbol(caller_final);
        }

        graph.add_call(caller_final, callee_final);
    }

    // Phase 5: Process variables for data flow (v1.1.0)
    std::cout << "Building data flow graph..." << std::endl;

    for (const auto &var : all_variables) {
        // Add variable as a symbol
        graph.add_symbol(var.qualified_name, SymbolType::Variable);

        // Track data flow from value source to variable
        if (!var.value_source.empty()) {
            // Store the raw value source as the data flow source
            // This captures: function calls, variable references, member accesses, literals
            std::string source = var.value_source;

            // If it's a function call, try to resolve to the function symbol
            if (var.from_function_call) {
                std::string source_base = var.value_source;
                size_t pos = source_base.rfind("::");
                if (pos != std::string::npos) {
                    source_base = source_base.substr(pos + 2);
                }
                pos = source_base.rfind(".");
                if (pos != std::string::npos) {
                    source_base = source_base.substr(pos + 1);
                }

                auto match_it = base_to_final_names.find(source_base);
                if (match_it != base_to_final_names.end() && !match_it->second.empty()) {
                    source = match_it->second[0];
                }
            }

            // Add the source as a symbol if it doesn't exist
            if (!graph.has_symbol(source)) {
                graph.add_symbol(source, var.from_function_call ? SymbolType::Function
                                                                : SymbolType::Variable);
            }

            // Add data flow: source -> variable
            graph.add_data_flow(source, var.qualified_name);
        }
    }

    // Populate indexed_files_
    for (const auto &f : files) {
        indexed_files_.push_back(f.string());
    }

    // Finalize
    graph.finalize();

    std::cout << "\nIndexing complete." << std::endl;
    std::cout << "  Files indexed: " << stats_.files_indexed.load() << std::endl;
    std::cout << "  Functions found: " << stats_.functions_found.load() << std::endl;
    std::cout << "  Calls found: " << stats_.calls_found.load() << std::endl;
    std::cout << "  Variables found: " << stats_.variables_found.load() << std::endl;
    std::cout << "  Symbols created: " << graph.call_graph.symbol_to_uid.size() << std::endl;

    return graph;
}

} // namespace pioneer
