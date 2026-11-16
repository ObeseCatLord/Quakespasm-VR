# Networking libraries for the Windows build

The Windows engine links libcurl (mod catalogue and downloads) and OpenSSL
(the ICE/WebRTC transport's DTLS), the same features the Linux packages ship.
Built with MSVC 14.44 (x64, release, dynamic CRT) from vcpkg ports at vcpkg
commit `9eec002df1cd109f8f104cd966a763daf9751489` (tag `2026.10.16`) by
`build-deps.ps1`; `vcpkg-list.txt` records the installed ports:

| Library | Version | License | Runtime file |
| --- | --- | --- | --- |
| curl (`ssl` = Windows Schannel, `http2`) | 8.23.0 | curl | `libcurl.dll` |
| OpenSSL | 3.6.5 | Apache-2.0 | `libssl-3-x64.dll`, `libcrypto-3-x64.dll` |
| nghttp2 (curl HTTP/2) | 1.70.0 | MIT | `nghttp2.dll` |
| zlib (curl) | 1.3.2 | zlib | `z.dll` |

curl uses Windows' own TLS (Schannel) and certificate store. OpenSSL is used
only by the ICE transport. `include/` holds the curl and OpenSSL headers,
`x64/` the import libraries and DLLs (copied beside the engine by the Visual
Studio post-build step), and `licenses/` each port's license text, installed in
packages under `LICENSES/`. Sources: the vcpkg port files at that commit name
the exact upstream source archives and their SHA-512 hashes.
