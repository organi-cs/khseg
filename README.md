# khseg

A Khmer word segmenter written in C++20. Work in progress; see [docs/DESIGN.md](docs/DESIGN.md) for the plan.

## Build

```bash
cmake --preset mingw-release
cmake --build --preset mingw-release
ctest --preset mingw-release
```

On Linux use the `linux-release` preset, on Windows with Visual Studio use `msvc`.

## License

Code: Apache-2.0. Data files have their own licenses, listed in `data/README.md`.
