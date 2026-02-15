#!/usr/bin/env zsh

set -e

cd "$(dirname "$0")/.."

t=v$(./scripts/version.sh --next)
git tag $t
git push origin $t
