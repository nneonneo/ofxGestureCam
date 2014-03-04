ofxGestureCam
=============

Copyright (c) 2014 Robert Xiao

MIT License.

For information on usage and redistribution, and for a DISCLAIMER OF ALL
WARRANTIES, see the file, "LICENSE.txt," in this distribution.

See https://github.com/nneonneo/gesturecam for documentation as well as the [OF forums](http://forum.openframeworks.cc/index.php).

This project uses [libuvc](https://github.com/nneonneo/libuvc).

Description
-----------

ofxGestureCam is an Open Frameworks addon for the Creative Gesturecam that runs on Mac OS X and Android (currently).

Caveats
-------

This is basically totally untested outside of my own setup. Use with caution; file bug reports liberally.

On Android, you will have to `chmod 666 /dev/bus/usb/*/*` in order for this library to work. You can do this
either in a `uevent` boot-time script or by using `su` on a rooted device. On an unrooted device, `UsbDevice`
can provide a USB device handle, but you will have to hack this library to open a camera from a `UsbDevice`.
