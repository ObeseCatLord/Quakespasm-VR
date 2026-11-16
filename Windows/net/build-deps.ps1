# Rebuilds Windows/net from pinned vcpkg ports (run in the Windows build guest
# with Visual Studio 2022 Build Tools; then copy the listed outputs).
$ErrorActionPreference = "Stop"
if (!(Test-Path vcpkg)) { git clone --quiet https://github.com/microsoft/vcpkg.git vcpkg }
Set-Location vcpkg
git fetch --quiet --tags
git checkout --quiet 9eec002df1cd109f8f104cd966a763daf9751489   # tag 2026.10.16
.\bootstrap-vcpkg.bat -disableMetrics
.\vcpkg.exe install --triplet x64-windows "curl[core,ssl,http2]" openssl --clean-after-build
# Copy: include\curl, include\openssl -> include\
#       lib\libcurl.lib, libssl.lib, libcrypto.lib -> x64\
#       bin\libcurl.dll, libssl-3-x64.dll, libcrypto-3-x64.dll, nghttp2.dll, z.dll -> x64\
#       share\{curl,openssl,nghttp2,zlib}\copyright -> licenses\<port>.txt
