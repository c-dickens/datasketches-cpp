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

add_executable(minhash_document_similarity minhash_document_similarity.cpp)
target_link_libraries(minhash_document_similarity PRIVATE minhash)
set_target_properties(minhash_document_similarity PROPERTIES CXX_STANDARD_REQUIRED YES)

add_executable(minhash_newsgroups minhash_newsgroups.cpp)
target_link_libraries(minhash_newsgroups PRIVATE minhash)
set_target_properties(minhash_newsgroups PROPERTIES CXX_STANDARD_REQUIRED YES)
