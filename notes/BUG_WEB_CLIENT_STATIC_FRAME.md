# Bug Report: Web Client Only Renders Single Static Frame, Not Animated

## Summary
The web browser client successfully connects to the server and receives ASCII frames, but only renders a single static frame. The animation does not update - subsequent frames are not being received or rendered.

## Observed Behavior
1. Browser client opens at `http://localhost:3000/client`
2. Client connects to WebSocket server
3. It appears one ASCII frame is rendered and displayed sometimes. User has seen it rendering several frames then stopping via
   safari. Basically it doesn't animate. Sometimes it dones't render any ascii to xterm.js at all if you refresh. It's
   intermittent or a race condition.
4. Frame never updates after rendering - same content stays on screen indefinitely. User swears it renders for a few
   frames before stopping, which he has seen in manual tests.. but then it stops.
5. Browser console shows no errors or warnings. Connection panel shows connection maintained.
6. Console logs show image frames being tranmitted but we haven't confirmed ascii frames are being sent back. We should
   check server logs.

## Expected Behavior
- Client should continuously send webcam video frames to server
- Server should continuously generate and send ASCII frames back
- xterm.js Terminal should show animated ASCII art updating in real-time
- Animation should persist for as long as connection is active

## Current vs. Working State

### Working (Playwright E2E Test):
```javascript
// Simulated canvas stream - generates frame every 33ms
setInterval(() => {
  ctx.fillStyle = `hsl(${(frameCount * 5) % 360}, 100%, 50%)`;
  ctx.fillRect(0, 0, 640, 480);
  ctx.fillText(`Frame: ${frameCount++}`, 20, 30);
}, 33);

// Result: 280+ frames received, 7 unique hashes, ANIMATES ✅
```

### Broken (Manual Browser Client):
- Webcam capture requested but may not be continuous
- Server receives one frame
- No additional frames received
- Result: Single static frame, NO ANIMATION ❌

## Root Cause (Hypothesis)
The browser client is **not continuously sending video frames** to the server. Possible causes:

1. **Webcam stream not captured continuously**
   - Client might capture once and stop
   - Missing `requestAnimationFrame` or interval loop for frame capture

2. **Connection drops after handshake**
   - Initial frame sent before connection stabilizes
   - Subsequent frames not sent/received due to connection error

3. **JavaScript error stopping frame transmission**
   - Console errors not visible or being ignored
   - Webcam permission denied
   - Stream closed unexpectedly

4. **Missing frame sending loop**
   - Browser client needs to continuously:
     1. Capture webcam frame
     2. Convert to RGB24 payload
     3. Send IMAGE_FRAME packet to server
     4. Repeat every ~33ms (30 FPS)

## How to Debug

### 1. Check Browser Console for Errors
```bash
# Open DevTools (F12) and look for:
- JavaScript errors
- WebSocket connection errors
- "Connection error" messages
- getUserMedia permission failures
```

### 2. Check Server Logs for Frame Reception
```bash
# Look for IMAGE_FRAME packets received
grep "IMAGE_FRAME\|has_video_sources" .server-debug-XXXXX.log

# Should see:
# ✅ Multiple IMAGE_FRAME packets with different timestamps
# ❌ Only one IMAGE_FRAME or none after initial connection
```

### 3. Compare Frame Sending Logic
- **Playwright test:** Uses canvas `setInterval(33ms)` to generate continuous frames
- **Browser client:** Should have similar loop in `Client.tsx` to capture and send frames
- Check if the loop exists and is being triggered

### 4. Verify WebSocket Connection State
```bash
# In browser console:
clientRef.current?.isConnected()  # Should return true continuously
```

### 5. Check Webcam Stream Status
- Browser console should show webcam stream initialization
- Look for `getUserMedia` success/failure
- Verify stream is still active (not stopped after one frame)

## Files to Investigate
- `web/web.ascii-chat.com/src/pages/Client.tsx` - Frame capture/sending loop
- `web/web.ascii-chat.com/src/network/ClientConnection.ts` - WebSocket state management
- `src/server/client.c` - Frame reception/generation loop (already verified working)

## Testing Steps to Reproduce
1. Start server: `./build/bin/ascii-chat server --port 27224 --websocket-port 27226`
2. Open browser: `http://localhost:3000/client`
3. Observe: Single static ASCII frame appears, never updates
4. Expected: Animated ASCII art continuously updating

