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

  # Common options with help text
  local -a common_opts=(
    '-h' 'print this help'
    '--help' 'print this help'
    '-v' 'print version information and exit'
    '--version' 'print version information and exit'
    '-a' 'IPv4 address to bind/connect to'
    '--address' 'IPv4 address to bind/connect to'
    '-p' 'TCP port'
    '--port' 'TCP port'
    '-P' 'ASCII character palette'
    '--palette' 'ASCII character palette'
    '-C' 'Custom palette characters'
    '--palette-chars' 'Custom palette characters'
    '-L' 'redirect logs to file'
    '--log-file' 'redirect logs to file'
    '--log-level' 'set log level: dev, debug, info, warn, error, fatal'
    '-V' 'increase log verbosity (stackable: -VV, -VVV)'
    '--verbose' 'increase log verbosity (stackable: -VV, -VVV)'
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
    '-x' 'render width'
    '--width' 'render width'
    '-y' 'render height'
    '--height' 'render height'
    '-H' 'hostname for DNS lookup'
    '--host' 'hostname for DNS lookup'
    '-c' 'webcam device index (0-based)'
    '--webcam-index' 'webcam device index (0-based)'
    '--list-webcams' 'list available webcam devices and exit'
    '--list-microphones' 'list available audio input devices and exit'
    '--list-speakers' 'list available audio output devices and exit'
    '-f' 'toggle horizontal flip of webcam image'
    '--webcam-flip' 'toggle horizontal flip of webcam image'
    '--test-pattern' 'use test pattern instead of webcam'
    '--fps' 'desired frame rate 1-144'
    '--color-mode' 'color modes: auto, none, 16, 256, truecolor'
    '--show-capabilities' 'show detected terminal capabilities and exit'
    '--utf8' 'force enable UTF-8/Unicode support'
    '-M' 'rendering mode: foreground, background, half-block'
    '--render-mode' 'rendering mode: foreground, background, half-block'
    '-A' 'enable audio capture and playback'
    '--audio' 'enable audio capture and playback'
    '-s' 'stretch or shrink video to fit (ignore aspect ratio)'
    '--stretch' 'stretch or shrink video to fit (ignore aspect ratio)'
    '-q' 'disable console logging (log only to file)'
    '--quiet' 'disable console logging (log only to file)'
    '-S' 'capture single frame and exit'
    '--snapshot' 'capture single frame and exit'
    '-D' 'delay SECONDS before snapshot'
    '--snapshot-delay' 'delay SECONDS before snapshot'
    '--mirror' 'view webcam locally without connecting to server'
    '--strip-ansi' 'remove all ANSI escape codes from output'
    '--server-key' 'expected server public key for verification'
  )

  # Server-only options with help text
  local -a server_opts=(
    '--address6' 'IPv6 address to bind to'
    '--client-keys' 'allowed client keys file for authentication'
  )

  # Modes
  local modes="server client"

  # Detect which mode we're in by scanning previous words
  local mode=""
  local i
  for ((i = 1; i < cword; i++)); do
    case "${words[i]}" in
    server | client)
      mode="${words[i]}"
      break
      ;;
    esac
  done

  case "$prev" in
  # Options that take file paths
  -L | --log-file | -K | --key | -F | --keyfile | --client-keys | --server-key)
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
    COMPREPLY=($(compgen -W "foreground background half-block" -- "$cur"))
    return
    ;;
  # Options that take numeric values - no completion
  -a | --address | -H | --host | --address6)
    return
    ;;
  -p | --port | -x | --width | -y | --height | -c | --webcam-index | -D | --snapshot-delay | --fps)
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
      opts_to_complete=("${common_opts[@]}" "${client_opts[@]}")
      ;;
    server)
      opts_to_complete=("${common_opts[@]}" "${server_opts[@]}")
      ;;
    *)
      # No mode yet - show all options
      opts_to_complete=("${common_opts[@]}" "${client_opts[@]}" "${server_opts[@]}")
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
