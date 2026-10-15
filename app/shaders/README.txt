First-party presets that ship with the app (copied next to the executable as
shaders/).  They are original to this project and GPL-licensed with the rest of
the repository.

  vj-sharp-bilinear.slangp   crisp scaling at non-integer window sizes
  vj-scanlines.slangp        scanlines + optional RGB mask, with parameters

slang-shaders/ (when present) is an unmodified subset of RetroArch's
https://github.com/libretro/slang-shaders, fetched at build time; every shader
keeps its own licence (see slang-shaders/NOTICE-virtualjaguar.txt).  For the rest
of the collection, point Settings > Video > Preset folder at a clone of it.

macOS: OpenGL stops at 4.1 there, so presets that need OpenGL 4.2/4.3 features
(CRT-Geom, CRT-Hyllian and a few more: arrays of arrays) cannot compile.  The
preset list greys them out; the plain picture is shown if one is chosen anyway.
