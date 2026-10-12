# Static HTTPS Turan update distribution (publisher-side)

This procedure prepares **signed updates**. It does **not** create a NorthByte
server, create real corporate publisher keys, authorize production auto-updates,
or perform Windows Authenticode signing. The shipped 0.2.0-dev candidate has no
publisher endpoint and fails closed. The selected 1.0 release contract permits
Windows 11 x64 with **automatic updates disabled until explicitly provisioned**.

## Signing and staging

Keep the private key outside the source directory, dist archives and server.
The examples below are placeholders, not a real NorthByte configuration.
Only execute them when you control a real HTTPS host and a protected key.
The CMake-generated runtime-files.txt is the exact runtime inventory.

```powershell
# Example ONLY: replace C:\secure\..., host and version with owned values.
python tools/update_release.py provision --public-key C:\secure\turan-public.pem --manifest-url https://updates.example.org/stable/release.manifest --output C:\secure\publisher_trust.h --channel stable --sequence 1 --openssl C:\Tools\openssl.exe
python tools/update_release.py publish --runtime .\out-cef\app\Release --inventory .\out-cef\runtime-files.txt --private-key C:\secure\turan-private.pem --expected-public-key C:\secure\turan-public.pem --base-url https://updates.example.org/stable/files/ --version 1.0.1.0 --kind installer --days 7 --output C:\secure\publication\release.manifest --channel stable --sequence 2 --openssl C:\Tools\openssl.exe
python tools/update_distribution.py assemble --runtime .\out-cef\app\Release --manifest C:\secure\publication\release.manifest --public-key C:\secure\turan-public.pem --openssl C:\Tools\openssl.exe --destination C:\secure\publication\staged
python tools/update_distribution.py verify --deployment C:\secure\publication\staged --public-key C:\secure\turan-public.pem --openssl C:\Tools\openssl.exe
```

Upload the **contents** of `staged/payload/` to the exact signed `base` URL.
Then upload `staged/manifest.txt` to the pre-provisioned manifest URL and
`staged/manifest.txt.sig` to that URL + `.sig`, **last** and from an atomic
server-side publication directory. The server must preserve the exact bytes,
return HTTP 200 without redirect, and use a trusted TLS certificate. Do not
serve a half-uploaded manifest/signature pair. Retain previous verified
releases until the new engine health-rollback test passes.

During deployment, refuse stale or expired manifests, incorrect inventory,
unexpected files, wrong signatures, wrong hash or different RSA public key.
The tool checks the signed metadata and copied files **locally**, not whether
public URLs are reachable. Perform an authenticated real endpoint/rollback test
on Windows before advertising any online update feature. Never use disposable
fixture RSA keys in production.
