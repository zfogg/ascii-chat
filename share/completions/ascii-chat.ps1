# PowerShell completion script for ascii-chat
#
# INSTALLATION:
#   1. Copy this file somewhere permanent, e.g.:
#      - Windows: $HOME\Documents\PowerShell\Completions\ascii-chat.ps1
#      - Unix:    ~/.config/powershell/Completions/ascii-chat.ps1
#
#   2. Add this line to your PowerShell profile ($PROFILE):
#      . "$HOME\Documents\PowerShell\Completions\ascii-chat.ps1"  # Windows
#      . "$HOME/.config/powershell/Completions/ascii-chat.ps1"    # Unix
#
#   3. Reload PowerShell or run: . $PROFILE
#
# Or install system-wide via CMake: cmake --install build
# Then add to $PROFILE: . "C:\Program Files\ascii-chat\doc\completions\ascii-chat.ps1"
#
# QUICK TEST (without permanent install):
#   . .\share\completions\ascii-chat.ps1
#   ascii-chat <Tab>   # Should show 'server' and 'client'

# Register argument completer for ascii-chat
Register-ArgumentCompleter -Native -CommandName ascii-chat, ascii-chat.exe -ScriptBlock {
    param($wordToComplete, $commandAst, $cursorPosition)

    # Modes
    $modes = @(
        @{ Name = 'server'; Description = 'Run a video chat server' }
        @{ Name = 'client'; Description = 'Connect to a video chat server' }
    )

    # Common options (available in both modes)
    $commonOptions = @(
        @{ Name = '-h'; Description = 'print this help' }
        @{ Name = '--help'; Description = 'print this help' }
        @{ Name = '-v'; Description = 'print version information and exit' }
        @{ Name = '--version'; Description = 'print version information and exit' }
        @{ Name = '-a'; Description = 'IPv4 address to bind/connect to' }
        @{ Name = '--address'; Description = 'IPv4 address to bind/connect to' }
        @{ Name = '-p'; Description = 'TCP port' }
        @{ Name = '--port'; Description = 'TCP port' }
        @{ Name = '-P'; Description = 'ASCII character palette' }
        @{ Name = '--palette'; Description = 'ASCII character palette' }
        @{ Name = '-C'; Description = 'Custom palette characters' }
        @{ Name = '--palette-chars'; Description = 'Custom palette characters' }
        @{ Name = '-L'; Description = 'redirect logs to file' }
        @{ Name = '--log-file'; Description = 'redirect logs to file' }
        @{ Name = '--log-level'; Description = 'set log level: dev, debug, info, warn, error, fatal' }
        @{ Name = '-V'; Description = 'increase log verbosity (stackable: -VV, -VVV)' }
        @{ Name = '--verbose'; Description = 'increase log verbosity (stackable: -VV, -VVV)' }
        @{ Name = '--compression-level'; Description = 'zstd compression level 1-9' }
        @{ Name = '--no-compress'; Description = 'disable video frame compression' }
        @{ Name = '--config'; Description = 'load configuration from TOML file' }
        @{ Name = '--config-create'; Description = 'create default configuration file' }
        @{ Name = '-E'; Description = 'enable packet encryption' }
        @{ Name = '--encrypt'; Description = 'enable packet encryption' }
        @{ Name = '-K'; Description = 'SSH/GPG key for authentication' }
        @{ Name = '--key'; Description = 'SSH/GPG key for authentication' }
        @{ Name = '-F'; Description = 'read encryption key from file' }
        @{ Name = '--keyfile'; Description = 'read encryption key from file' }
        @{ Name = '--password'; Description = 'password for connection encryption (prompts if not provided)' }
        @{ Name = '--no-encrypt'; Description = 'disable encryption' }
    )

    # Client-only options
    $clientOptions = @(
        @{ Name = '-x'; Description = 'render width' }
        @{ Name = '--width'; Description = 'render width' }
        @{ Name = '-y'; Description = 'render height' }
        @{ Name = '--height'; Description = 'render height' }
        @{ Name = '-H'; Description = 'hostname for DNS lookup' }
        @{ Name = '--host'; Description = 'hostname for DNS lookup' }
        @{ Name = '-c'; Description = 'webcam device index (0-based)' }
        @{ Name = '--webcam-index'; Description = 'webcam device index (0-based)' }
        @{ Name = '--list-webcams'; Description = 'list available webcam devices and exit' }
        @{ Name = '--list-microphones'; Description = 'list available audio input devices and exit' }
        @{ Name = '--list-speakers'; Description = 'list available audio output devices and exit' }
        @{ Name = '-f'; Description = 'toggle horizontal flip of webcam image' }
        @{ Name = '--webcam-flip'; Description = 'toggle horizontal flip of webcam image' }
        @{ Name = '--test-pattern'; Description = 'use test pattern instead of webcam (for testing multiple clients)' }
        @{ Name = '--fps'; Description = 'desired frame rate 1-144' }
        @{ Name = '--color-mode'; Description = 'color modes: auto, none, 16, 256, truecolor' }
        @{ Name = '--show-capabilities'; Description = 'show detected terminal capabilities and exit' }
        @{ Name = '--utf8'; Description = 'force enable UTF-8/Unicode support' }
        @{ Name = '-M'; Description = 'rendering mode: foreground, background, half-block' }
        @{ Name = '--render-mode'; Description = 'rendering mode: foreground, background, half-block' }
        @{ Name = '-A'; Description = 'enable audio capture and playback' }
        @{ Name = '--audio'; Description = 'enable audio capture and playback' }
        @{ Name = '--microphone-index'; Description = 'microphone device index (-1 for default)' }
        @{ Name = '--speakers-index'; Description = 'speakers device index (-1 for default)' }
        @{ Name = '--audio-analysis'; Description = 'enable audio analysis for debugging' }
        @{ Name = '--no-audio-playback'; Description = 'disable speaker playback while keeping audio recording' }
        @{ Name = '--encode-audio'; Description = 'force enable Opus audio encoding' }
        @{ Name = '--no-encode-audio'; Description = 'disable Opus audio encoding' }
        @{ Name = '-s'; Description = 'stretch or shrink video to fit (ignore aspect ratio)' }
        @{ Name = '--stretch'; Description = 'stretch or shrink video to fit (ignore aspect ratio)' }
        @{ Name = '-q'; Description = 'disable console logging (log only to file)' }
        @{ Name = '--quiet'; Description = 'disable console logging (log only to file)' }
        @{ Name = '-S'; Description = 'capture single frame and exit' }
        @{ Name = '--snapshot'; Description = 'capture single frame and exit' }
        @{ Name = '-D'; Description = 'delay SECONDS before snapshot' }
        @{ Name = '--snapshot-delay'; Description = 'delay SECONDS before snapshot' }
        @{ Name = '--mirror'; Description = 'view webcam locally without connecting to server' }
        @{ Name = '--strip-ansi'; Description = 'remove all ANSI escape codes from output' }
        @{ Name = '--server-key'; Description = 'expected server public key for verification' }
        @{ Name = '--reconnect'; Description = 'automatic reconnection behavior (off or auto)' }
    )

    # Server-only options
    $serverOptions = @(
        @{ Name = '--address6'; Description = 'IPv6 address to bind to' }
        @{ Name = '--max-clients'; Description = 'maximum concurrent client connections' }
        @{ Name = '--client-keys'; Description = 'allowed client keys file for authentication' }
        @{ Name = '--encode-audio'; Description = 'force enable Opus audio encoding' }
        @{ Name = '--no-encode-audio'; Description = 'disable Opus audio encoding' }
        @{ Name = '--no-audio-mixer'; Description = 'disable audio mixer, send silence instead (debug only)' }
    )

    # Palette values
    $paletteValues = @('standard', 'blocks', 'digital', 'minimal', 'cool', 'custom')

    # Color mode values
    $colorModeValues = @('auto', 'none', '16', '256', 'truecolor')

    # Log level values
    $logLevelValues = @('dev', 'debug', 'info', 'warn', 'error', 'fatal')

    # Render mode values
    $renderModeValues = @('foreground', 'background', 'half-block')

    # Parse current command line to determine context
    $tokens = $commandAst.CommandElements
    $mode = $null
    $previousArg = $null

    for ($i = 1; $i -lt $tokens.Count; $i++) {
        $token = $tokens[$i].ToString()
        if ($token -eq 'server' -or $token -eq 'client') {
            $mode = $token
        }
        if ($i -eq $tokens.Count - 1 -or $tokens[$i].Extent.EndOffset -lt $cursorPosition) {
            $previousArg = $token
        }
    }

    # Handle value completions for specific options
    switch -Regex ($previousArg) {
        '^(-P|--palette)$' {
            $paletteValues | Where-Object { $_ -like "$wordToComplete*" } | ForEach-Object {
                [System.Management.Automation.CompletionResult]::new($_, $_, 'ParameterValue', "Palette: $_")
            }
            return
        }
        '^(--color-mode)$' {
            $colorModeValues | Where-Object { $_ -like "$wordToComplete*" } | ForEach-Object {
                [System.Management.Automation.CompletionResult]::new($_, $_, 'ParameterValue', "Color mode: $_")
            }
            return
        }
        '^(--log-level)$' {
            $logLevelValues | Where-Object { $_ -like "$wordToComplete*" } | ForEach-Object {
                [System.Management.Automation.CompletionResult]::new($_, $_, 'ParameterValue', "Log level: $_")
            }
            return
        }
        '^(-M|--render-mode)$' {
            $renderModeValues | Where-Object { $_ -like "$wordToComplete*" } | ForEach-Object {
                [System.Management.Automation.CompletionResult]::new($_, $_, 'ParameterValue', "Render mode: $_")
            }
            return
        }
        '^(-L|--log-file|-K|--key|-F|--keyfile|--client-keys|--server-key|--config)$' {
            # File path completion - let PowerShell handle it
            return
        }
        '^(-a|--address|-H|--host|--address6|-p|--port|-x|--width|-y|--height|-c|--webcam-index|-D|--snapshot-delay|--fps|-C|--palette-chars|--password|--microphone-index|--speakers-index|--compression-level|--max-clients|--reconnect)$' {
            # These take values but don't have predefined completions
            return
        }
    }

    # Determine which options to show
    $options = @()

    if (-not $mode) {
        # No mode specified yet - show modes first
        if ($wordToComplete -notlike '-*') {
            $modes | Where-Object { $_.Name -like "$wordToComplete*" } | ForEach-Object {
                [System.Management.Automation.CompletionResult]::new(
                    $_.Name,
                    $_.Name,
                    'ParameterValue',
                    $_.Description
                )
            }
        }
        # Also show all options if typing an option
        $options = $commonOptions + $clientOptions + $serverOptions
    }
    elseif ($mode -eq 'client') {
        $options = $commonOptions + $clientOptions
    }
    elseif ($mode -eq 'server') {
        $options = $commonOptions + $serverOptions
    }

    # Complete options
    if ($wordToComplete -like '-*' -or ($mode -and $wordToComplete -eq '')) {
        $options | Where-Object { $_.Name -like "$wordToComplete*" } | ForEach-Object {
            [System.Management.Automation.CompletionResult]::new(
                $_.Name,
                $_.Name,
                'ParameterName',
                $_.Description
            )
        }
    }
}
