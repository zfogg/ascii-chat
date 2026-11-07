# Git hooks

Hook scripts in this directory can share code from `extra/`. The installer places
`extra/` beside the installed hooks, so hooks can find it relative to their own
location. Keep shared helper files inside that directory.

Install hooks with `scripts/git-hooks.sh`. Run `bash scripts/git-hooks.sh --help`
for installation options, including copying, symlinking, and replacing existing
hooks. The installer uses Git's configured hooks directory and installs `extra/`
with every selection of hooks.
