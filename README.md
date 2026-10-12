# Turan Browser

**NorthByte Norway Tec Ltd.** · Native Windows browser · C++17 / Win32 / Chromium Embedded Framework

**Current version:** `0.9.0-beta.1` · **Channel:** Beta · **Target:** Windows 11 x64 · **Engine:** CEF 154.0.34 / Chromium 154.0.8037.98

> **Beta software, not 1.0.0 Stable.** This repository contains the browser **source code, tests, build scripts, and technical documentation**. Windows installer and portable binaries belong in **GitHub Releases**, not in this Git repository. Automatic production updating is not configured.

## Türkçe — Proje hakkında

Turan Browser, **NorthByte Norway Tec Ltd.** tarafından geliştirilen, Chromium Embedded Framework (CEF) tabanlı yerel Windows tarayıcısıdır. Ana uygulama C++17 ve Win32 üzerinde geliştirilmiştir. Electron veya Qt kullanmaz; CEF motoru kilitlenmiş sürüm üzerinden edinilir.

### 0.9.0 Beta 1 ile gelen geliştirmeler

- **Bağımsız Beta kanalı:** `0.9.0-beta.1` sürüm kimliği, `TuranBrowserBeta` kurulum/profil alanı ve geliştirme/kararlı sürümlerden ayırma.
- **Tarayıcı ve oturum:** sekmeler, yer imleri, geçmiş, indirmeler, sekme sabitleme/sessize alma/gruplama, oturum kurtarma ve hasarlı oturum verisini koruma.
- **Gizlilik ve güvenlik:** özel oturum bağlamlarının ayrılması, Turan Shield istek filtreleme, site izinleri, DoH yapılandırması ve yerel şifreli kasa.
- **Deneysel Chromium entegrasyonu:** isteğe bağlı gerçek Chromium sekme/pencere modeli, Turan araç paneli ve seçili Manifest V3 uzantı senaryoları. **Varsayılan arayüz değildir.**
- **Sürüm güvenilirliği:** Windows derleme, CTest, Python regresyon testleri, kurulum/portable paketleme, profil kurtarma, eklenti işlevleri ve 60 dakikalık dayanıklılık kontrolü.

### Windows 11 x64 üzerinde kaydedilmiş Beta kabul sonuçları

| Kontrol | Sonuç |
|---|---|
| Genişletilmiş Windows kabulü | **12/12 PASS** — `passed_limited_scope` |
| Yerel C++ CTest | **9/9 PASS** |
| Son derleme Python testleri | **56 başarılı**, 10 atlandı |
| CEF dayanıklılık testi | **60 dakika PASS** |
| Dağıtım doğrulaması | `PACKAGE_INTEGRITY_PASSED_NOT_STABLE_ACCEPTANCE` |
| Eklentiler | uBlock Origin Lite, Dark Reader ve Bitwarden için **sınırlı senaryolar PASS** |

Bu sonuçlar Windows 11 üzerinde kaydedilen **sınırlı test kapsamına** aittir; bağımsız güvenlik sertifikası veya her eklenti/cihaz için tam uyumluluk anlamına gelmez. Kaynak belgeleri: [Beta kontrol listesi](BETA_BUILD_CHECKLIST.md), [test raporu](TEST_REPORT.md), [geliştirici notları (TR)](docs/DEVELOPER_NOTES_TR.md).

## Repository structure

```text
src/                 C++17 application and native components
tests/               Python/Node integration, regression and CEF fixtures
tools/               Windows build, release and distribution verification
resources/           App UI assets and localization
packaging/           NSIS installer and portable packaging
.github/workflows/   Windows engineering CI (not Stable certification)
docs/                Architecture, developer notes, detailed status and releases
CMakeLists.txt        C++ build configuration
cef.lock.json        Pinned CEF dependency metadata
release.json         Version/channel identity
```

## Build from source (Windows 11 x64)

**Prerequisites:** Visual Studio Build Tools with MSVC and Windows SDK, CMake, Python 3, and Node.js 22+ for selected integration tests. Node.js is not a runtime requirement of the distributed browser. Prebuilt CEF is obtained by the project's build tooling; review its lock file and [BUILD_WINDOWS](docs/BUILD_WINDOWS.md).

```powershell
python tools/release_gate.py
python tools/build_cef.py all
```

The build can create the following original Windows release artifacts when all checks succeed:

```text
dist/TuranBrowser-0.9.0-beta.1-windows-x64-setup.exe
dist/TuranBrowser-0.9.0-beta.1-windows-x64-portable.zip
dist/TuranBrowser-0.9.0-beta.1-source.zip
```

Run the extended workstation acceptance **on a Windows machine**:

```powershell
python tools/windows_acceptance.py --runtime .\out-cef\app\Release --extended --soak-minutes 60
```

A CI workflow and reconstructed source copy do not themselves reproduce the actual Windows acceptance evidence. Review the output and ensure built binaries match the release assets.

## Download and version history

Check [GitHub Releases](../../releases) for published binaries. **Do not use a release download link until the real tested assets have been uploaded.** For the Beta release description use [v0.9.0-beta.1 notes](docs/releases/v0.9.0-beta.1.md).

- [CHANGELOG](CHANGELOG.md) — 0.1.0, 0.2.0-dev, 0.9.0 Beta 1 and proposed 1.0 Stable
- [Developer notes (Turkish)](docs/DEVELOPER_NOTES_TR.md)
- [Original source engineering guide](docs/ORIGINAL_SOURCE_README.md)
- [Installation](INSTALLATION.md) · [Privacy](PRIVACY.md) · [Security](SECURITY.md) · [Third-party notices](THIRD_PARTY_NOTICES.md)
- [Repository publication guide](docs/REPOSITORY_PUBLICATION.md) · [Contributing](CONTRIBUTING.md)

### Known limitations

Production automatic update endpoint and release-key infrastructure are not enabled. The Chromium-integrated window is opt-in. Full Chrome Web Store compatibility, real Bitwarden authentication/autofill, Tor anonymity, independent penetration testing, all physical-media devices, accessibility and broad low-memory performance are outside the claimed release scope. The earlier package tests also surfaced a `portable.flag` duplicate ZIP-entry warning in one test fixture; do not conceal any persistent warnings in later builds.

## English summary

Turan Browser is an experimental native Windows 11 x64 browser built with **C++17, Win32 and CEF 154 / Chromium 154**. This repository contains source, test automation and documentation. Limited-scope Beta acceptance completed 12/12 Windows checks including a 60-minute local soak. It does **not** establish full browser security certification, default Chrome-like UI parity, or production automatic update availability.

## License and release provenance

The Turan source is distributed under the [MIT License](LICENSE); bundled third-party components retain their own licensing terms as described in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

**Source provenance:** this upload-ready repository bundle was assembled by overlaying the supplied Beta source snapshot with four subsequently prepared **test-tool fixes**. It is **not** the exact byte-identical `TuranBrowser-0.9.0-beta.1-source.zip` created on the Windows test machine, so its ZIP SHA-256 will differ. To publish historically exact Beta binaries and the matching Beta source archive, upload **those unchanged three artifacts** separately to GitHub Releases. See [publication guide](docs/REPOSITORY_PUBLICATION.md).
