# PowerShell completion script for acds (ASCII-Chat Discovery Service)
#
# INSTALLATION:
#   1. Copy this file somewhere permanent, e.g.:
#      - Windows: $HOME\Documents\PowerShell\Completions\acds.ps1
#      - Unix:    ~/.config/powershell/Completions/acds.ps1
#
#   2. Add this line to your PowerShell profile ($PROFILE):
#      . "$HOME\Documents\PowerShell\Completions\acds.ps1"  # Windows
#      . "$HOME/.config/powershell/Completions/acds.ps1"    # Unix
#
#   3. Reload PowerShell or run: . $PROFILE

Register-ArgumentCompleter -Native -CommandName acds, acds.exe -ScriptBlock {
    param($wordToComplete, $commandAst, $cursorPosition)

    $options = @(
        @{ Name = '-h'; Description = 'print this help' }
        @{ Name = '--help'; Description = 'print this help' }
        @{ Name = '-v'; Description = 'print version information and exit' }
        @{ Name = '--version'; Description = 'print version information and exit' }
        @{ Name = '-p'; Description = 'TCP port (default: 27225)' }
        @{ Name = '--port'; Description = 'TCP port (default: 27225)' }
        @{ Name = '-d'; Description = 'SQLite database path' }
        @{ Name = '--db'; Description = 'SQLite database path' }
        @{ Name = '-K'; Description = 'SSH/GPG key for authentication' }
        @{ Name = '--key'; Description = 'SSH/GPG key for authentication' }
        @{ Name = '-L'; Description = 'redirect logs to file' }
        @{ Name = '--log-file'; Description = 'redirect logs to file' }
        @{ Name = '-l'; Description = 'set log level' }
        @{ Name = '--log-level'; Description = 'set log level' }
        @{ Name = '--upnp'; Description = 'enable UPnP/NAT-PMP for automatic router port mapping' }
    )

    $logLevelValues = @('dev', 'debug', 'info', 'warn', 'error', 'fatal')

    # Parse previous argument
    $tokens = $commandAst.CommandElements
    $previousArg = $null
    if ($tokens.Count -gt 1) {
        $previousArg = $tokens[-1].ToString()
    }

    # Handle value completions
    switch -Regex ($previousArg) {
        '^(-l|--log-level)$' {
            $logLevelValues | Where-Object { $_ -like "$wordToComplete*" } | ForEach-Object {
                [System.Management.Automation.CompletionResult]::new($_, $_, 'ParameterValue', "Log level: $_")
            }
            return
        }
        '^(-d|--db|-K|--key|-L|--log-file)$' {
            # File path completion - let PowerShell handle it
            return
        }
        '^(-p|--port)$' {
            # Port takes a value but no predefined completions
            return
        }
    }

    # Complete options
    if ($wordToComplete -like '-*' -or $wordToComplete -eq '') {
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
