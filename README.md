ascii-chat
==========

ASCII video chat.

Probably the first command line video chat progam.

It just prints ASCII, so it works on your rxvt-unicode in OpenBox, a Putty SSH session, and even iTerm on OSX.  
It even works in an initial UNIX login shell, i.e. the login shell that runs 'startx'.

Eventually it will support 3+ simultaneous people, 'google-hangouts' style, and sound via PulseAudio or something.

![Animated demonstration](http://i.imgur.com/E4OuqvX.gif)


Dependencies
==========
- Most people: apt-get install clang libopencv-dev libjpeg-dev
- ArchLinux masterrace: pacman -S clang opencv libjpeg-turbo


Build and run
==========
- Clone this repo onto a computer with a webcam.
- Install the dependencies.
- run 'make clean all'.
- run './bin/server' in one terminal, and then
- run './bin/client -p 9001 -a 127.0.0.1' in another. 


TODO
==========
- Client should continuously attempt to reconnect
- Client program should accept URL arguments, as well as IP addresses like it does now
- Colorize ASCII output
- Refactor image processing algorithms
- Client should gracefully handle `frame width > term width`
- Client should gracefully handle `term resize` event

