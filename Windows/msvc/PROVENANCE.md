# Microsoft C++ runtime for Windows packages

`quakespasm-openvr.exe` is built with the MSVC 14.44 toolset (Visual Studio
2022 Build Tools) and imports `MSVCP140.dll` and `VCRUNTIME140.dll`. Windows
packages ship them beside the executable (app-local deployment), so the game
starts on PCs without the Visual C++ Redistributable installed.

Both files are unmodified, Authenticode-signed by Microsoft Corporation, and
copied from the build machine's
`VC\Redist\MSVC\14.44.35112\x64\Microsoft.VC143.CRT\` (file version
14.44.35211.0):

- `x64/msvcp140.dll` SHA-256 `0f885b509a685d2bbfa652fed26b5fb31d88fbdab0a978c641d1c7b8aa460aa9`
- `x64/vcruntime140.dll` SHA-256 `d5e4d9a3e835fa679450145d6a7d94e36573a509317111904d9b3712c30d9066`

They are redistributable files under the Visual Studio license terms. Update
them together with the build toolset; the app-local copy must not be older
than the toolset that built the engine.
