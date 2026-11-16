# VocalSuite

VocalSuite is a JUCE-based vocal processor plugin with EQ, drive, de-essing, compression, limiting, stereo width, mix, bypass, and Neutral/Warm/Bright starting presets.

## Build

Requirements:

- CMake 3.22+
- C++17 toolchain
- JUCE platform dependencies (Linux builds require the appropriate X11/GTK and audio development packages)

Configure and build:

```bash
cmake -S . -B build
cmake --build build --config Release --parallel 2
```

By default, the VST3 is copied to the platform's standard user plugin folder. To select a custom VST3 destination:

```bash
cmake -S . -B build -DVS_VST3_INSTALL_DIR="/path/to/your/VST3/folder"
cmake --build build --config Release --parallel 2
```

To keep the build from copying the plugin to any plugin folder, set `-DVS_COPY_AFTER_BUILD=OFF`.

## Downloadable package

Create a ZIP containing the VST3 bundle (and the AU component on macOS):

```bash
cmake --build build --config Release --target package --parallel 2
```

The ZIP is written to the build directory as `VocalSuite-0.1.0-<platform>.zip`. Unzip it and copy the plugin bundle to your DAW's plugin folder, then rescan plugins.

## Windows and macOS builds

GitHub Actions builds and packages the plugin for Windows and macOS on pushes, pull requests, and manual workflow runs. Download the ZIP from the run's **Artifacts** section.

This is an actively evolving starter processor; audition it and set levels conservatively in your DAW.
