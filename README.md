ascii-chat 📸
==========

ASCII video chat.

Probably the first command line video chat program.

It just prints ASCII, so it works on your rxvt-unicode in OpenBox, a Putty SSH
session, and even iTerm or Kitty.app on macOS.

It even works in an initial UNIX login shell, i.e. the login shell that runs
'startx'.

Eventually it will support 3+ simultaneous people, 'google-hangouts' style, and sound via PulseAudio or something.

![Animated demonstration](http://i.imgur.com/E4OuqvX.gif)


Dependencies
==========
- Most people: `apt-get install clang libopencv-dev libjpeg-dev`
- ArchLinux masterrace: `pacman -S clang opencv libjpeg-turbo`
- macOS: `brew install opencv@4 jpeg-turbo`


Build and run
==========
- Clone this repo onto a computer with a webcam.
- Install the dependencies.
- run `make`.
- run `./bin/server -p 90001` in one terminal, and then
- run `./bin/client -p 90001` in another.

Use `make clean debug` as you edit and test code.

Check the Makefile to see how it works.


Command line flags
=========

NOTE: run `./bin/server -h` to see these options

- `-a --address`: IPv4 address (default: 0.0.0.0)
- `-p --port`: TCP port (default: 90001)  
- `-x --width`: Render width (default: 110) NOTE: unused right now, auto-calculated
- `-y --height`: Render height (default: 70) NOTE: unused right now, auto-calculated
- `-c --webcam-index`: Webcam device index (0-based, server only, default: 0)
- `-f --webcam-flip`: Horizontally flip image (server only, default: 1)
- `-C --color`: Enable colored ASCII output (server and client)
- `-b --background-color`: Enable background colored mode with contrasting text (server only)
- `-h --help`: Show help message

Usage
==========

Start the server:
```bash
./bin/server [options]
```

Connect with a client:
```bash
./bin/client [options]
```

### New Color Feature

You can now enable colored ASCII output that preserves the original colors from
your webcam! Use the `--color` or `-C` flag:

**Server with color:**
```bash
./bin/server --color
```

**Client with color:**
```bash
./bin/client --color
```

The colored output uses ANSI escape codes to colorize each ASCII character based
on the RGB values from the original webcam image, creating a much more vibrant
and realistic representation.


TODO
==========
- [x] Client should continuously attempt to reconnect
- [ ] switch Client "-a/--address" option to "host" and make it accept domains as well as ipv4
- [x] Colorize ASCII output
- [ ] Refactor image processing algorithms
- [x] client reconnect logic
- [x] terminal resize events
- [x] client requests a frame size
- [x] Client should gracefully handle `frame width > term width`
- [x] Client should gracefully handle `term resize` event
- [ ] Compile to WASM/WASI and run in the browser


Notes
==========
**Performance Optimizations for Colored Mode:**
- **Color Quantization**: Reduces 16M colors to 512 colors for smaller, more consistent frames
- **Larger Buffers**: More frame buffering to handle colored frame size variations
- **Optimized Processing**: Reduces frame drops and visual stutter

**Note:** Colored frames are approximately 3x larger than monochrome frames due
*to the ANSI color codes. The system automatically adjusts the frame buffer size
*accordingly:
- 1-16MB frame buffer (plus 8MB network buffers)
