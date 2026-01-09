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

#include "pioneer/streaming.hpp"
#include <fstream>
#include <stdexcept>

namespace pioneer {

StreamingSearchHandler::StreamingSearchHandler(const std::vector<std::string> &p,
                                               std::vector<std::string> &m)
    : patterns(p), matches(m) {}

bool StreamingSearchHandler::null() { return true; }
bool StreamingSearchHandler::boolean(bool) { return true; }
bool StreamingSearchHandler::number_integer(number_integer_t) { return true; }
bool StreamingSearchHandler::number_unsigned(number_unsigned_t) { return true; }
bool StreamingSearchHandler::number_float(number_float_t, const string_t &) { return true; }
bool StreamingSearchHandler::string(string_t &) { return true; }
bool StreamingSearchHandler::binary(binary_t &) { return true; }

bool StreamingSearchHandler::start_object(std::size_t) {
    depth++;
    if (skip_depth > 0)
        skip_depth++;
    return true;
}

bool StreamingSearchHandler::end_object() {
    depth--;
    if (skip_depth > 0)
        skip_depth--;
    if (depth == 2 && in_uids) {
        in_uids = false; // Exiting UIDs section - we're done!
        return false;    // Stop parsing early
    }
    return true;
}

bool StreamingSearchHandler::start_array(std::size_t) {
    if (skip_depth == 0)
        skip_depth = 1; // Skip arrays
    return true;
}

bool StreamingSearchHandler::end_array() {
    if (skip_depth > 0)
        skip_depth--;
    return true;
}

bool StreamingSearchHandler::key(string_t &key) {
    if (skip_depth > 0)
        return true;

    if (depth == 2 && key == "UIDs") {
        in_uids = true;
    } else if (in_uids && depth == 3) {
        // key is a symbol name - check if it matches any pattern
        for (const auto &pattern : patterns) {
            if (key.find(pattern) != std::string::npos) {
                matches.push_back(key);
                break;
            }
        }
    } else if (depth == 1 && key != "metadata") {
        // Skip non-metadata sections entirely
        skip_depth = 1;
    }
    return true;
}

bool StreamingSearchHandler::parse_error(std::size_t, const std::string &,
                                         const nlohmann::json::exception &) {
    return false;
}

StreamingFilePathHandler::StreamingFilePathHandler(std::vector<std::string> &p) : paths(p) {}

bool StreamingFilePathHandler::null() { return true; }
bool StreamingFilePathHandler::boolean(bool) { return true; }
bool StreamingFilePathHandler::number_integer(number_integer_t) { return true; }
bool StreamingFilePathHandler::number_unsigned(number_unsigned_t) { return true; }
bool StreamingFilePathHandler::number_float(number_float_t, const string_t &) { return true; }
bool StreamingFilePathHandler::binary(binary_t &) { return true; }

bool StreamingFilePathHandler::string(string_t &val) {
    if (in_file_paths && skip_depth == 0) {
        paths.push_back(val);
    }
    return true;
}

bool StreamingFilePathHandler::start_object(std::size_t) {
    depth++;
    if (skip_depth > 0)
        skip_depth++;
    return true;
}

bool StreamingFilePathHandler::end_object() {
    depth--;
    if (skip_depth > 0)
        skip_depth--;
    if (depth == 1 && in_file_paths) {
        in_file_paths = false;
        return false; // Stop parsing - we got all file paths
    }
    return true;
}

bool StreamingFilePathHandler::start_array(std::size_t) {
    if (skip_depth == 0)
        skip_depth = 1;
    return true;
}

bool StreamingFilePathHandler::end_array() {
    if (skip_depth > 0)
        skip_depth--;
    return true;
}

bool StreamingFilePathHandler::key(string_t &key) {
    if (skip_depth > 0)
        return true;
    current_key = key;

    if (depth == 1) {
        if (key == "file_paths") {
            in_file_paths = true;
        } else {
            // Skip all other sections
            skip_depth = 1;
        }
    }
    return true;
}

bool StreamingFilePathHandler::parse_error(std::size_t, const std::string &,
                                           const nlohmann::json::exception &) {
    return false;
}

std::vector<std::string> stream_search_symbols(const std::string &index_file,
                                               const std::vector<std::string> &patterns) {
    std::ifstream file(index_file);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open index file: " + index_file);
    }

    std::vector<std::string> matches;
    StreamingSearchHandler handler(patterns, matches);
    nlohmann::json::sax_parse(file, &handler);
    return matches;
}

std::vector<std::string> stream_all_symbols(const std::string &index_file) {
    std::vector<std::string> all_patterns = {""}; // Empty pattern matches everything
    return stream_search_symbols(index_file, all_patterns);
}

std::vector<std::string> stream_file_paths(const std::string &index_file) {
    std::ifstream file(index_file);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open index file: " + index_file);
    }

    std::vector<std::string> paths;
    StreamingFilePathHandler handler(paths);
    nlohmann::json::sax_parse(file, &handler);
    return paths;
}

} // namespace pioneer
