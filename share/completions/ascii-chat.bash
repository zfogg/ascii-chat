# Bash completion script for ascii-chat
# Install to: /usr/share/bash-completion/completions/ascii-chat
# Or source this file in your ~/.bashrc

_ascii_chat() {
  local cur prev words cword
  _init_completion || return

  # All available options (common to both modes)
  local common_opts="
        -h --help
        -v --version
        -a --address
        -p --port
        -q --quiet
        -V --verbose
        -L --log-file
        -P --palette
        -C --palette-chars
        -E --encrypt
        -K --key
        -F --keyfile
        --password
        --no-encrypt
        --config
        --config-create
    "

  # Client-only options
  local client_opts="
        -x --width
        -y --height
        -H --host
        -c --webcam-index
        -f --webcam-flip
        -M --render-mode
        -A --audio
        -s --stretch
        -S --snapshot
        -D --snapshot-delay
        --mirror
        --test-pattern
        --list-webcams
        --list-microphones
        --list-speakers
        --fps
        --color-mode
        --show-capabilities
        --utf8
        --audio-device
        --server-key
    "

  # Server-only options
  local server_opts="
        --address6
        --client-keys
    "

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
  -L | --log-file | -K | --key | -F | --keyfile | --config | --client-keys | --server-key)
    _filedir
    return
    ;;
  # Options that take specific values
  -P | --palette)
    COMPREPLY=($(compgen -W "standard blocks digital minimal cool custom" -- "$cur"))
    return
    ;;
  --color-mode)
    COMPREPLY=($(compgen -W "auto mono 16 256 truecolor" -- "$cur"))
    return
    ;;
  -M | --render-mode)
    COMPREPLY=($(compgen -W "foreground background half-block" -- "$cur"))
    return
    ;;
  # Options that take numeric values - no completion
  -a | --address | -H | --host | --address6)
    # Could complete hostnames from /etc/hosts but skip for simplicity
    return
    ;;
  -p | --port | -x | --width | -y | --height | -c | --webcam-index | -D | --snapshot-delay | --fps | --audio-device)
    # Numeric values - no completion
    return
    ;;
  -C | --palette-chars | --password)
    # User-provided strings - no completion
    return
    ;;
  esac

  # If current word starts with -, complete options
  if [[ "$cur" == -* ]]; then
    local opts="$common_opts"
    case "$mode" in
    client)
      opts="$common_opts $client_opts"
      ;;
    server)
      opts="$common_opts $server_opts"
      ;;
    *)
      # No mode yet - show all options plus modes
      opts="$common_opts $client_opts $server_opts"
      ;;
    esac
    COMPREPLY=($(compgen -W "$opts" -- "$cur"))
    return
  fi

  # If no mode specified yet, suggest modes
  if [[ -z "$mode" ]]; then
    COMPREPLY=($(compgen -W "$modes" -- "$cur"))
    return
  fi
}

complete -F _ascii_chat ascii-chat
