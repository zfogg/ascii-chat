ascii-chat
==========

ASCII video chat.

![Animated demonstration](http://i.imgur.com/E4OuqvX.gif)


Dependencies
==========
- opencv (sudo apt-get install libopencv-dev)
- libjpeg (sudo apt-get install libjpeg-dev)
- clang (sudo apt-get install clang)


TODO
==========
- Client should continuously attempt to reconnect
- Client program should accept URLs as well as IP addresses
- Colorize ASCII output
- Refactor image processing algorithms
- Client should gracefully handle `frame width > term width`
- Client should gracefully handle `term resize` event

