# Face Follow

Face Follow automatically zooms and pans around one face before video is reduced
for transmission or converted to ASCII. It is off by default and works with
camera, video-file, and URL inputs in mirror, client, and discovery modes.

```sh
ascii-chat mirror --face-follow
ascii-chat client --face-follow
ascii-chat mirror --file interview.mp4 --face-follow
```

Enable **Face Follow** in the browser Settings panel, or open
`/mirror?faceFollow` (also supported on `/client` and `/discovery`). The setting is
stored as an individual URL parameter and survives reload. `faceFollow=false`
disables it. The cropped image is the image rendered and sent to peers, rather
than a cosmetic crop of the local preview.

## Tracking and fallback

The first subject is selected using face size and proximity to the center.
Subsequent detections associate with that subject by overlap and position, so a
larger bystander does not immediately take over. The subject is retained through
brief occlusions. After 700 ms without a matching detection, the image smoothly
widens to the complete source. After 1.5 seconds the old association expires and
another face can be selected. All changes, including a new subject or disabling
the setting, pass through the same smooth crop controller.

The crop preserves source aspect ratio, frames the face at about two thirds of
the view height, and limits zoom to about 2.86x. The composition stays fixed during
normal seated shifts: the face can move 18% of the view width or 14% of its height
from the last accepted position before a pan is considered. Movement must persist
for 850 ms; returning inside a smaller inner zone cancels it. Large movements near
the edge use a shorter 200 ms grace period. Reversing direction restarts the grace
period. Zoom has a separate roughly 20% size tolerance and 1.2-second grace period,
so noisy box sizes and breathing do not make the view creep or pump.
Pan responds faster than zoom; smoothing uses elapsed time rather than frame
counts. A face outside the camera's original field of view cannot be recovered.
Subject association uses geometry; similar faces crossing can still exchange
tracks. It does not identify people.

Detection samples the original field of view at most ten times per second,
using an image fitted within 320x240. The browser runs inference and the shared
C framing controller in an independent worker, using the newest available crop
without blocking rendering. Pending inference frames are bounded to one.
WebCodecs, direct camera-track capture, media uploads, and fallback video-element
capture all apply the crop before encoding/rendering. Detection stays local;
ordinary existing video packets carry the framed view.

## Developer outline

Debug native builds accept `--face-follow-debug`, which draws a red square around
the selected detected face. It can also be used alone to inspect detection
without enabling automatic zoom. Release native builds omit this CLI option.

Outline rendering remains available in Release builds, including the deployed
WASM modules. Only the native CLI option is debug-build-only. The website's
webcam demo offers **Face Follow (outlined face)** between **Face Follow** and
**Full Frame**; returning to either other mode removes the outline.

The browser equivalent is `?faceFollow&faceFollowDebug`, with no debug toggle in
the Settings panel. Debugging is opt-in and the square is included in the rendered
image. Debug capture canvases expose normalized crop/face coordinates and
detection counts through `data-face-follow` for the browser integration tests.
Without a current detection, the square disappears while framing follows its
normal hold/widen policy.

## Detector and builds

Native and browser use the same pinned BSD-3-Clause libfacedetection sources and
embedded model in `deps/libfacedetection`. No OpenCV, external model download, or
cloud inference is required. `lib/video/face_follow.c` implements the shared
controller; a narrow C++ adapter isolates the detector from C consumers.

The existing `mirror-web` and `client-web` modules both export the browser face
controller and detector API. The Face Follow worker loads `mirror.js` with no
prewarmed pthread pool, so detection stays off the render loop without requiring
a third WASM artifact. The normal `web/web/scripts/wasm-build.mjs` workflow
publishes only the verified mirror/client files. Its fingerprint includes the
vendored C++ sources and model weights as well as the existing C/WASM sources.

## Repeatable visual verification

Generate an 18-second fixture from OpenCV's moving-face/occlusion clip:

```sh
python tests/scripts/create_face_follow_fixture.py
cd web/web
vp exec playwright test tests/e2e/face-follow.spec.ts --project chromium --workers 1
```

The source is
[OpenCV's faceocc2.webm](https://github.com/opencv/opencv_extra/blob/5.x/testdata/cv/tracking/faceocc2/data/faceocc2.webm).
The generated fixture moves the source across a larger canvas, introduces a
second face from seconds 4–9, removes all faces from seconds 9–13, and restores
the original subject. Test media stays in the ignored build/evidence workspace;
the script downloads it on demand. `FACE_FOLLOW_VIDEO` can point to another copy
of the generated fixture.

Browser tests exercise actual detection, subject retention, smooth crop changes,
full-frame fallback, reacquisition, the settings toggle, and reload persistence.
The camera tests feed the clip through Chromium's real fake-camera device,
exercise both direct camera-track and video-element capture, and check the source
rectangle drawn before ASCII conversion. They save screenshots and timestamped
crop samples. A seated fixture adds small shifts, a brief lean, and a sustained
move, checking that the view holds before deliberately reframing. The eleven Criterion controller
tests in `tests/unit/video/face_follow_test.c` additionally exercise startup with
no face, normal webcam framing, transient loss, a larger bystander, frame-rate
independence, disabling, seated jitter, brief leans, pan grace, and zoom grace.
