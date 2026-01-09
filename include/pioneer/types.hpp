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

#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace pioneer {

class MemoryMappedFile {
public:

    MemoryMappedFile() = default;
    ~MemoryMappedFile() { close(); }

    MemoryMappedFile(const MemoryMappedFile &) = delete;
    MemoryMappedFile &operator=(const MemoryMappedFile &) = delete;

    MemoryMappedFile(MemoryMappedFile &&other) noexcept
        : data_(other.data_), size_(other.size_), fd_(other.fd_) {
        other.data_ = nullptr;
        other.size_ = 0;
        other.fd_ = -1;
    }

    MemoryMappedFile &operator=(MemoryMappedFile &&other) noexcept {
        if (this != &other) {
            close();
            data_ = other.data_;
            size_ = other.size_;
            fd_ = other.fd_;
            other.data_ = nullptr;
            other.size_ = 0;
            other.fd_ = -1;
        }
        return *this;
    }

    bool open(const std::string &filepath) {
        close();

        fd_ = ::open(filepath.c_str(), O_RDONLY);
        if (fd_ < 0)
            return false;

        struct stat st;
        if (fstat(fd_, &st) < 0) {
            ::close(fd_);
            fd_ = -1;
            return false;
        }
        size_ = static_cast<size_t>(st.st_size);

        if (size_ == 0)
            return true;

        data_ = static_cast<const char *>(mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd_, 0));
        if (data_ == MAP_FAILED) {
            data_ = nullptr;
            ::close(fd_);
            fd_ = -1;
            return false;
        }

        madvise(const_cast<char *>(data_), size_, MADV_SEQUENTIAL | MADV_WILLNEED);
        return true;
    }

    void close() {
        if (data_) {
            madvise(const_cast<char *>(data_), size_, MADV_DONTNEED);
            munmap(const_cast<char *>(data_), size_);
            data_ = nullptr;
        }
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
        size_ = 0;
    }

    const char *data() const { return data_; }
    size_t size() const { return size_; }
    bool is_open() const { return fd_ >= 0; }
    std::string_view view() const { return std::string_view(data_, size_); }

private:

    const char *data_ = nullptr;
    size_t size_ = 0;
    int fd_ = -1;
};

class StringPool {
public:

    size_t intern(const std::string &str) {
        auto it = index_.find(str);
        if (it != index_.end()) {
            return it->second;
        }
        size_t idx = strings_.size();
        strings_.push_back(str);
        index_[strings_.back()] = idx;
        return idx;
    }

    const std::string &get(size_t idx) const {
        static const std::string empty;
        return (idx < strings_.size()) ? strings_[idx] : empty;
    }

    std::string_view get_view(size_t idx) const {
        return (idx < strings_.size()) ? std::string_view(strings_[idx]) : std::string_view();
    }

    bool contains(const std::string &str) const { return index_.find(str) != index_.end(); }

    size_t find(const std::string &str) const {
        auto it = index_.find(str);
        return (it != index_.end()) ? it->second : SIZE_MAX;
    }

    size_t size() const { return strings_.size(); }

    void shrink_to_fit() { strings_.shrink_to_fit(); }

    void clear() {
        strings_.clear();
        index_.clear();
    }

    auto begin() const { return strings_.begin(); }
    auto end() const { return strings_.end(); }

private:

    std::vector<std::string> strings_;
    std::unordered_map<std::string_view, size_t> index_;
};

using SymbolUID = uint64_t;

constexpr SymbolUID INVALID_UID = 0;
constexpr SymbolUID END_UID_PLACEHOLDER = UINT64_MAX;

enum class Language { Unknown, Python, C, Cpp };

inline const char *language_to_string(Language lang) {
    switch (lang) {
    case Language::Python:
        return "python";
    case Language::C:
        return "c";
    case Language::Cpp:
        return "cpp";
    default:
        return "unknown";
    }
}

inline Language language_from_extension(const std::string &ext) {
    if (ext == ".py")
        return Language::Python;
    if (ext == ".c" || ext == ".h")
        return Language::C;
    if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".hpp" || ext == ".hh" ||
        ext == ".hxx")
        return Language::Cpp;
    return Language::Unknown;
}

struct Symbol {
    std::string name;
    std::string short_name;
    std::string file;
    uint32_t line;
    Language language;

    bool operator==(const Symbol &other) const { return name == other.name; }
};

struct SymbolHash {
    size_t operator()(const Symbol &s) const { return std::hash<std::string>{}(s.name); }
};

struct CallSite {
    std::string caller;
    std::string callee;
    std::string file;
    uint32_t line;
};

enum class SymbolType { Function, Variable, End };

struct VariableAssignment {
    std::string variable;
    std::string value_source;
    std::string containing_func;
    uint32_t line;
    bool is_function_call;
};

struct PathNode {
    std::map<std::string, PathNode> subdirs;
    std::vector<SymbolUID> file_uids;
};

struct CallGraph {
    StringPool symbol_pool;
    std::unordered_map<std::string, SymbolUID> symbol_to_uid;
    std::unordered_map<SymbolUID, size_t> uid_to_string_idx;
    std::unordered_map<SymbolUID, SymbolType> symbol_types;

