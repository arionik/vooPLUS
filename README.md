# vooPLUS
Play mov and mp4 files in vooya

Output should be a shared library that can be loaded by [vooya](http://www.offminor.de). On macOS, build `voo+.m`, linking to `AVFoundation` and on Linux or Windows build `voo+.c`, linking to the shared libraries of FFmpeg. FFmpeg DLLs for Windows can nicely be cross-compiled on e.g. Ubuntu.

Testing can be done by putting the plugin shared library into vooya's plugin folder or by running:
> vooya --plugin \<\<output library\>\>
