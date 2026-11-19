#!/bin/sh
# Downloads SDL2's MinGW development package (headers, import library and
# SDL2.dll) for the Windows build, into ~/.cache/mzm. Run once, from Linux/WSL:
#
#   ./fetch_sdl2_mingw.sh && make OS_TARGET=windows
#
# The archive comes from SDL's official GitHub releases and is checked against
# the SHA-256 below before it is unpacked.
set -eu

VERSION="${SDL2_VERSION:-2.30.12}"
SHA256="${SDL2_SHA256:-}"
DEST="${HOME}/.cache/mzm"
NAME="SDL2-devel-${VERSION}-mingw.tar.gz"
URL="https://github.com/libsdl-org/SDL/releases/download/release-${VERSION}/${NAME}"

if [ -f "${DEST}/SDL2-${VERSION}/i686-w64-mingw32/bin/SDL2.dll" ]; then
    echo "SDL2 ${VERSION} (MinGW) is already in ${DEST}/SDL2-${VERSION}"
    exit 0
fi

mkdir -p "${DEST}"
TMP="$(mktemp -d)"
trap 'rm -rf "${TMP}"' EXIT

echo "Downloading ${URL}"
curl -fL --proto '=https' -o "${TMP}/${NAME}" "${URL}"

ACTUAL="$(sha256sum "${TMP}/${NAME}" | cut -d' ' -f1)"
if [ -z "${SHA256}" ]; then
    SHA256="$(sed -n "s/^${VERSION} //p" "$(dirname "$0")/sdl2_mingw.sha256")"
fi
if [ "${ACTUAL}" != "${SHA256}" ]; then
    echo "SHA-256 mismatch for ${NAME}:" >&2
    echo "  expected ${SHA256:-<none recorded>}" >&2
    echo "  got      ${ACTUAL}" >&2
    exit 1
fi

tar -xzf "${TMP}/${NAME}" -C "${DEST}"
echo "SDL2 ${VERSION} (MinGW) unpacked into ${DEST}/SDL2-${VERSION}"
