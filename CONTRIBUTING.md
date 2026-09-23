# Contributing

```sh
cmake --preset debug
cmake --build build/preset-debug
ctest --test-dir build/preset-debug --output-on-failure
clang-format -i include/chronosched/*.hpp src/*.cpp tests/*.cpp
```

Use `cmake --preset asan` with GCC/Clang for AddressSanitizer. Use `cmake --preset tsan` on Linux with a supported compiler. Keep pull requests focused, add behavioral tests for concurrency changes, and do not commit generated output.
