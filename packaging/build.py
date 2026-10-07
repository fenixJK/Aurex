import argparse
import hashlib
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import urllib.request
import venv


ROOT = Path(__file__).resolve().parents[1]
QT_VERSION = "6.8.3"
OPENCV_VERSION = "4.13.0"
OPENCV_SHA256 = "1d40ca017ea51c533cf9fd5cbde5b5fe7ae248291ddf2af99d4c17cf8e13017d"
QT_ARCHIVES = {
    "qtbase": "56001b905601bb9023d399f3ba780d7fa940f3e4861e496a7c490331f49e0b80",
    "qtwebsockets": "fc6310c1b12d6e22a31062727aa6574ed69239d036c8a4247b3b089902709b11",
}


def run(*args, env=None):
    subprocess.run([str(arg) for arg in args], cwd=ROOT, env=env, check=True)


def require(*commands):
    for command in commands:
        if not shutil.which(command):
            raise RuntimeError(f"Install {command} and make sure it is on PATH, then try again.")


def download(url, destination, expected):
    if not destination.exists():
        partial = destination.with_suffix(".download")
        urllib.request.urlretrieve(url, partial)
        partial.replace(destination)
    with destination.open("rb") as source:
        digest = hashlib.sha256()
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    if digest.hexdigest() != expected:
        raise RuntimeError(f"Download checksum mismatch. Delete {destination} and try again.")


def windows_environment():
    require("powershell.exe")
    vswhere = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
    if not vswhere.is_file():
        raise RuntimeError("Install Visual Studio 2022 with Desktop development with C++ first.")
    found = subprocess.check_output([
        str(vswhere), "-latest", "-version", "[17.0,18.0)", "-products", "*", "-requires",
        "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"], text=True).strip()
    if not found:
        raise RuntimeError("Install Visual Studio 2022's C++ tools and a Windows SDK first.")
    setup = Path(found) / "Common7/Tools/VsDevCmd.bat"
    command = f'cmd.exe /d /u /s /c ""{setup}" -no_logo -arch=x64 -host_arch=x64 >nul && set"'
    environment = subprocess.check_output(command, encoding="utf-16le")
    for line in environment.splitlines():
        key, separator, value = line.partition("=")
        if key and separator:
            os.environ[key] = value
    require("cl", "dumpbin")


def windows_qt(deps, jobs):
    qt = deps / "Qt" / QT_VERSION / "windows-static"
    config = ROOT / "cmake/WindowsQt.cmake"
    signature = hashlib.sha256(config.read_bytes()).hexdigest()
    stamp = qt / ".aurex-static"
    if stamp.is_file() and stamp.read_text() == signature:
        return qt
    for module, checksum in QT_ARCHIVES.items():
        name = f"{module}-everywhere-src-{QT_VERSION}"
        archive = deps / f"{name}.tar.xz"
        download(f"https://download.qt.io/archive/qt/6.8/{QT_VERSION}/submodules/{archive.name}", archive, checksum)
        run("tar", "-xJf", archive, "-C", deps)
        build_dir = deps / f"{module}-build-static"
        options = ["-C", config] if module == "qtbase" else [f"-DCMAKE_PREFIX_PATH={qt}"]
        run("cmake", "-S", deps / name, "-B", build_dir, "-G", "Ninja",
            "-DCMAKE_BUILD_TYPE=Release", "-DBUILD_SHARED_LIBS=OFF", "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded",
            "-DQT_BUILD_TESTS=OFF", "-DQT_BUILD_EXAMPLES=OFF", f"-DCMAKE_INSTALL_PREFIX={qt}", *options)
        run("cmake", "--build", build_dir, "--parallel", jobs)
        run("cmake", "--install", build_dir)
        source = deps / name
        for folder in (source / "LICENSES", source / "src/3rdparty"):
            for path in folder.rglob("*"):
                label = path.name.lower()
                if path.is_file() and (folder.name == "LICENSES" or label.startswith(("license", "copying", "copyright", "notice")) or label == "qt_attribution.json"):
                    destination = qt / "share/aurex-notices" / module / path.relative_to(source)
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(path, destination)
    stamp.write_text(signature)
    return qt


