ascii-chat 📸
==========

ASCII video chat.

Probably the first command line video chat program.

It just prints ASCII, so it works on your rxvt-unicode in OpenBox, a Putty SSH
session, and even iTerm or Kitty.app on macOS.

It even works in an initial UNIX login shell, i.e. the login shell that runs
'startx'.

Eventually it will support 3+ simultaneous people, 'google-hangouts' style. Audio streaming is now supported via PortAudio!

![Animated demonstration: monochrome](http://i.imgur.com/E4OuqvX.gif)

![Animated demonstration: color](https://i.imgur.com/IL8fSkA.gif)

Dependencies
==========
- Most people: `apt-get install build-essential clang pkg-config libv4l-dev zlib1g-dev portaudio19-dev libsodium-dev
libcriterion-dev`
- ArchLinux masterrace: `pacman -S clang pkg-config v4l-utils zlib portaudio libsodium libcriterion`
- macOS: `brew install pkg-config zlib portaudio libsodium criterion`

**Note:** OpenCV is no longer required! The project now uses native platform APIs:
- **Linux**: V4L2 (Video4Linux2)
- **macOS**: AVFoundation


Build and run
==========
- Clone this repo onto a computer with a webcam.
- Install the dependencies.
- Run `make`.
- Run `./bin/server -p 9001` in one terminal, and then
- Run `./bin/client -p 9001` in another.

Use `make -j debug` as you edit and test code (sometimes `make clean` too 😏).

Check the `Makefile` to see how it works.

If you need compile_commands.json for clang-based tools, check out `bear`:
```bash
brew install bear # or `apt-get install bear` or `yay -S bear`
make compile_commands.json
ls compile_commands.json

```

Testing
=========
1. Have the dependencies installed.
2. Run `make test` or `make test-unit` or `make test-integration`.

Notes:
* You can run 'em one-by-one: `make tests && ls bin/test_*`
* Useful: `bin/test_*` with `--verbose` and `--filter`, also `--help`.
* Testing framework docs: [libcriterion docs](https://criterion.readthedocs.io/en/master/)


Cryptography
=========
🔴⚠️ NOT YET IMPLEMENTED 🔴⚠️

Good news though: we have **libsodium** installed and some code written for it.

🔜 TODO: Implement crypto.


Command line flags
=========

## Client Options

Run `./bin/client -h` to see all client options:

- `-a --address ADDRESS`: IPv4 address to connect to (default: 0.0.0.0)
- `-p --port PORT`: TCP port (default: 27224)
- `-x --width WIDTH`: Render width (auto-detected by default)
- `-y --height HEIGHT`: Render height (auto-detected by default)
- `-c --webcam-index INDEX`: Webcam device index (default: 0)
- `-f --webcam-flip`: Horizontally flip webcam (default: enabled)
- `--color-mode MODE`: Color modes: auto, mono, 16, 256, truecolor (default: auto)
- `--show-capabilities`: Display terminal color capabilities and exit
- `--utf8`: Force enable UTF-8/Unicode support
- `-M --background-mode MODE`: Render colors for glyphs or cells: foreground, background (default: foreground)
- `-A --audio`: Enable audio capture and playback
- `-s --stretch`: Stretch video to fit without preserving aspect ratio
- `-q --quiet`: Disable console logging (logs only to file)
- `-S --snapshot`: Capture one frame and exit (useful for testing)
- `-D --snapshot-delay SECONDS`: Delay before snapshot in seconds (default: 3.0/5.0)
- `-L --log-file FILE`: Redirect logs to file
- `-E --encrypt`: Enable AES encryption
- `-K --key PASSWORD`: Encryption password
- `-F --keyfile FILE`: Read encryption key from file
- `-h --help`: Show help message

## Server Options

Run `./bin/server -h` to see all server options:

- `-a --address ADDRESS`: IPv4 address to bind to (default: 0.0.0.0)
- `-p --port PORT`: TCP port to listen on (default: 27224)
- `-A --audio`: Enable audio mixing and streaming
- `-L --log-file FILE`: Redirect logs to file
- `-E --encrypt`: Enable AES encryption
- `-K --key PASSWORD`: Encryption password
- `-F --keyfile FILE`: Read encryption key from file
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


### Audio Feature

You can now enable real-time audio streaming between server and client! Use the `--audio` or `-A` flag:

**Server with audio:**
```bash
./bin/server --audio
```

**Client with audio:**
```bash
./bin/client --audio
```

Audio is captured on the server and streamed to the client in real-time using PortAudio for cross-platform compatibility. The system works on both Linux (ALSA) and macOS (Core Audio).



TODO
==========
- [x] Audio.
- [x] Client should continuously attempt to reconnect
- [ ] switch Client "-a/--address" option to "host" and make it accept domains as well as ipv4
- [x] Colorize ASCII output
- [ ] Refactor image processing algorithms
- [x] client reconnect logic
- [x] terminal resize events
- [x] A nice protocol for the thing (packets and headers).
- [x] client requests a frame size
- [x] Client should gracefully handle `frame width > term width`
- [x] Client should gracefully handle `term resize` event
- [ ] Compile to WASM/WASI and run in the browser
- [x] Socket multiplexing.
- [ ] Edge detection and other things like that to make the image nicer.
- [x] Multiple clients.
- [ ] Snapshot mode for clients with --snap to "take a photo" and print it to the terminal rather than render video for a long time.
- [x] Audio mixing for multiple clients with compression and ducking.
- [ ] Color filters so you can pick a color for all the ascii so it can look like the matrix when you pick green (Gurpreet suggested).
- [ ] Lock-free packet send queues.
- [x] Hardware-accelerated ASCII-conversion via SIMD.


Notes
==========
**Note:** Colored frames are many times larger than monochrome frames due
to the ANSI color codes.

We don't really save bandwidth by sending color ascii video. I did the math with Claude Code.
