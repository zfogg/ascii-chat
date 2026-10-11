"""Exercise two native clients through a local server and retain received WAVs."""

import argparse
import json
import os
import re
from pathlib import Path
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--speech", required=True)
    parser.add_argument("--port", type=int, default=54389)
    parser.add_argument("--speaker", type=int, default=27)
    parser.add_argument("--encrypted", action="store_true")
    args = parser.parse_args()
    import sounddevice as sd
    import soundfile as sf
    import numpy as np

    binary = str(Path(args.binary).resolve())
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    processes, logs = [], []

    def launch(name, flags):
        directory = output / name
        directory.mkdir(exist_ok=True)
        env = os.environ.copy()
        env.update(TEMP=str(directory), TMP=str(directory), ASCIICHAT_DUMP_AUDIO="1")
        if args.encrypted:
            # Only these loopback test processes skip ephemeral host identity checks.
            env["ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK"] = "1"
        log = open(directory / "console.log", "w", encoding="utf-8")
        logs.append(log)
        command = [
            binary,
            "--log-level",
            "info" if name == "server" else "warn",
            *flags,
        ]
        process = subprocess.Popen(
            command,
            cwd=directory,
            env=env,
            stdout=log,
            stderr=log,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        processes.append(process)
        return process

    try:
        security = [] if args.encrypted else ["--no-encrypt=true"]
        server = launch(
            "server",
            [
                "server",
                "127.0.0.1",
                "--port",
                str(args.port),
                "--status-screen=false",
                "--audio-spectral=true",
                *security,
            ],
        )
        time.sleep(2)
        common = [
            "client",
            f"127.0.0.1:{args.port}",
            "--test-pattern",
            "0",
            "--audio=true",
            "--audio-spectral=true",
            *security,
            "--speakers-volume",
            "0",
            "--snapshot",
            "--snapshot-delay",
            "32",
            "--width",
            "40",
            "--height",
            "12",
            "--splash-screen=false",
            "--video-codec",
            "raw",
        ]
        sender = launch("sender", common + ["--audio-capture-source", "mic"])
        time.sleep(6)
        receiver = launch("receiver", common + ["--audio-capture-source", "remote"])
        time.sleep(5)
        speech, rate = sf.read(args.speech, dtype="float32")
        sd.play(speech, rate, device=args.speaker)
        sd.wait()
        exits = [sender.wait(timeout=45), receiver.wait(timeout=45)]
        result = {"client_exits": exits, "server_alive": server.poll() is None}
        assert exits == [0, 0] and result["server_alive"], result
        server_log = (output / "server" / "console.log").read_text(
            encoding="utf-8", errors="replace"
        )
        capabilities = re.findall(
            r"INITIAL_CAPABILITIES:.*payload_len=(\d+)", server_log
        )
        assert capabilities == ["168", "168"], capabilities
        assert "Protocol violation" not in server_log
        received, rate = sf.read(output / "receiver" / "audio_playback_received.wav")
        excluded, _ = sf.read(output / "sender" / "audio_playback_received.wav")
        assert rate == 48000 and len(received) >= 25 * rate
        assert np.isfinite(received).all() and np.sqrt(np.mean(received**2)) > 0.001
        assert len(excluded) >= 25 * rate and np.max(np.abs(excluded)) < 1e-6
        result.update(
            capabilities_bytes=[int(x) for x in capabilities],
            received_seconds=len(received) / rate,
            received_rms=float(np.sqrt(np.mean(received**2))),
            received_peak=float(np.max(np.abs(received))),
            self_excluded_peak=float(np.max(np.abs(excluded))),
            decoded_packets_logged=len(re.findall("SERVER OPUS DECODE", server_log)),
            transport="TCP loopback, "
            + ("encrypted" if args.encrypted else "unencrypted"),
            spectral_capture=True,
            spectral_mixer=True,
        )
        (output / "result.json").write_text(json.dumps(result, indent=2))
        print(json.dumps(result, indent=2))
    finally:
        for process in reversed(processes):
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=10)
        for log in logs:
            log.close()


if __name__ == "__main__":
    main()
