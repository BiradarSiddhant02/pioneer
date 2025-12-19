#!/bin/sh

find . -type f \( -name "*.c" -o -name "*.cpp" -o -name "*.h" \) -exec dos2unix {} +
find . -iname '*.hpp' -o -iname '*.cpp' | xargs clang-format -i
