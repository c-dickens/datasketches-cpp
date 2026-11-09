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

#include "trajectory/featurizer.hpp"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <regex>
#include <stdexcept>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

namespace trajectory {

namespace {

using json = nlohmann::json;

const char* const NO_SUBCOMMAND = "-";

bool is_space(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

std::string trim_left(const std::string& s) {
  std::size_t i = 0;
  while (i < s.size() && is_space(s[i])) {
    ++i;
  }
  return s.substr(i);
}

bool starts_with(const std::string& s, const std::string& prefix) {
  return s.compare(0, prefix.size(), prefix) == 0;
}

// The four bash footer lines. Each pattern matches one whole line.
const std::regex& footer_line() {
  static const std::regex re(
      R"(\[The command completed with exit code -?\d+\.\]|)"
      R"(\[Current working directory: [^\n]*\]|)"
      R"(\[Python interpreter: [^\n]*\]|)"
      R"(\[Command finished with exit code -?\d+\])");
  return re;
}

// A footer line that gives the exit code, captured in group 1.
const std::regex& exit_code_line() {
  static const std::regex re(
      R"(\[(?:The command completed with exit code (-?\d+)\.|Command finished with exit code (-?\d+))\])");
  return re;
}

const char* const TIMEOUT_NOTICE = "[The command timed out after";
const char* const EDITOR_ERROR = "ERROR:";

// A user message that reports a malformed tool call.
bool is_malformed_report(const std::string& text) {
  return text.find("[Error occurred in processing last action]") != std::string::npos ||
         (starts_with(text, "Tool ") && text.find(" is not registered.") != std::string::npos);
}

std::string string_field(const json& object, const char* key) {
  if (object.is_object()) {
    const auto it = object.find(key);
    if (it != object.end() && it->is_string()) {
      return it->get<std::string>();
    }
  }
  return {};
}

// The "command" argument of a tool call. args is a JSON string, so it is parsed a second time.
std::string command_argument(const json& message) {
  const std::string args = string_field(message, "args");
  if (args.empty()) {
    return {};
  }
  const json parsed = json::parse(args, nullptr, false);
  return parsed.is_discarded() ? std::string() : string_field(parsed, "command");
}

std::string or_dash(const std::string& s) {
  return s.empty() ? std::string(NO_SUBCOMMAND) : s;
}

// The step of one assistant message with a tool call. `reply` is the tool
// message after it, or null if there is none.
step make_step(const std::string& tool, const json& message, const json* reply) {
  const std::string out = reply != nullptr ? string_field(*reply, "out") : std::string();
  if (tool == "execute_bash") {
    const std::string command = strip_workspace_prefix(command_argument(message));
    return step{tool, bash_subcommand(command), bash_outcome(out)};
  }
  if (tool == "str_replace_editor") {
    return step{tool, or_dash(command_argument(message)), starts_with(out, EDITOR_ERROR) ? "error" : "ok"};
  }
  if (tool == "task_tracker") {
    return step{tool, or_dash(command_argument(message)), "ok"};
  }
  // think, finish and any other tool.
  return step{tool, NO_SUBCOMMAND, "ok"};
}

} // namespace

std::string step::symbol() const {
  return tool + ":" + subcommand + ":" + outcome;
}

std::string strip_workspace_prefix(const std::string& command) {
  static const std::regex prefix(R"(^cd /workspace/[^/\s&]+\s*&&\s*)");
  const std::string trimmed = trim_left(command);
  std::smatch m;
  if (std::regex_search(trimmed, m, prefix)) {
    return trimmed.substr(static_cast<std::size_t>(m.length(0)));
  }
  return trimmed;
}

std::string bash_subcommand(const std::string& command) {
  const std::string trimmed = trim_left(command);
  std::size_t end = 0;
  while (end < trimmed.size() && !is_space(trimmed[end])) {
    ++end;
  }
  return or_dash(trimmed.substr(0, end));
}

std::string strip_bash_footer(const std::string& output) {
  std::string result;
  std::size_t start = 0;
  while (start < output.size()) {
    std::size_t end = output.find('\n', start);
    const bool has_newline = end != std::string::npos;
    if (!has_newline) {
      end = output.size();
    }
    const std::string line = output.substr(start, end - start);
    if (!std::regex_match(line, footer_line())) {
      result += line;
      if (has_newline) {
        result.push_back('\n');
      }
    }
    start = end + 1;
  }
  return result;
}

std::string bash_outcome(const std::string& output) {
  if (output.find(TIMEOUT_NOTICE) != std::string::npos) {
    return "timeout";
  }
  std::string code;
  for (auto it = std::sregex_iterator(output.begin(), output.end(), exit_code_line()); it != std::sregex_iterator();
       ++it) {
    code = (*it)[1].matched ? (*it)[1].str() : (*it)[2].str();
  }
  if (code.empty()) {
    return "unknown";
  }
  return code == "0" ? "ok" : "exit" + code;
}

record parse_line(const std::string& line) {
  const json doc = json::parse(line, nullptr, false);
  if (doc.is_discarded() || !doc.is_object()) {
    throw std::invalid_argument("not a JSON object");
  }
  const auto steps = doc.find("steps");
  if (steps == doc.end() || !steps->is_array()) {
    throw std::invalid_argument("no \"steps\" array");
  }
  record r;
  r.trajectory_id = string_field(doc, "trajectory_id");
  r.instance_id = string_field(doc, "instance_id");
  const auto resolved = doc.find("resolved");
  r.resolved = resolved != doc.end() && resolved->is_number_integer() && resolved->get<int>() == 1;

