# Fish completion script for ascii-chat
# Install to: /usr/share/fish/vendor_completions.d/ascii-chat.fish
# Or: ~/.config/fish/completions/ascii-chat.fish

# Disable file completion by default
complete -c ascii-chat -f

# Helper function to check if we're in a specific mode
function __fish_ascii_chat_using_mode
    set -l cmd (commandline -opc)
    for arg in $cmd
        if contains -- $arg server client mirror
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
complete -c ascii-chat -n __fish_ascii_chat_no_mode -a mirror -d 'View webcam locally without network'

# Binary-level options (available in all modes and before mode selection)
complete -c ascii-chat -s h -l help -d 'print this help'
complete -c ascii-chat -s v -l version -d 'print version information and exit'
complete -c ascii-chat -s L -l log-file -r -d 'redirect logs to file'
complete -c ascii-chat -l log-level -x -a 'dev debug info warn error fatal' -d 'set log level'
complete -c ascii-chat -s V -l verbose -d 'increase log verbosity (stackable: -VV, -VVV)'
complete -c ascii-chat -s q -l quiet -d 'disable console logging (log only to file)'
complete -c ascii-chat -l config -r -d 'load configuration from TOML file'
complete -c ascii-chat -l config-create -d 'create default configuration file'

# Common mode-specific options (available in client and server modes)
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client; or __fish_ascii_chat_mode_is server' -s p -l port -x -d 'TCP port'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client; or __fish_ascii_chat_mode_is server' -s P -l palette -x -a 'standard blocks digital minimal cool custom' -d 'ASCII character palette'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client; or __fish_ascii_chat_mode_is server' -s C -l palette-chars -x -d 'Custom palette characters for --palette=custom'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client; or __fish_ascii_chat_mode_is server' -s E -l encrypt -d 'enable packet encryption'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client; or __fish_ascii_chat_mode_is server' -s K -l key -r -d 'SSH/GPG key file for authentication'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client; or __fish_ascii_chat_mode_is server' -s F -l keyfile -r -d 'read encryption key from file'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client; or __fish_ascii_chat_mode_is server' -l password -x -d 'password for connection encryption (prompts if not provided)'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client; or __fish_ascii_chat_mode_is server' -l no-encrypt -d 'disable encryption'

# Client-only options
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -s x -l width -x -d 'render width'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -s y -l height -x -d 'render height'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -s c -l webcam-index -x -a '0 1 2 3' -d 'webcam device index (0-based)'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -l list-webcams -d 'list available webcam devices and exit'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -l list-microphones -d 'list available audio input devices and exit'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -l list-speakers -d 'list available audio output devices and exit'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -s f -l webcam-flip -d 'toggle horizontal flip of webcam image'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -l test-pattern -d 'use test pattern instead of webcam (for testing multiple clients)'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -l fps -x -a '15 30 60 120' -d 'desired frame rate 1-144'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -l color-mode -x -a 'auto none 16 256 truecolor' -d 'color modes'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -l show-capabilities -d 'show detected terminal capabilities and exit'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -l utf8 -d 'force enable UTF-8/Unicode support'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -s M -l render-mode -x -a 'foreground background half-block' -d 'rendering mode'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -s A -l audio -d 'enable audio capture and playback'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -l microphone-index -x -d 'microphone device index (-1 for default)'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -l speakers-index -x -d 'speakers device index (-1 for default)'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -l audio-analysis -d 'enable audio analysis for debugging'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -l no-audio-playback -d 'disable speaker playback while keeping audio recording'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -s s -l stretch -d 'stretch or shrink video to fit (ignore aspect ratio)'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -s S -l snapshot -d 'capture single frame and exit'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -s D -l snapshot-delay -x -a '1.0 2.0 3.0 5.0' -d 'delay SECONDS before snapshot'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -l strip-ansi -d 'remove all ANSI escape codes from output'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -l server-key -r -d 'expected server public key for verification'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -l compression-level -x -a '1 2 3 4 5 6 7 8 9' -d 'zstd compression level 1-9'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -l no-compress -d 'disable video frame compression'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -l encode-audio -d 'force enable Opus audio encoding'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -l no-encode-audio -d 'disable Opus audio encoding'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is client' -l reconnect -x -a 'off auto' -d 'automatic reconnection behavior'

# Server-only options
complete -c ascii-chat -n '__fish_ascii_chat_mode_is server' -l max-clients -x -d 'maximum concurrent client connections'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is server' -l client-keys -r -d 'allowed client keys file for authentication'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is server' -l compression-level -x -a '1 2 3 4 5 6 7 8 9' -d 'zstd compression level 1-9'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is server' -l no-compress -d 'disable video frame compression'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is server' -l encode-audio -d 'force enable Opus audio encoding'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is server' -l no-encode-audio -d 'disable Opus audio encoding'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is server' -l no-audio-mixer -d 'disable audio mixer, send silence instead (debug only)'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is server' -l upnp -d 'enable UPnP/NAT-PMP for automatic router port mapping'

# Mirror-only options
complete -c ascii-chat -n '__fish_ascii_chat_mode_is mirror' -s x -l width -x -d 'render width'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is mirror' -s y -l height -x -d 'render height'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is mirror' -s c -l webcam-index -x -a '0 1 2 3' -d 'webcam device index (0-based)'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is mirror' -l list-webcams -d 'list available webcam devices and exit'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is mirror' -s f -l webcam-flip -d 'toggle horizontal flip of webcam image'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is mirror' -l test-pattern -d 'use test pattern instead of webcam'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is mirror' -l fps -x -a '15 30 60 120' -d 'desired frame rate 1-144'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is mirror' -l color-mode -x -a 'auto none 16 256 truecolor' -d 'color modes'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is mirror' -l show-capabilities -d 'show detected terminal capabilities and exit'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is mirror' -l utf8 -d 'force enable UTF-8/Unicode support'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is mirror' -s M -l render-mode -x -a 'foreground background half-block' -d 'rendering mode'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is mirror' -s s -l stretch -d 'stretch or shrink video to fit (ignore aspect ratio)'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is mirror' -s S -l snapshot -d 'capture single frame and exit'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is mirror' -s D -l snapshot-delay -x -a '1.0 2.0 3.0 5.0' -d 'delay SECONDS before snapshot'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is mirror' -l strip-ansi -d 'remove all ANSI escape codes from output'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is mirror' -s P -l palette -x -a 'standard blocks digital minimal cool custom' -d 'ASCII character palette'
complete -c ascii-chat -n '__fish_ascii_chat_mode_is mirror' -s C -l palette-chars -x -d 'Custom palette characters for --palette=custom'
