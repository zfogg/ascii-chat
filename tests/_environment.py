"""Shared policy for every Python test process and its native children.

TESTING is the existing C test-mode flag. No telemetry test may opt out or send
OTLP to a collector. In-process gotel transport fixtures remain available.
"""
import os
from pathlib import Path
import tempfile

os.environ["TESTING"] = "1"
os.environ["ASCII_CHAT_OTEL_ENABLED_TELEMETRY"] = "none"

# Interactive integration tests opt in explicitly when exercising onboarding.
os.environ["ASCII_CHAT_DISABLE_FIRST_USE"] = "1"

# CTest wrappers and standalone drivers import this module before launching
# native children, so they share the same disposable native-state policy as
# pytest even when no pytest fixture is involved.
_test_home = Path(tempfile.mkdtemp(prefix="ascii-chat-python-test-"))
for _name in ("HOME", "USERPROFILE", "APPDATA", "LOCALAPPDATA",
              "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME"):
    os.environ[_name] = str(_test_home)
os.environ["ASCII_CHAT_TEST_HOME"] = str(_test_home)
os.environ["ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK"] = "1"
