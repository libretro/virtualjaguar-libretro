# Raspberry Pi standalone app

Use the existing **Linux ARM64** app downloads with a **64-bit userland**
(`uname -m` should report `aarch64`, and `dpkg --print-architecture` should report
`arm64`). A 64-bit CPU with a 32-bit OS cannot run these packages. Per-SoC
RetroArch cores are separate downloads, including 32-bit cores for older Pis.

These are generic ARM64 packages, not a claim of tested Pi GPU compatibility.
The standalone app requires **desktop OpenGL 3.3**. Pi models, OS releases and
Mesa drivers differ; an ARM64 CPU alone does not establish graphics support.
Pi hardware has not been part of the release app qualification matrix. Check
the reported desktop GL version before installing; OpenGL ES is a different API.

## Install and check the graphics driver

On a current 64-bit Raspberry Pi OS desktop, prefer the ARM64 `.deb`: apt resolves
its declared runtime dependencies, including `libgl1`, and normally installs its
recommended display/audio libraries too.

```sh
sudo apt update
sudo apt install ./virtualjaguar_3.13.1_arm64.deb
sudo apt install mesa-utils
glxinfo -B
virtualjaguar
```

Run the graphics check in your desktop session. Look at the **OpenGL core profile**
version, not the OpenGL ES version. If it is below 3.3, do not force a higher
version string: that does not add missing GPU features. Keep the OS/Mesa driver
up to date, or use the matching per-SoC core with RetroArch's supported renderer.
Pi 1/2/3 and Zero are not advertised as supported standalone app hardware.

The ARM64 AppImage is the portable alternative. If FUSE is unavailable, use
`./VirtualJaguar-3.13.1-linux-aarch64.AppImage --appimage-extract-and-run`.
The ARM64 Flatpak uses the packaged runtime, but still needs a compatible host
graphics driver. Neither format can add OpenGL capabilities to the Pi GPU.

## Shaders and other libraries

Official `.deb`, AppImage and Flatpak packages bundle the pinned, matching ARM64
**librashader** runtime. Do not install an unrelated system shader library to make
an official package work. The `.deb` installs its copy alongside the executable
in `/usr/lib/virtualjaguar`; AppImage and Flatpak keep it inside their package.
Display drivers, audio backends and window-system libraries still come from the OS.

If Settings > Video > Shader reports a missing runtime, first reinstall the complete
package and remove an unintended `VJ_LIBRASHADER` environment override. For a source
build or an intentionally unbundled distro package, follow [the shader runtime
instructions](../app/README.md#getting-the-shader-runtime), using
`aarch64-unknown-linux-gnu`. An x86-64 `.so` will not load on ARM64. Plain video
still requires the app's OpenGL context even when shaders are disabled.

## Why no per-SoC app downloads yet?

The existing ARM64 app and bundled core already select NEON. Tuning Cortex-A53,
A72 or A76 scheduling is a separate performance experiment; it does not solve
graphics compatibility or dependency installation. Keep one portable ARM64 app
until real-device frame-time measurements justify extra builds and a Pi GPU
smoke test can qualify them. The existing per-SoC libretro core downloads remain
available for users who want those optimizations now.

References: [Raspberry Pi OS architecture and packages](https://www.raspberrypi.com/documentation/computers/os.html),
[official OS images](https://www.raspberrypi.com/software/operating-systems/).
