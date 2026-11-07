"""Assertions for patterns expected in test output."""
import re


def require_match(pattern, text):
    match = re.search(pattern, text)
    assert match is not None, f"Pattern {pattern!r} not found in {text!r}"
    return match