## Next Steps
1. Enable verbose logging in browser client
2. Add frame counter to identify if frames are being sent
3. Check server logs to confirm it's receiving only one frame
4. Review `Client.tsx` for frame capture loop - likely missing or not triggered
5. Verify webcam stream doesn't close after first frame






BUG-REPORT-REPORT:
There is often garbage data after the single frame printed. here's an example of a frame :
OOOO000KOO0K0KKKKKKKK00OxxxxxkkkkkkkkkkkOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOO0O0000000000KX0KKKKK0OkkkkkkkkkkkkkkOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOK00000K000KKKN0KKKKK0kkkkkkkkkkkkkOOOOOOOOOOOOOO0000000000OOOOOOOOOOOOOO0K0000X0KKKKKN0KKKXX00kkkkkkkkkkOOOOOOOOOOO00000000000000000OOOOOOOOOOO00K0000X0KKKKKNXXXXX0KKkkkkkkkkOOOOOOOOOO00000000000000000000000OOOOOOO000KO000X0KKKKKXNXXKXoKKOkkkkkkkOOOOOOOOOOO0000000000000000000000OOOOOOO000K0000KKKKKKKXNXXXXoKKOkkkkkkkOOOOOOOOO000000000000000000000000O0OOOOO000KK000KX0KKKKKNKXXXXXXOkkkkkkkOOOOOOOOO00000000000000000000000O00OOOOO0000X000KX0KKKKXN0XXXXXK0kkkkOOOOOOOOOOOO0000000000000000000000OOOOOOOOO0000K0000X0KKKKKN0XXXKX0Okk0KKKOOOOOOOOOOOOO000000000000000000OOOOOOOOOO0000KO000X0KKKKKNKXXXKN0kkk0K0KOOOOOOOOOOOOOOOOOO0O00000000O0OOOOOOOOOOO0000K000KKXKKKKKXXXXKK0K0kkkkkkkkOOOOOOOOOOOOOOOOOO00000OOOOOOOOOOOOOOOOOkkxxK000KX000000XKKKKdKXkkkkkkkkkkOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOO00000000KKKKKKKKKKKKK:KKkkkkkkkkkkkkOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOO00000000000KKKKKKKKo:KOKXkkkkkkkkkkkkkkkOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOkkO000000000KKKKKKK0x':0000kxxxxxkkkkkkkkkkkkOOOkkkkO00OOOOkkkkkkkkkkkkkkkOO000000000KKKKKKK0KK0000kxxxxxxxxxxxxxkkkxk0000000000O0kkkkkkkkkkkkkkkkO0KKKKK0000KXXXXXXK00000Odxxxxo:::::c:lxxodxO00xkOOOkO000kkkkkkkkkkkkkkkOOOO0000000KO00000K0000OOoxkxlclkkxxxkxxxococoxkkOOkxd0000xxxxxxxxxxxxxx0OOOO00O0000O000000000OlkOxkxkxl.,..dxxxkkkkkkxxxxxkxxxxxodddddxxxxxxxxx0kOOOO00O000O000000OOOxkkkkkk..;;;;:;,.xxkkkxkOkkxxkxxxkkkkkkkkOOkkkkdxdOOOOOOO0OOOO0OOOOOOkkxkxkkk,.,;;;:cc;,;.;kkxkOOOkkOkkxxdxxkkxkkkOOOxOOxkkOkOOOOOOOOOOOOkkkdxddxxxo, .,...,:;.'''.oxkxOxkOOkxxxxxkkkkkOkk0xkOOdOxkOkkkkOOkOOOOkkxxxxkkxddxo. ',;;,.'',;'' ;ooddxxdxkxxdxxxdxxkxxxOOOxkOxxkkxkkkkOkkkkkxxxxddxxxxxooc;,,,..''..,,'.:loddoddxdddddxxxxxxkddOOkdkxkkxkkkkkkkOkkkkxxkxxxxkooddl: '''''''''''';:looddooooddxddxxxxkkkkkxkdkkkxxxkxxkkkkOodxkOkkkkxxdodoc;:'..   .....,::cccllooododddxxxxkkxxkkxkdkdkk;186;175;156mkdkk
see the garbage data at the end?
it crashes after this and goes back to connecting phase.
