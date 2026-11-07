"""Command construction for native ascii-chat test processes."""


def cli_command(binary, *args):
    """Build an invocation with update checks disabled for reproducible tests."""
    return [str(binary), "--no-check-update", *args]
import os


def clean_environment():
    """Copy the inherited environment without ascii-chat option overrides."""
    return {key: value for key, value in os.environ.items() if not key.startswith("ASCII_CHAT_")}


def run_cli(binary, *args, **subprocess_options):
    """Run the native binary with subprocess.run options."""
    import subprocess
    return subprocess.run(cli_command(binary, *args), **subprocess_options)


def start_cli(binary, *args, **subprocess_options):
    """Start the native binary with subprocess.Popen options."""
    import subprocess
    return subprocess.Popen(cli_command(binary, *args), **subprocess_options)
