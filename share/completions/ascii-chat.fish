# Fish completion script for ascii-chat
# Install to: /usr/share/fish/vendor_completions.d/ascii-chat.fish
# Or: ~/.config/fish/completions/ascii-chat.fish

# Disable file completion by default
complete -c ascii-chat -f

# Helper function to check if we're in a specific mode
function __fish_ascii_chat_using_mode
    set -l cmd (commandline -opc)
    for arg in $cmd
        if contains -- $arg server client
            echo $arg
            return 0
        end
    end
    return 1
end

function __fish_ascii_chat_mode_is
    set -l mode (__fish_ascii_chat_using_mode)
    test "$mode" = "$argv[1]"
end

function __fish_ascii_chat_no_mode
    not __fish_ascii_chat_using_mode > /dev/null
end

# Modes (only when no mode specified yet)
complete -c ascii-chat -n __fish_ascii_chat_no_mode -a server -d 'Run a video chat server'
complete -c ascii-chat -n __fish_ascii_chat_no_mode -a client -d 'Connect to a video chat server'

# Common options (available in all modes)
complete -c ascii-chat -s h -l help -d 'Print usage information and exit'
complete -c ascii-chat -s v -l version -d 'Print version information and exit'
complete -c ascii-chat -s a -l address -x -d 'Server address'
complete -c ascii-chat -s p -l port -x -d 'Server port number (default: 27224)'
complete -c ascii-chat -s q -l quiet -d 'Suppress console logging output'
complete -c ascii-chat -s L -l log-file -r -d 'Write log output to file'
complete -c ascii-chat -s P -l palette -x -a 'standard blocks digital minimal cool custom' -d 'ASCII character palette'
complete -c ascii-chat -s C -l palette-chars -x -d 'Custom palette characters'
complete -c ascii-chat -s E -l encrypt -d 'Enable packet encryption'
complete -c ascii-chat -s K -l key -r -d 'SSH key for authentication'
complete -c ascii-chat -s F -l keyfile -r -d 'Read encryption key from file'
complete -c ascii-chat -l password -x -d 'Password for connection encryption'
complete -c ascii-chat -l no-encrypt -d 'Explicitly disable encryption'
complete -c ascii-chat -l config -r -F -d 'Load configuration from TOML file'
complete -c ascii-chat -l config-create -d 'Create a default configuration file'

# Client-only options
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -s x -l width -x -d 'Set terminal width in characters'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -s y -l height -x -d 'Set terminal height in characters'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -s H -l host -x -d 'Hostname for DNS lookup'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -s c -l webcam-index -x -a '0 1 2 3' -d 'Webcam device index'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -s f -l webcam-flip -d 'Toggle horizontal flipping of webcam'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -s M -l render-mode -x -a 'foreground background half-block' -d 'Rendering mode'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -s A -l audio -d 'Enable audio capture and streaming'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -s s -l stretch -d 'Allow image stretching'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -s S -l snapshot -d 'Capture single frame and exit'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -s D -l snapshot-delay -x -a '1.0 2.0 3.0 5.0' -d 'Delay before snapshot (seconds)'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -l test-pattern -d 'Use test pattern instead of webcam'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -l fps -x -a '15 30 60 120' -d 'Desired frame rate'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -l color-mode -x -a 'auto mono 16 256 truecolor' -d 'Terminal color mode'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -l show-capabilities -d 'Print terminal capabilities and exit'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -l utf8 -d 'Force enable UTF-8 support'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -l audio-device -x -a '-1 0 1 2' -d 'Audio input device index'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -l server-key -r -d 'Expected server public key'

# Server-only options
complete -c ascii-chat -n '__fish_ascii_chat_mode_is server' -l address6 -x -d 'IPv6 bind address'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is server' -l client-keys -r -d 'Allowed client keys file'
