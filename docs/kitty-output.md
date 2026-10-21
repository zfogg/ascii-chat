# Snapshot images in Kitty terminals

Use the existing native terminal renderer (libvterm and FreeType) to display a
snapshot through the [Kitty graphics protocol](https://sw.kovidgoyal.net/kitty/graphics-protocol/):

```sh
ascii-chat mirror --file photo.jpg --snapshot --render-file=--kitty
ascii-chat mirror --file photo.jpg --snapshot --render-kitty
```

Both commands emit one image after the snapshot completes, without creating an
image file. To save the normal rendered image and display the same pixels:

```sh
ascii-chat mirror --file photo.jpg --snapshot --render-file snapshot.png --render-kitty
```

Both Kitty forms require `--snapshot`. The output uses direct RGBA transmission,
with base64 payload chunks no larger than 4096 bytes. It does not send filenames,
read terminal responses from stdin, or require temporary image files. Redirected
stdout contains only the protocol, so it can be captured and replayed in a capable
terminal. Known plain terminals (`TERM=dumb`, `linux`, or `vt100`) reject direct
display; other terminals must implement Kitty graphics. The explicit flag selects
the protocol without probing stdin, including when stdin carries media.

For binary media output, use `--render-file=-`. It writes a fragmented MP4 stream
and keeps logs and ASCII presentation off stdout. It can be combined with
`--file=-` for media input on stdin:

```sh
cat input.mp4 | ascii-chat mirror --file=- --snapshot --render-file=- > rendered.mp4
```

Kitty protocol and binary media cannot share stdout in one invocation. Write a
named file when also requesting `--render-kitty`. Output failures, including a
closed consumer, fail the command rather than reporting a successful export.
