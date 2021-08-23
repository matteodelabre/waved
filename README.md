## waved

_waved_ is aimed at becoming a userspace driver for the reMarkable 2 E-Ink controller.

**Disclaimer: This is still a prototype. It might damage your E-Ink display.**

### Demo

### Building

To compile the demo, from the [base](https://github.com/toltec-dev/toolchain/pkgs/container/base) Toltec Docker image:

```sh
cmake \
    -DCMAKE_TOOLCHAIN_FILE=/usr/share/cmake/arm-linux-gnueabihf.cmake \
    -DCMAKE_BUILD_TYPE=Release
    -S /host -B /host/build
cmake --build /host/build --verbose
```

Resulting binaries are `build/waved` (to launch the demo) and `build/waved-dump-wbf` (to print information about a WBF file).

### Roadmap

* Expose shared memory RGB framebuffer.
* Implement rm2fb server model.
* Package in Toltec.
* Damage tracking (see [mxc\_epdc\_fb\_damage](https://github.com/pl-semiotics/mxc_epdc_fb_damage) & [libqsgepaper-snoop](https://github.com/pl-semiotics/libqsgepaper-snoop)).
* User configuration (flip screen X/Y, invert colors, set contrast, set sleep timeout, …).
* Implement “Regal” algorithm.
* Merge updates.
