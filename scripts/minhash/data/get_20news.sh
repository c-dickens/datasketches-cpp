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
# Downloads the 20 Newsgroups data (the original 19,997-post version) into this
# directory and extracts it to ./20_newsgroups. This version keeps the
# cross-posted duplicates, which the LSH demo needs. Git ignores the downloaded
# files.
#
# Source: the UCI Machine Learning Repository, over HTTPS. Its zip holds
# 20_newsgroups.tar.gz. The script checks the SHA-256 of both files.
#
# The script does nothing if ./20_newsgroups exists. If an archive is already in
# this directory, the script uses it with no download.
#
# NEWS20_URL overrides the source, with no checksum check. A URL that ends in
# .tar.gz is used as the archive itself, for example
#   NEWS20_URL=http://qwone.com/~jason/20Newsgroups/20news-19997.tar.gz

set -euo pipefail

cd "$(dirname "$0")"

UCI_URL="https://archive.ics.uci.edu/static/public/113/twenty+newsgroups.zip"
UCI_ZIP_SHA256="cfbb360d6c1e55c06d33a4c5da0789a93b78db74833a70be8ff2e133cc4e6a6e"
UCI_TAR_SHA256="ab7bbf82b7831f7dbb1a09d9312f66fa78565c8de25526999b0d66f69d37e414"

url="${NEWS20_URL:-$UCI_URL}"
target="20_newsgroups"
archive="20_newsgroups.tar.gz"
zip="twenty_newsgroups.zip"

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

download() {  # url, file
  echo "20 Newsgroups: downloading $1"
  if ! curl -fsSL --retry 3 -o "$2.part" "$1"; then
    rm -f "$2.part"
    echo "error: the download failed. Put $archive in $(pwd) by hand, or set NEWS20_URL." >&2
    exit 1
  fi
  mv "$2.part" "$2"
}

if [ -d "$target" ]; then
  echo "20 Newsgroups: $target/ exists, nothing to do."
  exit 0
fi

if [ ! -f "$archive" ]; then
  case "$url" in
    *.tar.gz)
      download "$url" "$archive"
      ;;
    *)
      [ -f "$zip" ] || download "$url" "$zip"
      [ -n "${NEWS20_URL:-}" ] || check_sha256 "$zip" "$UCI_ZIP_SHA256"
      unzip -o -q "$zip" "$archive"
      [ -n "${NEWS20_URL:-}" ] || check_sha256 "$archive" "$UCI_TAR_SHA256"
      ;;
  esac
fi

echo "20 Newsgroups: extracting $archive"
tar -xzf "$archive"
if [ ! -d "$target" ]; then
  echo "error: $archive did not contain $target/." >&2
  exit 1
fi
