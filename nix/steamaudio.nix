{ lib, stdenv, fetchFromGitHub, cmake, pffft, libmysofa, flatbuffers, zlib }:
stdenv.mkDerivation rec {
  pname = "steamaudio";
  version = "4.8.1";
  src = fetchFromGitHub {
    owner = "ValveSoftware";
    repo = "steam-audio";
    rev = "v${version}";
    sha256 = "0id6277ndrl0d4b04i5zc7v36nxh26ahd7x0jy8wid6shlkij2ci";
  };
  sourceRoot = "${src.name}/core";
  # The PFFFT path has odd complex-bin strides; the SIMD unaligned branch
  # must also use an unaligned accumulator load (multichannel reverb crashes).
  patches = [ ./steamaudio-unaligned-accumulate.patch ];
  patchFlags = [ "-p2" ];
  nativeBuildInputs = [ cmake flatbuffers ];
  buildInputs = [ pffft libmysofa flatbuffers zlib ];
  postPatch = ''
    # SDK post-build commands stage headers/libraries into sibling integration dirs.
    chmod -R u+w ..
    # Upstream's dependency downloader layout must not override system zlib.
    sed -i '/^set(ZLIB_ROOT /d; /^set(ZLIB_INCLUDE_DIR /d' build/FindMySOFA.cmake
  '';
  cmakeFlags = [
    "-DSTEAMAUDIO_ENABLE_IPP=OFF"
    "-DSTEAMAUDIO_ENABLE_MKL=OFF"
    "-DSTEAMAUDIO_ENABLE_EMBREE=OFF"
    "-DSTEAMAUDIO_ENABLE_RADEONRAYS=OFF"
    "-DSTEAMAUDIO_ENABLE_TRUEAUDIONEXT=OFF"
    "-DSTEAMAUDIO_BUILD_TESTS=OFF"
    "-DSTEAMAUDIO_BUILD_BENCHMARKS=OFF"
    "-DSTEAMAUDIO_BUILD_SAMPLES=OFF"
    "-DSTEAMAUDIO_BUILD_DOCS=OFF"
  ];
  installPhase = ''
    runHook preInstall
    mkdir -p $out/lib/pkgconfig $out/include $out/share/licenses/steamaudio
    cp src/core/libphonon.so $out/lib/
    cp ../src/core/phonon.h src/core/phonon_version.h $out/include/
    cp ../../LICENSE.md $out/share/licenses/steamaudio/
    cat > $out/lib/pkgconfig/steamaudio.pc <<EOF
    prefix=$out
    libdir=\''${prefix}/lib
    includedir=\''${prefix}/include
    Name: Steam Audio
    Description: Steam Audio C API (CPU build)
    Version: ${version}
    Libs: -L\''${libdir} -lphonon
    Cflags: -I\''${includedir}
    EOF
    runHook postInstall
  '';
  meta = {
    description = "Steam Audio spatial audio SDK";
    homepage = "https://valvesoftware.github.io/steam-audio/";
    license = lib.licenses.asl20;
    platforms = lib.platforms.linux;
  };
}