  const json& messages = *steps;
  for (std::size_t i = 0; i < messages.size(); ++i) {
    const json& message = messages[i];
    const std::string role = string_field(message, "role");
    if (role == "assistant") {
      const std::string tool = string_field(message, "fn");
      if (tool.empty()) {
        continue;  // an assistant message without a tool call is not a step
      }
      const json* reply = nullptr;
      if (i + 1 < messages.size() && string_field(messages[i + 1], "role") == "tool") {
        reply = &messages[i + 1];
      }
      r.steps.push_back(make_step(tool, message, reply));
    } else if (role == "user" && is_malformed_report(string_field(message, "out")) && !r.steps.empty()) {
      r.steps.back().outcome = "malformed";
    }
  }
  return r;
}

std::string encode_token(const std::vector<std::string>& symbols) {
  std::string token;
  for (const auto& s : symbols) {
    token += std::to_string(s.size());
    token.push_back(':');
    token += s;
  }
  return token;
}

std::vector<std::string> tokens(const std::vector<step>& steps, std::size_t width) {
  std::vector<std::string> result;
  if (width == 0 || steps.size() < width) {
    return result;
  }
  std::vector<std::string> symbols;
  symbols.reserve(steps.size());
  for (const auto& s : steps) {
    symbols.push_back(s.symbol());
  }
  result.reserve(steps.size() - width + 1);
  for (std::size_t i = 0; i + width <= symbols.size(); ++i) {
    const std::vector<std::string> window(symbols.begin() + static_cast<std::ptrdiff_t>(i),
                                          symbols.begin() + static_cast<std::ptrdiff_t>(i + width));
    result.push_back(encode_token(window));
  }
  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

std::vector<record> load_trajectories(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error("cannot read " + path.string());
  }
  std::vector<record> result;
  std::string line;
  std::size_t number = 0;
  while (std::getline(in, line)) {
    ++number;
    if (trim_left(line).empty()) {
      continue;
    }
    try {
      result.push_back(parse_line(line));
    } catch (const std::invalid_argument& e) {
      throw std::invalid_argument(path.string() + " line " + std::to_string(number) + ": " + e.what());
    }
  }
  return result;
}

} // namespace trajectory
