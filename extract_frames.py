import hashlib, json, pathlib, re, sys
directory = pathlib.Path(sys.argv[1])
manifest = json.loads((directory / 'manifest.json').read_text())
out = directory.with_name(directory.name + '-frames')
out.mkdir(exist_ok=True)
for case in manifest['cases']:
    data = (directory / (case['name'] + '.ansi')).read_bytes()
    homes = list(re.finditer(rb'\x1b\[(?:H|1;1H|0;0H)', data))
    if not homes:
        print('NO_HOME', case['name'], repr(data[:150]), repr(data[-150:]))
        raise SystemExit(1)
    # Snapshot streams end with terminal cleanup. Select a complete image near
    # the middle of the run, retaining its bytes exactly. This keeps the moving
    # square away from its wraparound position at the edge of the viewport.
    candidates = [(m.start(), homes[i+1].start() if i+1 < len(homes) else len(data))
                  for i,m in enumerate(homes)]
    complete = [(a,b) for a,b in candidates if data[a:b].count(b'\n') >= 12]
    selected = len(complete) // 2
    start,end = complete[selected]
    frame = data[start:end]
    (out / (case['name'] + '.ansi')).write_bytes(frame)
    case.update(raw_bytes=len(data), raw_sha256=hashlib.sha256(data).hexdigest(),
                frame_start=start, frame_end=end, selected_frame=selected, complete_frames=len(complete),
                frame_sha256=hashlib.sha256(frame).hexdigest())
    errors = (directory / (case['name'] + '.stderr')).read_bytes()
    (out / (case['name'] + '.stderr')).write_bytes(errors)
print('frames', len(manifest['cases']), 'tail', repr(frame[-120:]))
(out / 'manifest.json').write_text(json.dumps(manifest,indent=2))
