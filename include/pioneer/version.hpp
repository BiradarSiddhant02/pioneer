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

namespace pioneer {

// ============================================================================
// Pioneer Version Information
// ============================================================================

// Application version (displayed to users)
constexpr int VERSION_MAJOR = 2;
constexpr int VERSION_MINOR = 1;
constexpr int VERSION_PATCH = 0;

// Version string for display
constexpr const char* VERSION_STRING = "2.1.0";

// Index file schema version (for compatibility checking)
// Increment when index file format changes
constexpr int INDEX_SCHEMA_MAJOR = 2;
constexpr int INDEX_SCHEMA_MINOR = 1;
constexpr int INDEX_SCHEMA_PATCH = 0;
constexpr const char* INDEX_SCHEMA_VERSION = "2.1.0";

// Minimum compatible index schema version (for loading old indexes)
constexpr int MIN_COMPAT_SCHEMA_MAJOR = 1;
constexpr int MIN_COMPAT_SCHEMA_MINOR = 2;
constexpr int MIN_COMPAT_SCHEMA_PATCH = 0;

// Check if a schema version is compatible
inline bool is_schema_compatible(int major, int minor, int /*patch*/) {
    // Same major version and >= minimum minor
    if (major == INDEX_SCHEMA_MAJOR) {
        return true;  // Same major version, always compatible
    }
    // Check against minimum compatible version
    if (major > MIN_COMPAT_SCHEMA_MAJOR) {
        return false;  // Future version, not compatible
    }
    if (major == MIN_COMPAT_SCHEMA_MAJOR && minor >= MIN_COMPAT_SCHEMA_MINOR) {
        return true;  // Meets minimum requirements
    }
    return false;
}

// Parse version string "X.Y.Z" into components
inline bool parse_version(const std::string& version, int& major, int& minor, int& patch) {
    size_t pos1 = version.find('.');
    if (pos1 == std::string::npos) return false;
    
    size_t pos2 = version.find('.', pos1 + 1);
    if (pos2 == std::string::npos) return false;
    
    try {
        major = std::stoi(version.substr(0, pos1));
        minor = std::stoi(version.substr(pos1 + 1, pos2 - pos1 - 1));
        patch = std::stoi(version.substr(pos2 + 1));
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace pioneer
