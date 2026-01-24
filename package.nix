{
  stdenv,
  lib,
  pkg-config,
  glib,
  gtk2,
  SDL2,
  SDL2_ttf,
  SDL2_image,
  libticonv,
  libtifiles2,
  libticables2,
  libticalcs2,
}:

stdenv.mkDerivation rec {
  pname = "tilem";
  version = "2.0";
  src = ./.;
  nativeBuildInputs = [ pkg-config ];
  buildInputs = [
    glib
    gtk2
    SDL2
    SDL2_ttf
    SDL2_image
    libticonv
    libtifiles2
    libticables2
    libticalcs2
  ];
  patches = [ ./gcc14-fix.patch ];
  env.NIX_CFLAGS_COMPILE = toString [ "-lm" ];
  meta = {
    homepage = "http://lpg.ticalc.org/prj_tilem/";
    description = "Emulator and debugger for Texas Instruments Z80-based graphing calculators";
    license = lib.licenses.gpl3Plus;
    maintainers = with lib.maintainers; [ siraben ];
    platforms = lib.platforms.linux ++ lib.platforms.darwin;
    mainProgram = "tilem2";
  };
}
