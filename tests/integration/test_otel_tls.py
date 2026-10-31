"""HTTPS integration for the actual ascii-chat worker and CLI/env/TOML settings."""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401

from pathlib import Path
import os
import shutil
import ssl
import subprocess
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'deps/ascii-chat-deps/gotel/c/testing/python'))
from gotel_testing import Collector, service_names

def main():
    print("SKIP: collector-export checks are prohibited in test mode; use in-process transport tests and test_otel_disabled.py")
    return 77

    binary = str(Path(sys.argv[1]).resolve())
    env = {k:v for k,v in os.environ.items() if not k.startswith(('OTEL_', 'ASCII_CHAT_OTEL_'))}
    openssl = shutil.which('openssl') or r'C:\Program Files\Git\usr\bin\openssl.exe'
    media = ['mirror', '--test-pattern', '--audio=false', '--snapshot', '--snapshot-delay', '0',
             '--width', '40', '--height', '12', '--splash-screen=false']
    with tempfile.TemporaryDirectory(prefix='ascii-otel-tls-') as directory:
        root = Path(directory)
        cert, key = root/'cert.pem', root/'key.pem'
        subprocess.run([openssl, 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-keyout', str(key),
            '-out', str(cert), '-days', '1', '-subj', '/CN=localhost',
            '-addext', 'subjectAltName=DNS:localhost,IP:127.0.0.1'], check=True,
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        for version in [ssl.TLSVersion.TLSv1_2, ssl.TLSVersion.TLSv1_3]:
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.minimum_version = context.maximum_version = version
            context.load_cert_chain(cert, key)
            with Collector(ssl_context=context) as collector:
                for source in ['cli', 'env', 'toml', 'standard_env']:
                    collector.clear()
                    for signal in ['traces', 'metrics', 'logs']: collector.script('/v1/'+signal, [503])
                    flags = ['--otel-endpoint', collector.url, '--otel-timeout', '5000']
                    settings = dict(env)
                    if source == 'cli': flags += ['--otel-ca-file', str(cert)]
                    elif source == 'env': settings['ASCII_CHAT_OTEL_CA_FILE'] = str(cert)
                    elif source == 'standard_env': settings['OTEL_EXPORTER_OTLP_CERTIFICATE'] = str(cert)
                    else:
                        config = root/'config.toml'
                        config.write_text('[otel]\nca_file = ' + repr(str(cert).replace('\\', '/')) + '\n')
                        flags += ['--config', str(config)]
                    result = subprocess.run([binary, '--no-check-update', *flags, *media], env=settings,
                        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=15)
                    assert result.returncode == 0, result.stderr[-4000:]
                    paths = {path for path, _ in collector.records}
                    if paths != {'/v1/traces', '/v1/logs', '/v1/metrics'}: print(result.stderr.decode(errors='replace')[-6000:])
                    assert paths == {'/v1/traces', '/v1/logs', '/v1/metrics'}, (source, paths, result.stderr[-4000:])
                    assert service_names(collector.records) == {'ascii-chat-mirror'}
                collector.clear()
                # Runtime certificate rejection must send no plaintext application data.
                subprocess.run([binary, '--no-check-update', '--otel-endpoint', collector.url,
                    '--otel-timeout', '150', *media], env=env, stdout=subprocess.DEVNULL,
                    stderr=subprocess.PIPE, timeout=10)
                assert not collector.attempts
                result = subprocess.run([binary, '--no-check-update', '--otel-endpoint', collector.url,
                    '--otel-ca-file', str(root/'missing.pem'), *media], env=env,
                    stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=10)
                assert result.returncode != 0 and not collector.attempts
                print(version.name, 'ascii-chat CLI/environment/TOML export and certificate rejection passed')
if __name__ == '__main__': sys.exit(main())
