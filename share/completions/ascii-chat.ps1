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
        @{ Name = '-h'; Description = 'Print usage information and exit' }
        @{ Name = '--help'; Description = 'Print usage information and exit' }
        @{ Name = '-v'; Description = 'Print version information and exit' }
        @{ Name = '--version'; Description = 'Print version information and exit' }
        @{ Name = '-a'; Description = 'Server address' }
        @{ Name = '--address'; Description = 'Server address' }
        @{ Name = '-p'; Description = 'Server port number (default: 27224)' }
        @{ Name = '--port'; Description = 'Server port number (default: 27224)' }
        @{ Name = '-q'; Description = 'Suppress console logging output' }
        @{ Name = '--quiet'; Description = 'Suppress console logging output' }
        @{ Name = '-V'; Description = 'Increase verbosity (stackable)' }
        @{ Name = '--verbose'; Description = 'Increase verbosity (stackable)' }
        @{ Name = '-L'; Description = 'Write log output to file' }
        @{ Name = '--log-file'; Description = 'Write log output to file' }
        @{ Name = '-P'; Description = 'ASCII character palette' }
        @{ Name = '--palette'; Description = 'ASCII character palette' }
        @{ Name = '-C'; Description = 'Custom palette characters' }
        @{ Name = '--palette-chars'; Description = 'Custom palette characters' }
        @{ Name = '-E'; Description = 'Enable packet encryption' }
        @{ Name = '--encrypt'; Description = 'Enable packet encryption' }
        @{ Name = '-K'; Description = 'SSH key for authentication' }
        @{ Name = '--key'; Description = 'SSH key for authentication' }
        @{ Name = '-F'; Description = 'Read encryption key from file' }
        @{ Name = '--keyfile'; Description = 'Read encryption key from file' }
        @{ Name = '--password'; Description = 'Password for connection encryption' }
        @{ Name = '--no-encrypt'; Description = 'Explicitly disable encryption' }
        @{ Name = '--config'; Description = 'Load configuration from TOML file' }
        @{ Name = '--config-create'; Description = 'Create a default configuration file' }
    )

    # Client-only options
    $clientOptions = @(
        @{ Name = '-x'; Description = 'Set terminal width in characters' }
        @{ Name = '--width'; Description = 'Set terminal width in characters' }
        @{ Name = '-y'; Description = 'Set terminal height in characters' }
        @{ Name = '--height'; Description = 'Set terminal height in characters' }
        @{ Name = '-H'; Description = 'Hostname for DNS lookup' }
        @{ Name = '--host'; Description = 'Hostname for DNS lookup' }
        @{ Name = '-c'; Description = 'Webcam device index' }
        @{ Name = '--webcam-index'; Description = 'Webcam device index' }
        @{ Name = '-f'; Description = 'Toggle horizontal flipping of webcam' }
        @{ Name = '--webcam-flip'; Description = 'Toggle horizontal flipping of webcam' }
        @{ Name = '-M'; Description = 'Rendering mode' }
        @{ Name = '--render-mode'; Description = 'Rendering mode' }
        @{ Name = '-A'; Description = 'Enable audio capture and streaming' }
        @{ Name = '--audio'; Description = 'Enable audio capture and streaming' }
        @{ Name = '-s'; Description = 'Allow image stretching' }
        @{ Name = '--stretch'; Description = 'Allow image stretching' }
        @{ Name = '-S'; Description = 'Capture single frame and exit' }
        @{ Name = '--snapshot'; Description = 'Capture single frame and exit' }
        @{ Name = '-D'; Description = 'Delay before snapshot (seconds)' }
        @{ Name = '--snapshot-delay'; Description = 'Delay before snapshot (seconds)' }
        @{ Name = '--test-pattern'; Description = 'Use test pattern instead of webcam' }
        @{ Name = '--list-webcams'; Description = 'List available webcam devices and exit' }
        @{ Name = '--list-microphones'; Description = 'List available audio input devices and exit' }
        @{ Name = '--list-speakers'; Description = 'List available audio output devices and exit' }
        @{ Name = '--fps'; Description = 'Desired frame rate' }
        @{ Name = '--color-mode'; Description = 'Terminal color mode' }
        @{ Name = '--show-capabilities'; Description = 'Print terminal capabilities and exit' }
        @{ Name = '--utf8'; Description = 'Force enable UTF-8 support' }
        @{ Name = '--audio-device'; Description = 'Audio input device index' }
        @{ Name = '--server-key'; Description = 'Expected server public key' }
    )

    # Server-only options
    $serverOptions = @(
        @{ Name = '--address6'; Description = 'IPv6 bind address' }
        @{ Name = '--client-keys'; Description = 'Allowed client keys file' }
    )

    # Palette values
    $paletteValues = @('standard', 'blocks', 'digital', 'minimal', 'cool', 'custom')

    # Color mode values
    $colorModeValues = @('auto', 'mono', '16', '256', 'truecolor')

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
        '^(-M|--render-mode)$' {
            $renderModeValues | Where-Object { $_ -like "$wordToComplete*" } | ForEach-Object {
                [System.Management.Automation.CompletionResult]::new($_, $_, 'ParameterValue', "Render mode: $_")
            }
            return
        }
        '^(-L|--log-file|-K|--key|-F|--keyfile|--config|--client-keys|--server-key)$' {
            # File path completion - let PowerShell handle it
            return
        }
        '^(-a|--address|-H|--host|--address6|-p|--port|-x|--width|-y|--height|-c|--webcam-index|-D|--snapshot-delay|--fps|--audio-device|-C|--palette-chars|--password)$' {
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