def build(jobs):
    system = platform.system()
    if system not in ("Darwin", "Windows"):
        raise RuntimeError("Build Aurex on macOS or Windows.")
    mac = system == "Darwin"
    require("cmake", "git", "tar")
    if mac:
        require("xcrun", "make", "perl", "bash", "curl")
        run("xcrun", "--find", "clang++")
        machine = platform.machine()
        if machine not in ("arm64", "x86_64"):
            raise RuntimeError(f"Unsupported Mac architecture: {machine}")
    else:
        windows_environment()

    workspace = ROOT / ("build-local" if mac else "build-windows-static")
    deps = workspace / "deps"
    deps.mkdir(parents=True, exist_ok=True)
    python = deps / "python" / ("bin/python" if mac else "Scripts/python.exe")
    if not python.is_file():
        venv.create(deps / "python", with_pip=True)
    if mac:
        qt = deps / "Qt" / QT_VERSION / "macos"
        if not (qt / "lib/cmake/Qt6WebSockets/Qt6WebSocketsConfig.cmake").is_file():
            run(python, "-m", "pip", "install", "aqtinstall==3.3.0")
            run(python, "-m", "aqt", "install-qt", "mac", "desktop", QT_VERSION, "clang_64",
                "--modules", "qtwebsockets", "--archives", "qtbase", "qtwebsockets", "-O", deps / "Qt")
    else:
        if not (python.parent / "ninja.exe").is_file():
            run(python, "-m", "pip", "install", "ninja==1.13.2")
        os.environ["PATH"] = str(python.parent) + os.pathsep + os.environ.get("PATH", "")
        qt = windows_qt(deps, jobs)

    if mac:
        crypto = deps / "openssl-install"
        if not (crypto / "lib/libcrypto.a").is_file():
            run("bash", ROOT / "packaging/host-crypto-macos.sh", deps)
    else:
        vcpkg = deps / "vcpkg"
        crypto = vcpkg / "installed/x64-windows-static"
        if not (crypto / "lib/libcrypto.lib").is_file():
            if not (vcpkg / ".git").is_dir():
                run("git", "clone", "--depth", "1", "https://github.com/microsoft/vcpkg.git", vcpkg)
            if not (vcpkg / "vcpkg.exe").is_file():
                run("cmd.exe", "/c", vcpkg / "bootstrap-vcpkg.bat", "-disableMetrics")
            run(vcpkg / "vcpkg.exe", "install", "openssl:x64-windows-static")

    generator = [] if mac else ["-G", "Ninja"]
    vision = deps / "opencv-install"
    signature = hashlib.sha256((ROOT / "cmake/ReleaseVision.cmake").read_bytes()).hexdigest()
    stamp = vision / f".aurex-{OPENCV_VERSION}"
    if not stamp.is_file() or stamp.read_text() != signature:
        archive = deps / f"opencv-{OPENCV_VERSION}.tar.gz"
        download(f"https://github.com/opencv/opencv/archive/refs/tags/{OPENCV_VERSION}.tar.gz", archive, OPENCV_SHA256)
        source = deps / f"opencv-{OPENCV_VERSION}"
        run("tar", "-xzf", archive, "-C", deps)
        options = ["-DCMAKE_OSX_DEPLOYMENT_TARGET=14.0"] if mac else []
        run("cmake", "-S", source, "-B", deps / "opencv-build", *generator,
            "-C", ROOT / "cmake/ReleaseVision.cmake", "-DCMAKE_BUILD_TYPE=Release",
            f"-DCMAKE_INSTALL_PREFIX={vision}", *options)
        run("cmake", "--build", deps / "opencv-build", "--config", "Release", "--parallel", jobs)
        run("cmake", "--install", deps / "opencv-build", "--config", "Release")
        stamp.write_text(signature)

    app = workspace / "app"
    run("cmake", "-S", ROOT, "-B", app, *generator, "-DCMAKE_BUILD_TYPE=Release",
        "-DBUILD_TESTING=OFF", "-DOpenCV_STATIC=ON", f"-DCMAKE_PREFIX_PATH={qt};{vision}",
        f"-DOPENSSL_ROOT_DIR={crypto}")
    run("cmake", "--build", app, "--config", "Release", "--parallel", jobs)
    env = dict(os.environ, RUNNER_TEMP=str(deps), AUREX_QT_ROOT=str(qt),
               PATH=str(qt / "bin") + os.pathsep + os.environ.get("PATH", ""))
    if mac:
        run("bash", ROOT / "packaging/macos.sh", "macOS-arm64" if machine == "arm64" else "macOS-x64", app, env=env)
    else:
        run("powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
            ROOT / "packaging/windows.ps1", "-BuildDir", app, env=env)
    print(f"\nDone. Your Aurex ZIP is in {ROOT / 'dist'}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Build and package Aurex. Dependencies stay in its build folder.")
    parser.add_argument("--jobs", type=int, default=3, help="parallel compile jobs (default: 3)")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be at least 1")
    try:
        build(args.jobs)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        sys.exit(f"Build stopped: {error}")
