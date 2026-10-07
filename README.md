# Aurex

A Bee Swarm Simulator macro for Windows and macOS. Still being worked on, so bug reports and recordings help a lot.

## Install

Download the ZIP for your computer from [Releases](https://github.com/fenixJK/Renova/releases), when available, and extract it into a folder you can write to.

- **Windows:** open `Aurex.exe`. Its libraries are compiled in; no DLL folder is needed.
- **Mac:** open `Aurex.app`. Allow Screen Recording and Accessibility in System Settings, then reopen it. Requires macOS 14 or newer.

Settings are saved in `AurexData` beside the app. Keep that folder when updating. It can contain your Discord credentials, so don't share it.

## Build it yourself

Download the source and open a terminal in its folder. The build helper downloads the libraries, compiles Aurex and puts a ready-to-use ZIP in `dist/`. The first build takes longer; later builds reuse the downloads.

### Mac

Install the [Xcode Command Line Tools](https://developer.apple.com/xcode/resources/) and [Homebrew](https://brew.sh), then run:

```sh
brew install cmake python git
python3 packaging/build.py
```

Build on Apple Silicon for Apple Silicon, or on an Intel Mac for Intel.

### Windows

Install Visual Studio 2022's **Desktop development with C++** tools, [CMake](https://cmake.org/download/), [Git](https://git-scm.com/downloads/win) and [Python 3.10+](https://www.python.org/downloads/windows/). Make sure CMake and Git are on PATH.

Open **Developer PowerShell for VS 2022** in the source folder and run:

```powershell
py packaging/build.py
```

This builds for Windows x64, including a static build of Qt. Allow extra time and disk space for the first build. The ZIP contains the EXE and license notices.

Licensed under [GPL-3.0](LICENSE).
