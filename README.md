# Pioneer - Call Graph Analyzer

**Version 2.3.0**

A CLI tool that uses tree-sitter to index codebases (Python, C, C++) and build call graphs with advanced querying capabilities.

## Building

Requires:
- CMake 3.20+
- Clang/GNU Compiler
- Git (for fetching dependencies)

```bash
# Configure (must use clang)
cmake -B build -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++

# Build
cmake --build build -j$(nproc)

# The executable will be at build/pioneer
```

## Usage

### Indexing
```bash
# Build call graph index for current directory
pioneer --index

# Use 8 threads for faster indexing
pioneer --index -j 8
```

### Call Graph Queries
```bash
# Find paths from function A to function B
pioneer --start A --end B

# Find all call paths from function A (forward trace)
pioneer --start A --end END

# Find all callers of function B (backtrace)
pioneer --start START --end B
# or
pioneer --backtrace --end B

# Show file paths for each node in the trace
pioneer --start foo --end bar --path
```

### Symbol Search
```bash
# Search for symbols matching a pattern
pioneer --search 'init'

# Search with file paths
pioneer --search 'init' --path

# Pattern matching for start/end symbols
pioneer -p --start foo --end bar
```

### Code Search (Grep)
```bash
# Search for text pattern in all indexed files
pioneer --grep 'rxe_send'

# Use 8 threads for faster grep
pioneer --grep 'pattern' -j 8

# Use regex patterns
pioneer --grep 'rx.*send' --regex

# Case-insensitive search
pioneer --grep 'RXE' --ignore-case

# Combine flags
pioneer --grep 'error.*handling' --regex --ignore-case -j 8
```

### Data Flow Analysis
```bash
# Find what a variable is assigned from
pioneer --data-sources 'func::x'

# Find variables assigned from a function's return value
pioneer --data-sinks 'get_data'

# List all variables in functions matching a pattern
pioneer --vars 'MyClass'

# Find ALL assignments to a member/field
pioneer --member 'dev->field'
```

### Other Commands
```bash
# List all indexed symbols
pioneer --list

# Check symbol type (function or variable)
pioneer --type 'MyClass::foo'

# Help
pioneer --help
```

## Index Format

The index is saved as `.pioneer.json` with the following structure (v1.2.0):

```json
{
  "metadata": {
    "version": "1.2.0",
    "num_symbols": 42,
    "num_functions": 35,
    "num_variables": 7,
    "num_files": 5,
    "end_uid": 43,
    "UIDs": {
      "MyClass::foo_int": 1,
      "MyClass::bar": 2,
      "helper": 3,
      "main": 4,
      "END": 43
    }
  },
  "symbol_types": {
    "1": 0,
    "2": 0,
    "3": 0,
    "4": 0
  },
  "call_mapping": {
    "1": [2, 3],
    "2": [43],
    "3": [43],
    "4": [1]
  },
  "data_flow": {
    "10": [15, 20]
  },
  "file_paths": {
    "1": "src/main.cpp",
    "2": "src/utils.cpp",
    "3": "include/myclass.hpp"
  },
  "file_symbols": {
    "1": [4],
    "2": [3],
    "3": [1, 2]
  },
  "symbol_files": {
    "1": 3,
    "2": 3,
    "3": 2,
    "4": 1
  },
  "path_trie": {
    "name": "",
    "children": {
      "src": {
        "name": "src",
        "file_uids": [],
        "children": {
          "main.cpp": {
            "name": "main.cpp",
            "file_uids": [1],
            "children": {}
          }
        }
      }
    }
  }
}
```

### Format Details

- **metadata.version**: Schema version (1.2.0 includes file tracking)
- **symbol_types**: Maps symbol UID to type (0=function, 1=variable)
- **call_mapping**: Function call relationships (caller → [callees])
- **data_flow**: Variable assignments (source → [destinations])
- **file_paths**: File UID to absolute path mapping
- **file_symbols**: File UID to symbols contained in that file
- **symbol_files**: Symbol UID to file UID mapping
- **path_trie**: Hierarchical trie of file paths with UIDs at leaves

## Symbol Naming Convention

- **C functions**: `filename::function_name` or `function_name_type1_type2` for overloads
- **C++ methods**: `Namespace::Class::method_type1_type2`
- **Python methods**: `Class.method` or `module.function`

## Architecture

```
include/pioneer/
├── types.hpp      # Core types, PathNode trie, file UID system
├── graph.hpp      # Graph class with file tracking and serialization
├── parser.hpp     # Tree-sitter parser abstraction
├── indexer.hpp    # File discovery and indexing
├── query.hpp      # Path finding query engine
└── commands.hpp   # Command handlers (index, search, grep, etc.)

src/
├── main.cpp       # CLI entry point and argument parsing
├── graph.cpp      # Graph implementation with file tracking
├── parser.cpp     # Language-specific parsing
├── indexer.cpp    # Indexing logic with file path tracking
├── query.cpp      # Query algorithms
└── commands.cpp   # Command implementations and multithreaded grep
```

## Design Decisions

1. **UID-based graph**: All symbols are mapped to 64-bit UIDs for memory efficiency
2. **Iterative algorithms**: Avoid recursion to prevent stack overflow on large codebases
3. **Streaming output**: Paths are printed as found, not buffered
4. **Closed graph**: All leaf nodes connect to a special END node
5. **Reverse index**: Maintains both caller→callee and callee→caller mappings for efficient backtrace

## License

Apache 2.0

