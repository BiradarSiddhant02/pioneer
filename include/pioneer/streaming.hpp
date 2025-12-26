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

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace pioneer {

// SAX handler for streaming symbol search - minimal memory usage
// Only parses the UIDs section and matches patterns on-the-fly
class StreamingSearchHandler : public nlohmann::json::json_sax_t {
public:
    const std::vector<std::string>& patterns;
    std::vector<std::string>& matches;
    bool in_uids = false;
    int depth = 0;
    int skip_depth = 0;
    
    StreamingSearchHandler(const std::vector<std::string>& p, std::vector<std::string>& m);
    
    bool null() override;
    bool boolean(bool) override;
    bool number_integer(number_integer_t) override;
    bool number_unsigned(number_unsigned_t) override;
    bool number_float(number_float_t, const string_t&) override;
    bool string(string_t&) override;
    bool binary(binary_t&) override;
    bool start_object(std::size_t) override;
    bool end_object() override;
    bool start_array(std::size_t) override;
    bool end_array() override;
    bool key(string_t& key) override;
    bool parse_error(std::size_t, const std::string&, const nlohmann::json::exception&) override;
};

// SAX handler for streaming file path extraction
// Only parses the file_paths section
class StreamingFilePathHandler : public nlohmann::json::json_sax_t {
public:
    std::vector<std::string>& paths;
    bool in_file_paths = false;
    int depth = 0;
    int skip_depth = 0;
    std::string current_key;
    
    explicit StreamingFilePathHandler(std::vector<std::string>& p);
    
    bool null() override;
    bool boolean(bool) override;
    bool number_integer(number_integer_t) override;
    bool number_unsigned(number_unsigned_t) override;
    bool number_float(number_float_t, const string_t&) override;
    bool string(string_t& val) override;
    bool binary(binary_t&) override;
    bool start_object(std::size_t) override;
    bool end_object() override;
    bool start_array(std::size_t) override;
    bool end_array() override;
    bool key(string_t& key) override;
    bool parse_error(std::size_t, const std::string&, const nlohmann::json::exception&) override;
};

// Streaming utility functions

// Stream search symbols matching patterns (minimal memory)
std::vector<std::string> stream_search_symbols(const std::string& index_file,
                                                const std::vector<std::string>& patterns);

// Stream all symbol names (minimal memory)
std::vector<std::string> stream_all_symbols(const std::string& index_file);

// Stream file paths only (minimal memory)
std::vector<std::string> stream_file_paths(const std::string& index_file);

} // namespace pioneer
