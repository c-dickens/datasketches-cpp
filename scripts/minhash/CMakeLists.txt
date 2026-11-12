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

# Fork-only project for the MinHash demos. It is separate from the root
# CMakeLists.txt, and it builds against the in-tree library. Upstream PRs leave
# scripts/ out. The demos may use C++17.
#
#   cmake -S scripts/minhash -B scripts/minhash/build -DCMAKE_BUILD_TYPE=Release
#   cmake --build scripts/minhash/build -j
#   ctest --test-dir scripts/minhash/build --output-on-failure

cmake_minimum_required(VERSION 3.16.0)
project(minhash_demos LANGUAGES CXX)

# The in-tree library. Its own tests are not needed here.
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.. ${CMAKE_CURRENT_BINARY_DIR}/datasketches EXCLUDE_FROM_ALL)

if (MSVC)
  set(DEMO_WARNINGS /W4)
else()
  set(DEMO_WARNINGS -Wall -Wextra -pedantic)
endif()

add_library(news20 STATIC src/loader.cpp)
target_include_directories(news20 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(news20 PUBLIC cxx_std_17)
target_compile_options(news20 PRIVATE ${DEMO_WARNINGS})
target_link_libraries(news20 PUBLIC datasketches common)

add_executable(news20_stats src/news20_stats.cpp)
target_link_libraries(news20_stats PRIVATE news20)
target_compile_options(news20_stats PRIVATE ${DEMO_WARNINGS})

# nlohmann/json reads the trajectory JSON lines. CMake fetches one pinned release.
include(FetchContent)
if (POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)  # extracted files get the time of extraction
endif()
FetchContent_Declare(
  nlohmann_json
  URL      https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
)
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

add_library(trajectory STATIC src/featurizer.cpp)
target_include_directories(trajectory PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(trajectory PUBLIC cxx_std_17)
target_compile_options(trajectory PRIVATE ${DEMO_WARNINGS})
target_link_libraries(trajectory PUBLIC nlohmann_json::nlohmann_json)

add_executable(trajectory_stats src/trajectory_stats.cpp)
target_link_libraries(trajectory_stats PRIVATE trajectory)
target_compile_options(trajectory_stats PRIVATE ${DEMO_WARNINGS})

include(CTest)
if (BUILD_TESTING)
  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2
    GIT_TAG        v2.13.9
  )
  FetchContent_MakeAvailable(Catch2)

  add_executable(news20_test test/loader_test.cpp)
  target_link_libraries(news20_test PRIVATE news20 Catch2::Catch2)
  target_compile_options(news20_test PRIVATE ${DEMO_WARNINGS})
  add_test(NAME news20_test COMMAND news20_test)

  add_executable(trajectory_test test/featurizer_test.cpp)
  target_link_libraries(trajectory_test PRIVATE trajectory Catch2::Catch2)
  target_compile_options(trajectory_test PRIVATE ${DEMO_WARNINGS})
  add_test(NAME trajectory_test COMMAND trajectory_test)
endif()
