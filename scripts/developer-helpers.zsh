#!/usr/bin/env zsh

# ascii-chat Development Aliases
#
# So you don't have to struggle to type long commands or use shell history as much.
# They're single word aliases or like a few simple letters, so you can type them
# super fast as you iterate.
# Pipe-able when it makes sense, so you can 2>&1 or | tee or | grep or | sed.
#
# How to use: `source ./scripts/developer-helpers.zsh && dtbash ls build_docker`


# Variables and Helpers
_repo_root=$(git rev-parse --show-toplevel)

source "$_repo_root/scripts/color.zsh"

# Alias and function helper API

# CMake
unalias c 2>/dev/null || true
c() {
  cmake "$@"
}

cpd() {
  cmake --preset default -B build "$@"
}
cpr() {
  cmake --preset release -B build_release "$@"
}

cb() {
  cmake --build "$@"
}

cbb() {
  cmake --build build "$@"
}

cbr() {
  cmake --build build_release "$@"
}

# Ninja
n() {
  ninja "$@"
}

# Make
m() {
  make "$@"
}

# t - Test - Run the test defined in $test with a convenient one-letter command
# Run it from anywhere in the repo - you can be in the build dir running ninjas 
# or the repo root running cmake --build build
function t() {
  _src="$_repo_root"
  _test="$test"
  if [ -z "$_test" ]; then
    echo "Error: define they \"test\" environment variable to use the 't' command"
    exit 1
  fi
  cd "$_src"/build && ninja bin/"$_test"
  cd .. && build/bin/"$_test"
}

# dtbash - Docker Tests Bash - Run a bash command in the tests docker container
# Mostly to quickly iterate in Linux from macOS or Windows. I like showing Claude Code this one.
# You don't have to quote the arguments to it.
function dtbash() {
  docker-compose -f "$_repo_root"/tests/docker-compose.yml \
    run --rm ascii-chat-tests \
    bash -c "$*"
}

# dt - Docker Tests Test - Build and run a single test "$test" in the Docker tests container
# Like `t` (check the description) but for Docker
function dt() {
  docker-compose -f "$_repo_root"/tests/docker-compose.yml run --rm ascii-chat-tests bash \
    -c '
  _dir=build_docker
  _test="$0"
  if [ -z "$_test" ]; then
    echo "Error: define the \"test\" environment variable to use the '"'"'dt'"'"' command"
    exit 1
  fi
  [ -f $_dir/build.ninja ] || cmake --preset default -B $_dir
  cd $_dir && ninja bin/$_test
  cd .. && $_dir/bin/$_test' \
      "$test"
}

