"""Source-grounded journeys. Each checkpoint opens an exact file or symbol."""
def P(file,symbol,title,text,*also):
 return dict(file=file,symbol=symbol,title=title,text=text,also=list(also))
def C(id,title,summary,*steps):
 return dict(id=id,title=title,summary=summary,steps=list(steps))
def G(id,title,group,scope,question,goal,outcome,diagnostic,chapters,related):
 return dict(id=id,title=title,group=group,scope=scope,question=question,goal=goal,subtitle=goal,outcome=outcome,diagnostic=diagnostic,chapters=chapters,related=related)
S='src/common/session/'
W='web/web/src/'
B='web/packages/shared/src/'
N=W+'network/'
L='lib/'
GUIDES=[
G('native','Native lifecycle','Architecture','CLI','What happens after I run ascii-chat?',
 'Trace mode dispatch, shared setup, rendering ownership, and ordered teardown.',
 'Distinguish mode callbacks from shared infrastructure and follow startup through to shutdown.',
 'Separate option validation, capture creation, connection setup, and the first displayed frame when startup stalls.',[
 C('dispatch','Choose a mode','One executable dispatches to one mode; it does not run every mode loop.',
 P('src/main.c',None,'Dispatch through the mode table','The binary registers server, client, mirror, discovery-service, and implicit discovery entry points. The selected entry point determines the rest of the lifecycle.'),
 P(L+'options/options.c','options_detect_mode','Resolve the invocation','Mode detection determines how positional arguments are interpreted. Configuration, environment values, and explicit flags are resolved before the mode consumes settings.'),
 P('src/mirror/main.c','mirror_main','Install a mode callback','Mirror delegates to the client-like lifecycle. Compare client_main and discovery_main: shared setup is retained while connection and run callbacks change.','src/client/main.c','src/discovery/main.c')),
 C('setup','Own the session','Shared contexts establish media and output ownership before the mode runs.',
 P(S+'client_like.c','session_client_like_run','Coordinate setup','The lifecycle coordinates capture, display, splash, audio, and session attempts. Mode-specific preparation and run callbacks supply the differences.'),
 P(S+'capture.c','session_capture_create','Select a frame source','Capture abstracts webcam, file, test pattern, and network-fed media while retaining source dimensions, timing, and ownership.',L+'media/source.c'),
 P(S+'display.c','session_display_create','Select an output target','Display owns terminal capabilities, dimensions, output policy, and optional render-file state.',L+'ui/controller.c')),
 C('stop','Run and release','Leaving a render loop is only one part of shutdown.',
 P(S+'render.c','session_render_loop','Choose the scheduler','Local capture can use the pipeline; callback-driven modes provide capture and sleep callbacks. Snapshot completion and exit checks belong here.',S+'pipeline.c'),
 P('src/client/main.c','shutdown_client','Interrupt workers','Protocol reception, capture, audio, and keepalive work must stop using resources before their contexts are released.','src/client/keepalive.c'),
 P(S+'client_like.c','client_like_cleanup_session','Release shared resources','Cleanup detaches capture/audio, completes render-file output, destroys display state, and restores terminal/logging behavior.'))],['hosting','capture','recording']),
G('mirror','Browser → ASCII','Architecture','Browser + shared C','How does a browser use the same ASCII engine as the CLI?',
 'Follow browser pixels through TypeScript memory adapters, C/WASM conversion, and terminal rasterization.',
 'Identify who captures pixels, who generates ASCII, and how ANSI becomes canvas pixels.',
 'A blank preview can originate in media acquisition, WASM initialization, zero grid dimensions, or presentation.',[
 C('prepare','Prepare the browser','React composes sources and waits for a usable WASM module and grid.',
 P(W+'pages/Mirror.tsx','MirrorPage','Compose the preview','The page supplies initialization, settings, media selection, and renderer refs instead of implementing another ASCII converter.'),
 P(W+'hooks/useClientLike.ts','useClientLike','Wait for readiness','WASM readiness and terminal dimensions gate settings and rendering. A ready camera does not imply valid C renderer dimensions.'),
 P(W+'hooks/useMirrorWebcam.ts','useMirrorWebcam','Acquire browser frames','The browser owns camera permissions and video/canvas elements. File playback is a separate source feeding the same render loop.',W+'hooks/useMirrorFile.ts')),
 C('convert','Cross into C','Browser scheduling calls exported C functions instead of entering a native blocking loop.',
 P(B+'hooks/useMirrorRenderLoop.ts','useMirrorRenderLoop','Schedule conversion','The loop checks dimensions/readiness and obtains frames at browser media cadence.',B+'hooks/videoFrameClock.ts'),
 P(B+'wasm/mirror.ts','convertFrameToAscii','Marshal memory','The wrapper moves frame bytes into WASM and reads returned ASCII. Allocation, string ownership, and release belong to this boundary.'),
 P('src/web/mirror.c','mirror_convert_frame','Use the C renderer','The exported entry point applies dimensions, options, filters, and animation before reusable C conversion.',L+'video/ascii/ascii.c',L+'video/rgba/color_filter.c')),
 C('present','Present the result','ANSI is an intermediate representation, not the final browser image.',
 P(B+'components/AsciiRenderer/useAsciiRendererHandle.ts','useAsciiRendererHandle','Feed shared UI state','The handle sends media, keys, and resize events through the C UI session before presenting tick output.',B+'wasm/sessionUi.ts'),
 P(B+'components/AsciiRenderer/useInitAsciiRenderer.ts','useInitAsciiRenderer','Create the terminal renderer','Font loading, renderer lifetime, resize handling, and canvas initialization belong to this hook.'),
 P(L+'media/render/terminal.c','term_renderer_feed','Rasterize terminal text','libvterm interprets ANSI; glyph rendering generates a pixel surface. This is distinct from the camera-to-ASCII conversion.'))],['capture','shared-grid','settings']),
G('client','Browser networking','Architecture','Browser','What does Connect actually start?',
 'Separate UI orchestration, session strategy, browser transport, and C protocol/crypto state.',
 'Trace direct server connections separately from joining or hosting a discovered session.',
 'An open signaling WebSocket is a different milestone from a protocol-ready peer media channel.',[
 C('strategy','Select a session','One client page supports different connection strategies.',
 P(W+'pages/Client.tsx','ClientPage','Collect intent','ClientPage composes media, security, connection controls, and rendering. DiscoveryPage simply enables discovery mode.',W+'pages/Discovery.tsx'),
 P(W+'hooks/useClientConnection.ts','useClientConnection','Choose the strategy','The hook creates ClientConnection for direct access, WebRTCSession for joining, or BrowserHostSession for creation. Reconnect policy follows this choice.'),
 P(N+'Transport.ts',None,'Use one contract','ClientSession supplies connect/disconnect, packet send, and state callbacks without requiring one transport implementation.',N+'ClientConnection.ts',N+'WebRTCSession.ts')),
 C('boundary','Cross the protocol boundary','Browser adapters own sockets and scheduling while reusable C code owns protocol and crypto operations.',
 P(N+'ClientConnection.ts','ClientConnection','Drive socket events','This class feeds browser transport events into C/WASM client state and must propagate transport failures rather than trust stale module state.',N+'SocketBridge.ts'),
 P(W+'wasm/client.ts',None,'Adapt exported APIs','The module exposes packet helpers, crypto operations, and client state to TypeScript. Socket bytes cross this interface.'),
 P('src/web/client.c','client_init_with_args','Initialize C state','Exports initialize options/state and handle handshakes, packet processing, encryption, and outgoing media.',L+'network/acip/client.c')),
 C('ready','Start media','Connected state is followed by capability and stream-control packets.',
 P(W+'hooks/useClientConnection.ts','useClientConnection','Advertise capabilities','CLIENT_CAPABILITIES and STREAM_START use actual renderer dimensions, settings, and codec support.',N+'packetBuilders.ts'),
 P(N+'WebRTCSession.ts','WebRTCSession','Wait for host readiness','The initial SERVER_STATE acknowledges native host registration; a DataChannel open event can precede installation of the host receive callback.'),
 P(W+'pages/Client.tsx','ClientPage','Manage media producers','Video and audio send through the selected session. Disconnect stops producers before closing a channel they might still use.'))],['direct','join','audio']),
G('ui','Shared UI runtime','Architecture','CLI + browser','How do help, splash, logs, and video share a screen?',
 'Trace native and browser input into shared C screen state and composed output.',
 'Distinguish host scheduling adapters from C behavior that chooses and draws screens.',
 'Follow a key through input, returned action, UI tick, terminal feed, and presentation when visible state fails to change.',[
 C('input','Route input','Native and browser hosts adapt events into shared C behavior.',
 P(S+'keyboard_handler.c','session_handle_keyboard_input','Handle native keys','Application actions and setting changes are routed with controller ownership of the current screen.',L+'ui/input.c'),
 P(B+'components/AsciiRenderer/useAsciiRendererHandle.ts','useAsciiRendererHandle','Handle browser keys','The handle forwards keyboard, dimensions, logs, and media to SessionUi and applies returned host actions.'),
 P(B+'wasm/sessionUi.ts','SessionUi','Own one UI instance','This wrapper allocates event strings and owns one C UI session, rather than owning the complete WASM module.')),
 C('state','Choose and draw','Shared nonblocking C state avoids independent React replicas of native screens.',
 P('src/web/common/ui.c','wasm_ui_tick','Expose host-driven ticks','WASM exports forward time, input, loading, media, and resize into the shared session.'),
 P(L+'ui/session.c','ui_session_tick','Advance screen state','Animation, dirty flags, help, loading, logs, and media determine the composed output.',L+'ui/session_help.c',L+'ui/session_splash.c'),
 P(L+'ui/frame_buffer.c',None,'Compose screen cells','Shared frame buffers hold terminal output. Native controller composition also uses these rendering facilities.',L+'ui/controller.c')),
 C('output','Deliver the frame','Native terminals and browser canvases diverge after the shared UI produces text.',
 P(S+'display.c',None,'Present natively','Display combines terminal capabilities, controller updates, and media output. Resizing changes upstream rendering dimensions.',L+'ui/terminal_screen.c'),
 P(B+'components/AsciiRenderer/useAsciiRendererHandle.ts','useAsciiRendererHandle','Present in a browser','C UI output feeds the terminal rasterizer, whose pixels are copied into the canvas.'),
 P(L+'media/render/terminal.c','term_renderer_pixels','Read the pixel surface','The terminal renderer exposes pixels, pitch, and dimensions. It is not a second ASCII conversion algorithm.'))],['settings','native','mirror']),
G('discovery','Discovery service','Architecture','CLI + browser + service','What does ACDS do for a call?',
 'Follow service startup, database membership, request dispatch, signaling, and expiration.',
 'Separate naming/membership from the peer media host and find the relay and persistence handlers.',
 'A valid session name is not proof of media connectivity. Check join results, host availability, signaling, and media transport separately.',[
 C('startup','Start ACDS','The service uses server-like lifecycle infrastructure with discovery-specific callbacks.',
 P('src/discovery-service/main.c',None,'Configure the mode','Identity, database settings, and callbacks are assembled before entering the shared server lifecycle.',S+'server_like.c'),
 P('src/discovery-service/server.c',None,'Own service state','Startup opens persistent session state and rate limiting. Maintenance handles expiry, old rate-limit records, and migrations.',L+'discovery/database.c'),
 P(L+'discovery/identity.c','acds_identity_load','Identify the endpoint','The signaling endpoint has its own loaded/generated identity, separate from participant camera streams.')),
 C('membership','Persist membership','Create, lookup, join, and leave produce database-backed protocol results.',
 P(L+'network/acip/acds_client.c','acds_protocol_enter','Run the shared protocol','Native and browser adapters enter the same C protocol state machine with creation/join intent and credentials.'),
 P(L+'network/acip/acds_handlers.c','acip_handle_acds_packet','Dispatch requests','ACIP dispatch validates packet structure and forwards discovery operations to service callbacks.',L+'network/acip/acds_server.c'),
 P(L+'discovery/database.c','database_session_join','Record the participant','The database resolves sessions, verifies join requirements, records members, and tracks the host. Expiration and leave alter this state.')),
 C('relay','Relay control traffic','ACDS carries negotiation and coordination; media has another transport and host.',
 P('src/discovery-service/signaling.c',None,'Route SDP and ICE','Participant-addressed signaling connects membership knowledge to peer negotiation.'),
 P('src/discovery/webrtc.c','discovery_send_sdp','Emit native signaling','Callbacks associate descriptions/candidates with session and recipient identifiers before sending through discovery transport.'),
 P(N+'WebRTCSession.ts','WebRTCSession','Keep media on the peer channel','Browser signaling stays on ACDS while media travels on the peer DataChannel. Their reconnection milestones differ.'))],['join','webrtc','recovery']),
]

