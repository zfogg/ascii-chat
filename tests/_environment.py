"""Shared policy for every Python test process and its native children.

TESTING is the existing C test-mode flag. No telemetry test may opt out or send
OTLP to a collector. In-process gotel transport fixtures remain available.
"""
import os

os.environ["TESTING"] = "1"
os.environ["ASCII_CHAT_OTEL_ENABLED_TELEMETRY"] = "none"