    StringPool filepath_pool;
    std::unordered_map<std::string, SymbolUID> filepath_to_uid;
    std::unordered_map<SymbolUID, size_t> file_uid_to_path_idx;
    std::unordered_map<SymbolUID, std::vector<SymbolUID>> file_to_symbols;
    std::unordered_map<SymbolUID, SymbolUID> symbol_to_file;
    SymbolUID next_file_uid = 1;

    std::unordered_map<SymbolUID, std::unordered_set<SymbolUID>> call_map;
    std::unordered_map<SymbolUID, std::unordered_set<SymbolUID>> reverse_call_map;
    std::unordered_map<SymbolUID, std::unordered_set<SymbolUID>> data_flow_map;
    std::unordered_map<SymbolUID, std::unordered_set<SymbolUID>> reverse_data_flow_map;

    SymbolUID next_uid = 1;
    SymbolUID end_uid = INVALID_UID;

    SymbolUID get_or_create_uid(const std::string &symbol_name,
                                SymbolType type = SymbolType::Function) {
        auto it = symbol_to_uid.find(symbol_name);
        if (it != symbol_to_uid.end()) {
            return it->second;
        }
        SymbolUID uid = next_uid++;
        size_t str_idx = symbol_pool.intern(symbol_name);
        symbol_to_uid[symbol_name] = uid;
        uid_to_string_idx[uid] = str_idx;
        symbol_types[uid] = type;
        return uid;
    }

    SymbolUID get_uid(const std::string &symbol_name) const {
        auto it = symbol_to_uid.find(symbol_name);
        return (it != symbol_to_uid.end()) ? it->second : INVALID_UID;
    }

    const std::string &get_symbol(SymbolUID uid) const {
        static const std::string end_str = "END";
        static const std::string empty_str;
        if (uid == end_uid)
            return end_str;
        auto it = uid_to_string_idx.find(uid);
        return (it != uid_to_string_idx.end()) ? symbol_pool.get(it->second) : empty_str;
    }

    SymbolType get_type(SymbolUID uid) const {
        auto it = symbol_types.find(uid);
        return (it != symbol_types.end()) ? it->second : SymbolType::Function;
    }

    bool is_variable(SymbolUID uid) const { return get_type(uid) == SymbolType::Variable; }

    void add_call(SymbolUID caller, SymbolUID callee) {
        call_map[caller].insert(callee);
        reverse_call_map[callee].insert(caller);
    }

    void add_data_flow(SymbolUID variable, SymbolUID source) {
        data_flow_map[variable].insert(source);
        reverse_data_flow_map[source].insert(variable);
    }

    void finalize() {
        end_uid = next_uid++;
        size_t end_str_idx = symbol_pool.intern("END");
        symbol_to_uid["END"] = end_uid;
        uid_to_string_idx[end_uid] = end_str_idx;
        symbol_types[end_uid] = SymbolType::End;

        std::unordered_set<SymbolUID> all_symbols;
        for (const auto &[name, uid] : symbol_to_uid) {
            if (uid != end_uid && get_type(uid) == SymbolType::Function) {
                all_symbols.insert(uid);
            }
        }

        for (SymbolUID uid : all_symbols) {
            if (call_map.find(uid) == call_map.end() || call_map[uid].empty()) {
                add_call(uid, end_uid);
            }
        }

        shrink_to_fit();
    }

    void shrink_to_fit() {
        symbol_pool.shrink_to_fit();
        filepath_pool.shrink_to_fit();
        for (auto &[uid, symbols] : file_to_symbols) {
            symbols.shrink_to_fit();
        }
    }

    const std::string &get_file_path(SymbolUID file_uid) const {
        static const std::string empty_str;
        auto it = file_uid_to_path_idx.find(file_uid);
        return (it != file_uid_to_path_idx.end()) ? filepath_pool.get(it->second) : empty_str;
    }

    size_t num_symbols() const { return symbol_to_uid.size() - (end_uid != INVALID_UID ? 1 : 0); }

    size_t num_functions() const {
        size_t count = 0;
        for (const auto &[uid, type] : symbol_types) {
            if (type == SymbolType::Function)
                count++;
        }
        return count;
    }

    size_t num_variables() const {
        size_t count = 0;
        for (const auto &[uid, type] : symbol_types) {
            if (type == SymbolType::Variable)
                count++;
        }
        return count;
    }
};

inline void add_to_path_trie(PathNode &root, const std::string &filepath, SymbolUID file_uid) {
    if (filepath.empty())
        return;

    PathNode *current = &root;
    std::string remaining = filepath;
    size_t pos = 0;

    while (pos < remaining.size()) {
        size_t next_slash = remaining.find_first_of("/\\", pos);
        if (next_slash == std::string::npos) {
            if (!filepath.empty()) {
                auto it = std::find(current->file_uids.begin(), current->file_uids.end(), file_uid);
                if (it == current->file_uids.end()) {
                    current->file_uids.push_back(file_uid);
                }
            }
            break;
        } else {
            std::string dir = remaining.substr(pos, next_slash - pos);
            if (!dir.empty() && dir != ".") {
                current = &(current->subdirs[dir]);
            }
            pos = next_slash + 1;
        }
    }
}

inline PathNode
build_path_trie(const std::unordered_map<SymbolUID, std::string> &file_uid_to_path) {
    PathNode root;
    for (const auto &[file_uid, filepath] : file_uid_to_path) {
        add_to_path_trie(root, filepath, file_uid);
    }
    return root;
}

} // namespace pioneer