def make_guides():
 from guides_audio import make_audio_guides
 from guides_calls import make_call_guides
 from guides_media import make_media_guides
 from guides_controls import make_control_guides
 args=(P,C,G,S,W,B,N,L)
 guides=GUIDES + make_call_guides(*args) + make_media_guides(*args) + make_control_guides(*args) + make_audio_guides(*args)
 # Include concrete adapters/helpers so cross-process checkpoints retain their local code context.
 context={
  ('discovery','membership',0):['src/web/browser_discovery.c'],
  ('discovery','membership',1):['src/discovery-service/server.c'],
  ('discovery','membership',2):['src/discovery-service/signaling.c'],
  ('discovery','relay',0):[L+'network/acip/send.c'],
  ('discovery','relay',2):[N+'DiscoveryProtocol.ts'],
  ('join','membership',0):['src/web/browser_discovery.c'],
  ('join','membership',1):[L+'network/acip/acds_client.c'],
  ('join','membership',2):['src/discovery-service/signaling.c'],
  ('join','media',0):[L+'network/acip/acds_client.c'],
  ('join','media',1):[N+'WebRTCBridge.ts'],
  ('join','media',2):[S+'av_state.c'],
  ('webrtc','reachability',1):['src/discovery/nat.c'],
  ('webrtc','signal',0):[L+'network/acip/send.c'],
  ('webrtc','signal',2):[N+'DiscoveryProtocol.ts'],
  ('security','trust',0):[L+'crypto/keys.c'],
  ('security','trust',1):[L+'network/http_client.c'],
  ('security','trust',2):[L+'crypto/handshake/common.c'],
  ('shared-grid','compose',0):[L+'video/rgba/image.c'],
  ('shared-grid','compose',2):[S+'host.c'],
  ('recovery','retry',0):['src/client/server.c'],
  ('recovery','retry',1):[N+'ClientConnection.ts'],
  ('recovery','retry',2):[N+'WebRTCBridge.ts'],
 }
 for guide in guides:
  for chapter in guide['chapters']:
   for index,step in enumerate(chapter['steps']):
    step['also']=list(dict.fromkeys(step['also']+context.get((guide['id'],chapter['id'],index),[])))
 return guides
