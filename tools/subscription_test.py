"""Build only the subscription tests against the existing native core library.

The parent build must have compiled turan_core first. This avoids concurrent
out-cef builds while independent feature work is in progress.
"""
from pathlib import Path
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    import build_cef
    folder = ROOT / "artifacts" / "subscription-native"
    folder.mkdir(parents=True, exist_ok=True)
    vc = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio/18/BuildTools/VC/Auxiliary/Build/vcvars64.bat"
    core = ROOT / "out-cef/Release/turan_core.lib"
    if not core.is_file():
        raise SystemExit("Build the existing turan_core target before this test")
    arguments = ["/nologo", "/std:c++17", "/EHsc", "/O2", "/MT", "/utf-8", "/W4", "/DUNICODE", "/D_UNICODE", "/DNOMINMAX", "/DWIN32_LEAN_AND_MEAN", f'/I"{ROOT / "src"}"', str(ROOT / "src/privacy/filter_subscription.cpp"), str(ROOT / "src/privacy/shield.cpp"), str(ROOT / "src/ui/filter_subscription_window.cpp"), str(ROOT / "tests/filter_subscription_tests.cpp"), f'/Fe:"{folder / "filter_subscription_tests.exe"}"', "/link", str(core), "bcrypt.lib", "winhttp.lib", "ws2_32.lib", "normaliz.lib", "shell32.lib", "ole32.lib", "user32.lib", "gdi32.lib"]
    arguments += [str(p) for p in (ROOT / "out-cef/native-filter").rglob("*.lib")]
    response = folder / "compile.rsp"
    response.write_text("\n".join(a if a.startswith(("/", '"')) or " " not in a else '"' + a + '"' for a in arguments), encoding="utf-16")
    command = folder / "compile.cmd"
    command.write_text(f'@echo off\r\ncall "{vc}" >nul\r\nif errorlevel 1 exit /b %errorlevel%\r\ncl @"{response}"\r\n', encoding="utf-8")
    result = subprocess.run(["cmd.exe", "/d", "/c", str(command)], cwd=folder)
    if result.returncode:
        raise SystemExit(result.returncode)
    subprocess.run([str(folder / "filter_subscription_tests.exe"), str(ROOT)], check=True, cwd=ROOT)


if __name__ == "__main__":
    main()
