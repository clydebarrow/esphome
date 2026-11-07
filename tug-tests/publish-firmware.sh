#!/usr/bin/env bash
# Builds tug firmware and publishes it to the flight-upload Worker, which offers it to devices of the same name from
# Settings > Check for update. A build that is already the newest for its name is left as it is.
#
#   tug-tests/publish-firmware.sh                 builds and publishes tug-d1001, tug-jc1060 and tug-wave7
#   tug-tests/publish-firmware.sh tug-d1001       just that one
#
# The token comes from Admin > Tug firmware > Upload token. Keep it in ~/.config/tug-firmware/token (mode 600), or
# in TUG_FIRMWARE_TOKEN. TUG_FIRMWARE_URL changes the Worker, for example to a local `wrangler dev`.
set -euo pipefail

cd "$(dirname "$0")/.."
WORKER="${TUG_FIRMWARE_URL:-https://flight-upload.clyde-beb.workers.dev}"
TOKEN_FILE="$HOME/.config/tug-firmware/token"
if [[ -z "${TUG_FIRMWARE_TOKEN:-}" ]]; then
  if [[ ! -r "$TOKEN_FILE" ]]; then
    echo "No upload token: create one in Admin > Tug firmware, and save it in $TOKEN_FILE" >&2
    exit 1
  fi
  TUG_FIRMWARE_TOKEN="$(tr -d '[:space:]' <"$TOKEN_FILE")"
fi

if [[ $# -eq 0 ]]; then
  set -- tug-d1001 tug-jc1060 tug-wave7
fi

# shellcheck disable=SC1091
source venv/bin/activate
export PYTHONPATH="$PWD"

for name in "$@"; do
  config="tug-tests/${name%.yaml}.yaml"
  name="$(basename "${config%.yaml}")"
  echo "== $name: building"
  python -m esphome compile "$config" >"/tmp/publish-$name.log" 2>&1 || {
    tail -30 "/tmp/publish-$name.log" >&2
    echo "$name: the build failed; the full log is /tmp/publish-$name.log" >&2
    exit 1
  }
  # The build directory takes the device's name, which these configs share with their file name
  image="tug-tests/.esphome/build/$name/build/firmware.ota.bin"
  if [[ ! -f "$image" ]]; then
    echo "$name: no $image after the build" >&2
    exit 1
  fi
  echo "== $name: publishing $(du -h "$image" | cut -f1 | tr -d ' ')"
  answer="$(curl --silent --show-error --fail-with-body \
    -H "Authorization: Bearer $TUG_FIRMWARE_TOKEN" \
    -H "Content-Type: application/octet-stream" \
    --data-binary "@$image" "$WORKER/api/firmware")" || {
    echo "$name: the upload failed: $answer" >&2
    exit 1
  }
  case "$answer" in
    *'"stored":true'*) echo "$name: published, offered to devices from now" ;;
    *'"stored":false'*) echo "$name: unchanged, this build is already the newest" ;;
    *) echo "$name: unexpected answer: $answer" >&2; exit 1 ;;
  esac
done
