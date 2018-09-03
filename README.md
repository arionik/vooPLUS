# vooPLUS
Play mov and mp4 files in vooya

Output should be a shared library that can be loaded by [vooya](http://www.offminor.de). On macOS, build `voo+.m`, linking to the necessary frameworks (`Cocoa`, `AVFoundation`, `CoreVideo`, `CoreMedia`) and on Linux or Windows build `voo+.c`, linking to the shared libraries of FFmpeg (`avcodec`, `avformat`, `avutil`). FFmpeg DLLs for Windows can nicely be cross-compiled on e.g. Ubuntu.

Testing can be done by putting the plugin shared library into vooya's plugin folder or by running:
```
vooya --plugin <<output library>>
```

[voo_plugin.h](voo_plugin.h) stems from [vooya's plugin repository](https://github.com/arionik/vooya-Plugin-API). FFmpeg version 3.4.1 was used.
