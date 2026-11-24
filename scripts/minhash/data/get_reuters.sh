#!/usr/bin/env bash
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
# Downloads Reuters-21578, Distribution 1.0, into this directory and extracts it
# to ./reuters21578 (22 reut2-NNN.sgm files). Git ignores the downloaded files.
#
# Source: the UCI Machine Learning Repository, over HTTPS. Its zip holds
# reuters21578.tar.gz. The script checks the SHA-256 of both files. The values
# were recorded on the first download (24 Nov 2026).
#
# The script does nothing if ./reuters21578 exists. If an archive is already in
# this directory, the script uses it with no download.

set -euo pipefail

cd "$(dirname "$0")"

UCI_URL="https://archive.ics.uci.edu/static/public/137/reuters+21578+text+categorization+collection.zip"
UCI_ZIP_SHA256="c2e001fbef39c59fcdcbc72659249f420b889ffc73e9d221c7c57e578479941f"
UCI_TAR_SHA256="3bae43c9b14e387f76a61b6d82bf98a4fb5d3ef99ef7e7075ff2ccbcf59f9d30"

target="reuters21578"
archive="reuters21578.tar.gz"
zip="reuters21578.zip"

sha256_of() {
  if command -v sha256sum > /dev/null; then
    sha256sum "$1" | cut -d' ' -f1
  else
    shasum -a 256 "$1" | cut -d' ' -f1
  fi
}

check_sha256() {  # file, expected
  local actual
  actual="$(sha256_of "$1")"
  if [ "$actual" != "$2" ]; then
    echo "error: SHA-256 of $1 is $actual, expected $2." >&2
    exit 1
  fi
}

if [ -d "$target" ]; then
  echo "Reuters-21578: $target/ exists, nothing to do."
  exit 0
fi

if [ ! -f "$archive" ]; then
  if [ ! -f "$zip" ]; then
    echo "Reuters-21578: downloading $UCI_URL"
    if ! curl -fsSL --retry 3 -o "$zip.part" "$UCI_URL"; then
      rm -f "$zip.part"
      echo "error: the download failed. Put $archive in $(pwd) by hand." >&2
      exit 1
    fi
    mv "$zip.part" "$zip"
  fi
  check_sha256 "$zip" "$UCI_ZIP_SHA256"
  unzip -o -q "$zip" "$archive"
fi
check_sha256 "$archive" "$UCI_TAR_SHA256"

# The archive has no top directory, so it is extracted into a new one.
echo "Reuters-21578: extracting $archive"
mkdir -p "$target.part"
tar -xzf "$archive" -C "$target.part"
if [ ! -f "$target.part/reut2-021.sgm" ]; then
  echo "error: $archive did not contain reut2-021.sgm." >&2
  exit 1
fi
mv "$target.part" "$target"
