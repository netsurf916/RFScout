#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
MODE=${1:-all}
IDF_IMAGE=${RFSCOUT_IDF_IMAGE:-espressif/idf:release-v6.1}

run_fast() {
    cd "$ROOT"
    echo "[*] Checking C formatting"
    python3 tools/check_format.py --all
    echo "[*] Running release packager tests"
    python3 -m unittest discover -s tests -p 'test_*.py'
}

run_build() {
    command -v docker >/dev/null 2>&1 || {
        echo "docker is required for the ESP-IDF build" >&2
        exit 1
    }

    cd "$ROOT"
    echo "[*] Building ESP32-S3 firmware with $IDF_IMAGE"
    docker run --rm \
        -v "$ROOT:/project" \
        -w /project \
        "$IDF_IMAGE" \
        bash -lc '. "$IDF_PATH/export.sh" && idf.py --version && idf.py -DRFSCOUT_FORMAT_CHECK=OFF set-target esp32s3 && idf.py -DRFSCOUT_FORMAT_CHECK=OFF build'
}

case "$MODE" in
    fast)
        run_fast
        ;;
    build)
        run_build
        ;;
    all)
        run_fast
        run_build
        ;;
    *)
        echo "usage: $0 [fast|build|all]" >&2
        exit 2
        ;;
esac
