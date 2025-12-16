# Pioneer - Call Graph Analyzer

A CLI tool that uses tree-sitter to index codebases (Python, C, C++) and build call graphs.

## Features

- **Multi-language support**: Python, C, and C++ (extensible for more)
- **Call graph indexing**: Builds a complete call graph of your codebase
- **Path finding**: Find all call paths between two symbols
- **Backtrace**: Find all callers that lead to a specific function
- **Forward trace**: Find all functions reachable from a starting point
- **Memory efficient**: Uses UIDs instead of string storage for the graph
- **Streaming output**: Prints paths as they are found

## Building

Requires:
- CMake 3.20+
- Clang compiler (required)
- Git (for fetching dependencies)

```bash
# Configure (must use clang)
cmake -B build -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++

# Build
cmake --build build -j$(nproc)

# The executable will be at build/pioneer
```

## Usage

```bash
# Build call graph index for current directory
pioneer --index

# Find paths from function A to function B
pioneer --start A --end B

# Find all call paths from function A (forward trace)
pioneer --start A --end END

# Find all callers of function B (backtrace)
pioneer --start START --end B
# or
pioneer --backtrace --end B

# List all indexed symbols
pioneer --list

# Help
pioneer --help
```

## Index Format

The index is saved as `.pioneer.json` with the following structure:

```json
{
  "metadata": {
    "num_symbols": 42,
    "end_uid": 43,
    "UIDs": {
      "MyClass::foo_int": 1,
      "MyClass::bar": 2,
      "helper": 3,
      ...
      "END": 43
    }
  },
  "call_mapping": {
    "1": [2, 3],
    "2": [43],
    "3": [43]
  }
}
```

## Symbol Naming Convention

- **C functions**: `filename::function_name` or `function_name_type1_type2` for overloads
- **C++ methods**: `Namespace::Class::method_type1_type2`
- **Python methods**: `Class.method` or `module.function`

## Architecture

```
include/pioneer/
├── types.hpp      # Core types, Language enum, Symbol/CallGraph structs
├── graph.hpp      # Graph class with serialization
├── parser.hpp     # Tree-sitter parser abstraction
├── indexer.hpp    # File discovery and indexing
└── query.hpp      # Path finding query engine

src/
├── main.cpp       # CLI entry point
├── graph.cpp      # Graph implementation
├── parser.cpp     # Language-specific parsing
├── indexer.cpp    # Indexing logic
└── query.cpp      # Query algorithms
```

## Design Decisions

1. **UID-based graph**: All symbols are mapped to 64-bit UIDs for memory efficiency
2. **Iterative algorithms**: Avoid recursion to prevent stack overflow on large codebases
3. **Streaming output**: Paths are printed as found, not buffered
4. **Closed graph**: All leaf nodes connect to a special END node
5. **Reverse index**: Maintains both caller→callee and callee→caller mappings for efficient backtrace

## License

MIT
