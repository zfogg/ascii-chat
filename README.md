ascii-chat 📸
==========

ASCII video chat.

Probably the first command line video chat program.

It just prints ASCII, so it works on your rxvt-unicode in OpenBox, a Putty SSH session, and even iTerm or Kitty.app on macOS.  
It even works in an initial UNIX login shell, i.e. the login shell that runs 'startx'.

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
- run `./bin/client -p 90001 -a 127.0.0.1` in another.

NOTE: run `./bin/server -h` to see options


TODO
==========
- Client should continuously attempt to reconnect
- Client program should accept URL arguments, as well as IP addresses like it does now
- Colorize ASCII output
- Refactor image processing algorithms
- Client should gracefully handle `frame width > term width`
- Client should gracefully handle `term resize` event
- Rewrite entire thing in Rust!
- Compile to WASM/WASI.
