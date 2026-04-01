#!/usr/bin/env zsh

set -e

cd "$(dirname "$0")/.."

set -x

which coolify >/dev/null || { echo "Please install coolify-cli first"; exit 1; }

app_uuid=$(coolify app list --format json | jq '.[] | select(.name=="zfogg/ascii-chat/discovery-service").uuid' -r)
version_json=$(coolify app env get "$app_uuid" VERSION --show-sensitive --format json)
version_uuid=$(echo "$version_json" | jq '.uuid' -r)

coolify_app_version=$(echo "$version_json" | jq '.value' -r)
echo "Coolify version set right now: $coolify_app_version"

current_app_version=$(./scripts/version.sh)
echo "Current repo version on this machine: $current_app_version"

if [ "$current_app_version" = "$coolify_app_version" ]; then
  echo "versions are the same, nothing to do"
  exit 0
fi

coolify_token=$(jq -r '.instances[] | select(.default==true).token' ~/.config/coolify/config.json)
coolify_url=$(jq -r '.instances[] | select(.default==true).fqdn' ~/.config/coolify/config.json)
api="$coolify_url/api/v1/applications/$app_uuid/envs"

curl -sf -X DELETE "$api/$version_uuid" -H "Authorization: Bearer $coolify_token"
echo
curl -sf -X POST "$api" -H "Authorization: Bearer $coolify_token" -H "Content-Type: application/json" -d "{\"key\":\"VERSION\",\"value\":\"$current_app_version\",\"is_buildtime\":true,\"is_runtime\":true}"
echo

echo 'success:'
set -x
coolify app env get "$app_uuid" VERSION --show-sensitive --format json | jq -r '.value'
set +x

