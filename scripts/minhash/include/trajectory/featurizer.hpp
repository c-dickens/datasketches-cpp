/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef TRAJECTORY_FEATURIZER_HPP_
#define TRAJECTORY_FEATURIZER_HPP_

// Fork-only demo code (scripts/minhash/). It is C++17 and is not part of the library.
//
// Trajectory featurizer v1. It turns one trajectory (one line of
// data/trajectories_<N>.jsonl, written by data/get_trajectories.sh) into a set
// of tokens that describes what the agent did. The tokens are the sketch's
// input items.
//
// Each step (an assistant message with a tool call) becomes one symbol,
// "tool:subcommand:outcome". A token is 3 steps in a row.

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace trajectory {

/// The number of steps in one token.
constexpr std::size_t TOKEN_STEPS = 3;

/// One step: one tool call of the agent, with its outcome.
struct step {
  std::string tool;        ///< the tool name, for example "execute_bash"
  std::string subcommand;  ///< for example "pytest" or "view"; "-" if the tool has none
  std::string outcome;     ///< "ok", "exit<N>", "timeout", "error", "malformed" or "unknown"

  /// The symbol "tool:subcommand:outcome".
  std::string symbol() const;
};

/// One trajectory, with its steps.
struct record {
  std::string trajectory_id;
  std::string instance_id;
  bool resolved = false;
  std::vector<step> steps;
};

/// Removes a leading "cd /workspace/<dir> && " from a bash command, where <dir>
/// is one path component. Leading white space is removed first. Any other
/// command is returned without its leading white space.
std::string strip_workspace_prefix(const std::string& command);

/// The first word of a normalised bash command (after strip_workspace_prefix).
/// Words are separated by white space. An empty command gives "-".
std::string bash_subcommand(const std::string& command);

/// Drops the bash footer lines from a bash reply:
/// "[The command completed with exit code N.]", "[Current working directory: …]",
/// "[Python interpreter: …]" and "[Command finished with exit code N]".
/// Other lines are kept, each with its newline.
std::string strip_bash_footer(const std::string& output);

/// The outcome of a bash reply: "timeout" if it holds the timeout notice
/// ("[The command timed out after"), else "ok" for exit code 0 and "exit<N>"
/// for another exit code, read from the last footer line that gives one.
/// A reply with neither gives "unknown". The source cuts each reply to 1,000
/// characters, which often cuts the footer off.
std::string bash_outcome(const std::string& output);

/// Parses one JSON line of the trajectory file into a record with its steps.
/// Throws std::invalid_argument if the line is not a JSON object with a
/// "steps" array.
record parse_line(const std::string& line);

/// Encodes a list of symbols as one string: each symbol is written as
/// "<length>:<symbol>", and the parts are joined with nothing between them.
/// The length is in bytes. Two different lists never give the same string.
std::string encode_token(const std::vector<std::string>& symbols);

/// The token set of a list of steps: the encoding of each run of `width`
/// consecutive symbols. The result is sorted and has no duplicates.
/// A list with fewer than `width` steps has no tokens.
std::vector<std::string> tokens(const std::vector<step>& steps, std::size_t width = TOKEN_STEPS);

/// Reads every line of a trajectory file. Empty lines are skipped.
/// Throws std::runtime_error if the file cannot be read, and
/// std::invalid_argument (with the line number) if a line cannot be parsed.
std::vector<record> load_trajectories(const std::filesystem::path& path);

} // namespace trajectory

#endif // TRAJECTORY_FEATURIZER_HPP_
