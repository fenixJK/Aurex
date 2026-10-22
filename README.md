# Aurex

A Bee Swarm Simulator macro for Windows and macOS. Still being worked on, so bug reports and recordings help a lot.

## Install

Download the ZIP for your computer from [Releases](https://github.com/fenixJK/Renova/releases), when available, and extract it into a folder you can write to.

- **Windows:** open `Aurex.exe`. Its third-party libraries are compiled in; no DLL folder is needed.
- **Mac:** open `Aurex.app`. Its third-party libraries are compiled in too; the app bundle holds the executable, icon and license notices. Allow Screen Recording and Accessibility in System Settings, then reopen it. Requires macOS 14 or newer.

Settings are saved in `AurexData` beside the app. Keep that folder when updating. It can contain your Discord credentials, so don't share it.

The app also creates its Lua paths and patterns there on first launch, ready to edit.

Aurex checks for a newer release when it opens. Choose **Update and restart**, or use the version number at the bottom of the sidebar to check later. Stop the macro before installing an update. The updater replaces the app and keeps `AurexData`; on Mac, you may need to refresh Screen Recording and Accessibility permissions after replacement.

## Build it yourself

Download the source and open a terminal in its folder. The build script downloads and compiles the libraries, then puts the app and a ZIP in `dist/`. No Python or separate Qt installation is needed. The first build takes longer; later builds reuse the compiled libraries.

### Mac

Install the [Xcode Command Line Tools](https://developer.apple.com/xcode/resources/) and [Homebrew](https://brew.sh), then run:

```sh
brew install cmake ninja
bash build.sh
```

Build on Apple Silicon for Apple Silicon, or on an Intel Mac for Intel.

### Windows

Install Visual Studio 2022's **Desktop development with C++** workload, including **C++ CMake tools for Windows**, and [Git](https://git-scm.com/downloads/win).

Open PowerShell in the source folder and run:

```powershell
.\build.cmd
```

This builds for Windows x64 and sets up the compiler automatically. The ZIP contains the EXE, `LICENSE` and `THIRD_PARTY_NOTICES.txt`.

Licensed under [GPL-3.0](LICENSE).
