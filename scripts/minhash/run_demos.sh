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
# The one command that runs every MinHash demo: it gets the data, builds the
# demos, and runs them. Later tickets add their demos here.

set -euo pipefail

cd "$(dirname "$0")"

jobs="${JOBS:-4}"

data/get_20news.sh

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF > /dev/null
cmake --build build -j "$jobs" > /dev/null

echo "== 20 Newsgroups: data check =="
build/news20_stats data/20_newsgroups
