# Fish completion script for acds (ASCII-Chat Discovery Service)
# Install to: /usr/share/fish/vendor_completions.d/acds.fish
# Or: ~/.config/fish/completions/acds.fish

complete -c acds -f
complete -c acds -s h -l help -d 'print this help'
complete -c acds -s v -l version -d 'print version information and exit'
complete -c acds -s p -l port -x -d 'TCP port (default: 27225)'
complete -c acds -s d -l db -r -d 'SQLite database path'
complete -c acds -s K -l key -r -d 'SSH/GPG key for authentication'
complete -c acds -s L -l log-file -r -d 'redirect logs to file'
complete -c acds -s l -l log-level -x -a 'dev debug info warn error fatal' -d 'set log level'
complete -c acds -l upnp -d 'enable UPnP/NAT-PMP for automatic router port mapping'
