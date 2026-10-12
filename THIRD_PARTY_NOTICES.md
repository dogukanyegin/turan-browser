# Third-party notices

Turan-authored C++/Python/UI assets are MIT licensed; see LICENSE. Bundled
third-party data and native libraries retain the following original licenses.

Engine: unmodified CEF minimal Windows64 distribution
`154.0.34+g14c5a08+chromium-154.0.8037.98`, pinned in `cef.lock.json`.
The immutable 0.1.0 baseline packages contain CEF 144.0.36 / Chromium
144.0.7559.264 and their matching original credits.
Binary origin: https://cef-builds.spotifycdn.com/

Each application package includes original `CEF-LICENSE.txt` (CEF BSD-style
license) and full supplied `CEF-CREDITS.html` (Chromium/third-party credits),
Turan `LICENSE.txt` and this notice. Consult those texts for Blink, V8, ANGLE,
ICU and other engine dependency terms. Required engine filenames and legal
attributions remain unchanged.

NSIS 3.13 is a build-time compiler downloaded from official SourceForge. Its
LICENSE/COPYING files remain with the local compiler distribution. NSIS permits
distribution of generated installers; the compiler is not shipped in the app.

Optional compatibility tests fetch uBlock Origin Lite, Dark Reader and Bitwarden
publisher release artifacts, pinned in tests/extensions.thirdparty.lock.json.
These test dependencies are not redistributed in application or source archives.
Their original license files remain in the local test cache. Publisher sources:
https://github.com/uBlockOrigin/uBOL-home,
https://github.com/darkreader/darkreader,
https://github.com/bitwarden/clients. No Bitwarden server/account is configured.

Turan adds no Chrome logos, Google API/OAuth keys or proprietary Widevine binaries.
The prebuilt engine remains governed by its original licenses and notices.

Native content filtering statically links the unmodified RE2 2025-11-05
(google/re2, BSD-3-Clause) and Abseil 20260817.0 (abseil/abseil-cpp, Apache-2.0)
source releases. Versions, exact origins and SHA-256 are in filter.lock.json.
Their full upstream license texts are in resources/licenses/RE2-LICENSE.txt and
resources/licenses/ABSEIL-LICENSE.txt. No external library binary is downloaded.

resources/public_suffix_list.dat is the unchanged Mozilla/Public Suffix List at
publicsuffix/list commit 3929462652695bad04f0a27afb600974014a3c8b.
It retains its original notices and is MPL-2.0 licensed; the complete license is
resources/licenses/MPL-2.0.txt. The exact corresponding source can be obtained at
https://raw.githubusercontent.com/publicsuffix/list/3929462652695bad04f0a27afb600974014a3c8b/public_suffix_list.dat
The pinned original file is also included in Turan's matching source archive.

Optional QA compiles official EasyList/EasyPrivacy corpora pinned in
tests/filter.corpus.lock.json. These downloaded lists are not shipped or enabled
in Turan application or source archives. Upstream lists are dual GPL-3.0-or-later /
CC-BY-SA-3.0-or-later; see https://easylist.to/pages/licence.html and the notices
in each original list. Implementing syntax support does not include or relicense
the lists, and this test does not claim full compatibility.

The native optional subscription feature fetches those official lists only after
explicit user consent, retaining their notices in the profile cache and displaying
their source/license. Downloaded cache/profile data is excluded from installers,
portable and source archives. No list is rebranded or relicensed by Turan.

The optional external Tor Browser integration embeds a generated file-hash
catalog, not its binaries, logo or profile. Tor Browser 15.0.24 is downloaded
only for isolated optional QA/catalog maintenance, retaining TorBrowser/Docs/Licenses
notices. Its OpenPGP key authenticates that QA package and is not a NorthByte
signing key. GnuPG/7z are optional developer tools, not redistributed. This feature
does not imply Tor Project affiliation or Tor Browser-equivalent anonymity.
