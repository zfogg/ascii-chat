# Bash completion script for ascii-chat
# Install to: /usr/share/bash-completion/completions/ascii-chat
# Or source this file in your ~/.bashrc

_ascii_chat_complete_with_help() {
  local cur="$1"
  local -a options help_texts

  # Parse options with help text from arrays
  local -a opts=("$@")
  local i
  for ((i = 1; i < ${#opts[@]}; i += 2)); do
    if [[ "${opts[i]}" == "$cur"* ]]; then
      options+=("${opts[i]}")
      help_texts+=("${opts[i+1]}")
    fi
  done

  # If we have matches, format with help text
  if [[ ${#options[@]} -gt 0 ]]; then
    # Check if completion supports descriptions (bash 4.4+)
    if compopt &>/dev/null && compopt -o description 2>/dev/null; then
      COMPREPLY=()
      for i in "${!options[@]}"; do
        COMPREPLY+=("${options[i]}$'\v'${help_texts[i]}")
      done
      compopt -o nosort
    else
      # Fallback for older bash versions
      COMPREPLY=($(compgen -W "${options[*]}" -- "$cur"))
    fi
  fi
}

_ascii_chat() {
  local cur prev words cword
  _init_completion || return

  # Binary-level options (available before mode selection)
  local -a binary_opts=(
    '-h' 'print this help'
    '--help' 'print this help'
    '-v' 'print version information and exit'
    '--version' 'print version information and exit'
    '-L' 'redirect logs to file'
    '--log-file' 'redirect logs to file'
    '--log-level' 'set log level: dev, debug, info, warn, error, fatal'
    '-V' 'increase log verbosity (stackable: -VV, -VVV)'
    '--verbose' 'increase log verbosity (stackable: -VV, -VVV)'
    '-q' 'disable console logging (log only to file)'
    '--quiet' 'disable console logging (log only to file)'
    '--config' 'load configuration from TOML file'
    '--config-create' 'create default configuration file'
  )

  # Common mode-specific options (available in all modes)
  local -a common_opts=(
    '-p' 'TCP port'
    '--port' 'TCP port'
    '-P' 'ASCII character palette'
    '--palette' 'ASCII character palette'
    '-C' 'Custom palette characters'
    '--palette-chars' 'Custom palette characters'
    '-E' 'enable packet encryption'
    '--encrypt' 'enable packet encryption'
    '-K' 'SSH/GPG key for authentication'
    '--key' 'SSH/GPG key for authentication'
    '-F' 'read encryption key from file'
    '--keyfile' 'read encryption key from file'
    '--password' 'password for connection encryption'
    '--no-encrypt' 'disable encryption'
  )

  # Client-only options with help text
  local -a client_opts=(
    '-x' 'render width (characters)'
    '--width' 'render width (characters)'
    '-y' 'render height (characters)'
    '--height' 'render height (characters)'
    '-c' 'webcam device index (0-based)'
    '--webcam-index' 'webcam device index (0-based)'
    '--list-webcams' 'list available webcam devices and exit'
    '--list-microphones' 'list available audio input devices and exit'
    '--list-speakers' 'list available audio output devices and exit'
    '-f' 'toggle horizontal flip of webcam image'
    '--webcam-flip' 'toggle horizontal flip of webcam image'
    '--test-pattern' 'use test pattern instead of webcam'
    '--fps' 'desired frame rate 1-144 (default: 60)'
    '--color-mode' 'color modes: auto, none, 16, 256, truecolor'
    '--show-capabilities' 'show detected terminal capabilities and exit'
    '--utf8' 'force enable UTF-8/Unicode support'
    '-M' 'rendering mode: foreground/fg, background/bg, half-block'
    '--render-mode' 'rendering mode: foreground/fg, background/bg, half-block'
    '-A' 'enable audio capture and playback'
    '--audio' 'enable audio capture and playback'
    '--microphone-index' 'microphone device index (-1 for default)'
    '--speakers-index' 'speakers device index (-1 for default)'
    '--audio-analysis' 'enable audio analysis for debugging'
    '--no-audio-playback' 'disable speaker playback while keeping audio recording'
    '-s' 'stretch or shrink video to fit (ignore aspect ratio)'
    '--stretch' 'stretch or shrink video to fit (ignore aspect ratio)'
    '-S' 'capture single frame and exit'
    '--snapshot' 'capture single frame and exit'
    '-D' 'delay SECONDS before snapshot'
    '--snapshot-delay' 'delay SECONDS before snapshot (default: 3.0-4.0)'
    '--mirror' 'view webcam locally without connecting to server'
    '--strip-ansi' 'remove all ANSI escape codes from output'
    '--server-key' 'expected server public key for verification'
    '--compression-level' 'zstd compression level 1-9 (default: 1)'
    '--no-compress' 'disable video frame compression'
    '--encode-audio' 'force enable Opus audio encoding'
    '--no-encode-audio' 'disable Opus audio encoding'
    '--reconnect' 'automatic reconnection: off, auto, or number 1-999'
  )

  # Server-only options with help text
  local -a server_opts=(
    '--max-clients' 'maximum concurrent client connections (default: 10, max: 32)'
    '--client-keys' 'allowed client keys file for authentication (authorized_keys format)'
    '--compression-level' 'zstd compression level 1-9 (default: 1)'
    '--no-compress' 'disable video frame compression'
    '--encode-audio' 'force enable Opus audio encoding'
    '--no-encode-audio' 'disable Opus audio encoding'
    '--no-audio-mixer' 'disable audio mixer, send silence instead (debug only)'
  )

  # Mirror-only options with help text
  local -a mirror_opts=(
    '-x' 'render width (characters)'
    '--width' 'render width (characters)'
    '-y' 'render height (characters)'
    '--height' 'render height (characters)'
    '-c' 'webcam device index (0-based)'
    '--webcam-index' 'webcam device index (0-based)'
    '--list-webcams' 'list available webcam devices and exit'
    '-f' 'toggle horizontal flip of webcam image'
    '--webcam-flip' 'toggle horizontal flip of webcam image'
    '--test-pattern' 'use test pattern instead of webcam'
    '--fps' 'desired frame rate 1-144 (default: 60)'
    '--color-mode' 'color modes: auto, none, 16, 256, truecolor'
    '--show-capabilities' 'show detected terminal capabilities and exit'
    '--utf8' 'force enable UTF-8/Unicode support'
    '-M' 'rendering mode: foreground/fg, background/bg, half-block'
    '--render-mode' 'rendering mode: foreground/fg, background/bg, half-block'
    '-s' 'stretch or shrink video to fit (ignore aspect ratio)'
    '--stretch' 'stretch or shrink video to fit (ignore aspect ratio)'
    '-S' 'capture single frame and exit'
    '--snapshot' 'capture single frame and exit'
    '-D' 'delay SECONDS before snapshot'
    '--snapshot-delay' 'delay SECONDS before snapshot (default: 3.0-4.0)'
    '--strip-ansi' 'remove all ANSI escape codes from output'
  )

  # Modes
  local modes="server client mirror"

  # Detect which mode we're in by scanning previous words
  local mode=""
  local i
  for ((i = 1; i < cword; i++)); do
    case "${words[i]}" in
    server | client | mirror)
      mode="${words[i]}"
      break
      ;;
    esac
  done

  case "$prev" in
  # Options that take file paths
  -L | --log-file | -K | --key | -F | --keyfile | --client-keys | --server-key | --config)
    _filedir
    return
    ;;
  # Options that take specific values
  -P | --palette)
    COMPREPLY=($(compgen -W "standard blocks digital minimal cool custom" -- "$cur"))
    return
    ;;
  --color-mode)
    COMPREPLY=($(compgen -W "auto none 16 256 truecolor" -- "$cur"))
    return
    ;;
  --log-level)
    COMPREPLY=($(compgen -W "dev debug info warn error fatal" -- "$cur"))
    return
    ;;
  -M | --render-mode)
    COMPREPLY=($(compgen -W "foreground fg background bg half-block" -- "$cur"))
    return
    ;;
  --reconnect)
    COMPREPLY=($(compgen -W "off auto" -- "$cur"))
    return
    ;;
  --compression-level)
    COMPREPLY=($(compgen -W "1 2 3 4 5 6 7 8 9" -- "$cur"))
    return
    ;;
  # Options that take numeric values - no completion
  -p | --port | -x | --width | -y | --height | -c | --webcam-index | -D | --snapshot-delay | --fps | --microphone-index | --speakers-index | --max-clients)
    return
    ;;
  -C | --palette-chars | --password)
    return
    ;;
  esac

  # If current word starts with -, complete options
  if [[ "$cur" == -* ]]; then
    local -a opts_to_complete

    case "$mode" in
    client)
      opts_to_complete=("${binary_opts[@]}" "${common_opts[@]}" "${client_opts[@]}")
      ;;
    server)
      opts_to_complete=("${binary_opts[@]}" "${common_opts[@]}" "${server_opts[@]}")
      ;;
    mirror)
      opts_to_complete=("${binary_opts[@]}" "${mirror_opts[@]}")
      ;;
    *)
      # No mode yet - show binary options and modes
      opts_to_complete=("${binary_opts[@]}")
      ;;
    esac

    # Generate completions with help text
    local -a completions
    for ((i = 0; i < ${#opts_to_complete[@]}; i += 2)); do
      if [[ "${opts_to_complete[i]}" == "$cur"* ]]; then
        completions+=("${opts_to_complete[i]}")
      fi
    done

    # Format output - in newer bash with description support
    if [[ ${#completions[@]} -gt 0 ]]; then
      if compopt &>/dev/null 2>&1; then
        # Try to enable description format if available
        compopt -o nosort 2>/dev/null || true
        COMPREPLY=()
        for opt in "${completions[@]}"; do
          # Find help text for this option
          for ((i = 0; i < ${#opts_to_complete[@]}; i += 2)); do
            if [[ "${opts_to_complete[i]}" == "$opt" ]]; then
              COMPREPLY+=("$opt	${opts_to_complete[i+1]}")
              break
            fi
          done
        done
      else
        COMPREPLY=($(compgen -W "${completions[*]}" -- "$cur"))
      fi
    fi
    return
  fi

  # If no mode specified yet, suggest modes
  if [[ -z "$mode" ]]; then
    COMPREPLY=($(compgen -W "$modes" -- "$cur"))
    return
  fi
}

complete -F _ascii_chat ascii-chat
