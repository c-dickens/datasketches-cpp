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

#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "trajectory/featurizer.hpp"

namespace fs = std::filesystem;
using strings = std::vector<std::string>;

namespace {

strings symbols_of(const trajectory::record& r) {
  strings result;
  for (const auto& s : r.steps) {
    result.push_back(s.symbol());
  }
  return result;
}

// Hand-written JSON lines in the layout of data/get_trajectories.sh. Each
// message is {role, fn, args, out}, and args is a JSON string.

// A bash call with the workspace prefix, whose reply ends with the footer.
const char* const LINE_BASH =
    R"({"trajectory_id":"t1","instance_id":"org__repo-1","resolved":1,"exit_status":"submit","steps":[)"
    R"({"role":"assistant","fn":"execute_bash","args":"{\"command\": \"cd /workspace/org__repo__1.0 && pytest -q tests\"}","out":"Run the tests."},)"
    R"({"role":"tool","fn":"execute_bash","args":null,"out":"3 passed in 0.1s\n[The command completed with exit code 0.]\n[Current working directory: /workspace/org__repo__1.0]\n[Python interpreter: /opt/conda/envs/testbed/bin/python]\n[Command finished with exit code 0]"},)"
    R"({"role":"assistant","fn":"execute_bash","args":"{\"command\": \"grep -rn foo .\"}","out":""},)"
    R"({"role":"tool","fn":"execute_bash","args":null,"out":"[The command completed with exit code 1.]\n[Current working directory: /workspace/org__repo__1.0]\n[Python interpreter: /opt/conda/envs/testbed/bin/python]\n[Command finished with exit code 1]"},)"
    R"({"role":"assistant","fn":"finish","args":"{\"message\": \"done\"}","out":""})"
    R"(]})";

// Every tool, an editor error, a timeout, a malformed call and an assistant message without a tool call.
const char* const LINE_MIXED =
    R"({"trajectory_id":"t2","instance_id":"org__repo-2","resolved":0,"exit_status":"submit","steps":[)"
    R"({"role":"assistant","fn":"str_replace_editor","args":"{\"command\": \"view\", \"path\": \"/workspace/r\"}","out":""},)"
    R"({"role":"tool","fn":"str_replace_editor","args":null,"out":"Here's the files and directories"},)"
    R"({"role":"assistant","fn":"str_replace_editor","args":"{\"path\": \"/workspace/r/a.py\", \"command\": \"str_replace\"}","out":""},)"
    R"({"role":"tool","fn":"str_replace_editor","args":null,"out":"ERROR:\nNo replacement was performed."},)"
    R"({"role":"assistant","fn":"task_tracker","args":"{\"command\": \"plan\", \"task_list\": []}","out":""},)"
    R"({"role":"tool","fn":"task_tracker","args":null,"out":"Task list has been updated."},)"
    R"({"role":"assistant","fn":"think","args":"{\"thought\": \"hmm\"}","out":""},)"
    R"({"role":"tool","fn":"think","args":null,"out":"Your thought has been logged."},)"
    R"({"role":"assistant","fn":"execute_bash","args":"{\"command\": \"cd /workspace/r && pip install -e .\"}","out":""},)"
    R"({"role":"tool","fn":"execute_bash","args":null,"out":"Collecting x\n[The command timed out after 300.0 seconds. You may wait longer to see additional output by sending empty command '', send other commands to interact with the current process, send keys (\"C-c\", \"C-z\", \"C-d\") to interrupt/kill the previous command before sending your new command, or use the timeout parameter in execute_bash for future commands.]"},)"
    R"({"role":"user","fn":null,"args":null,"out":"Missing required argument \"command\" in tool call str_replace_editor\n[Error occurred in processing last action]"},)"
    R"({"role":"assistant","fn":null,"args":null,"out":"Let me think."},)"
    R"({"role":"user","fn":null,"args":null,"out":"Please continue working on the task on whatever approach you think is suitable.\n"},)"
    R"({"role":"assistant","fn":"execute_bash","args":"{\"command\": \"python -c 'print(1)' \"}","out":""},)"
    R"({"role":"tool","fn":"execute_bash","args":null,"out":"a long reply that the source cut off before its foo"})"
    R"(]})";

// A temporary file that is removed at the end of the test.
struct temp_file {
  fs::path path;
  explicit temp_file(const std::string& text) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path = fs::temp_directory_path() / ("trajectory_test_" + std::to_string(stamp) + ".jsonl");
    std::ofstream out(path, std::ios::binary);
    out << text;
  }
  ~temp_file() {
    std::error_code ec;
    fs::remove(path, ec);
  }
  temp_file(const temp_file&) = delete;
  temp_file& operator=(const temp_file&) = delete;
};

} // namespace

TEST_CASE("strip_workspace_prefix removes only a leading cd /workspace/<dir> &&", "[trajectory]") {
  using trajectory::strip_workspace_prefix;
  REQUIRE(strip_workspace_prefix("cd /workspace/org__repo__1.0 && pytest -q") == "pytest -q");
  REQUIRE(strip_workspace_prefix("  cd /workspace/r &&   python x.py") == "python x.py");
  REQUIRE(strip_workspace_prefix("cd /workspace/r&&ls") == "ls");
  // Only one path component after /workspace/.
  REQUIRE(strip_workspace_prefix("cd /workspace/r/src && ls") == "cd /workspace/r/src && ls");
  REQUIRE(strip_workspace_prefix("cd /tmp && ls") == "cd /tmp && ls");
  REQUIRE(strip_workspace_prefix("cd /workspace && ls") == "cd /workspace && ls");
  // Not leading, so it stays.
  REQUIRE(strip_workspace_prefix("ls && cd /workspace/r && ls") == "ls && cd /workspace/r && ls");
  REQUIRE(strip_workspace_prefix("cd /workspace/r") == "cd /workspace/r");
  REQUIRE(strip_workspace_prefix("").empty());
}

TEST_CASE("bash_subcommand is the first word", "[trajectory]") {
  using trajectory::bash_subcommand;
  REQUIRE(bash_subcommand("pytest -q tests") == "pytest");
  REQUIRE(bash_subcommand("python\n-c 'x'") == "python");
  REQUIRE(bash_subcommand("\tgrep -rn foo") == "grep");
  REQUIRE(bash_subcommand("ls") == "ls");
  REQUIRE(bash_subcommand("") == "-");
  REQUIRE(bash_subcommand("  \n") == "-");
}

TEST_CASE("strip_bash_footer drops the four footer lines", "[trajectory]") {
  using trajectory::strip_bash_footer;
  const std::string reply =
      "3 passed\n"
      "[The command completed with exit code 0.]\n"
      "[Current working directory: /workspace/r]\n"
      "[Python interpreter: /opt/conda/envs/testbed/bin/python]\n"
      "[Command finished with exit code 0]";
  REQUIRE(strip_bash_footer(reply) == "3 passed\n");
  REQUIRE(strip_bash_footer("a\n[IU]\nb") == "a\n[IU]\nb");
  REQUIRE(strip_bash_footer("x [Command finished with exit code 0]") == "x [Command finished with exit code 0]");
  REQUIRE(strip_bash_footer("").empty());
}

TEST_CASE("bash_outcome reads the exit code and the timeout notice", "[trajectory]") {
  using trajectory::bash_outcome;
  REQUIRE(bash_outcome("ok\n[The command completed with exit code 0.]\n[Command finished with exit code 0]") == "ok");
  REQUIRE(bash_outcome("[The command completed with exit code 2.]") == "exit2");
  REQUIRE(bash_outcome("[Command finished with exit code 127]") == "exit127");
  REQUIRE(bash_outcome("[Command finished with exit code -1]") == "exit-1");
  // The last footer line wins.
  REQUIRE(bash_outcome("[Command finished with exit code 1]\n...\n[Command finished with exit code 0]") == "ok");
  REQUIRE(bash_outcome("x\n[The command timed out after 300.0 seconds. You may wait longer]") == "timeout");
  REQUIRE(bash_outcome("output cut off before the footer") == "unknown");
  REQUIRE(bash_outcome("") == "unknown");
}

TEST_CASE("encode_token writes <length>:<symbol> for each symbol", "[trajectory]") {
  using trajectory::encode_token;
  REQUIRE(encode_token({"think:-:ok", "finish:-:ok"}) == "10:think:-:ok11:finish:-:ok");
  REQUIRE(encode_token({}).empty());
  // Two different lists that a plain join would confuse.
  REQUIRE(encode_token({"a:", "b"}) != encode_token({"a", ":b"}));
  REQUIRE(encode_token({"ab", "c"}) != encode_token({"a", "bc"}));
}

TEST_CASE("a bash call with the workspace prefix and the footer", "[trajectory]") {
  const auto r = trajectory::parse_line(LINE_BASH);
  REQUIRE(r.trajectory_id == "t1");
  REQUIRE(r.instance_id == "org__repo-1");
  REQUIRE(r.resolved);
  REQUIRE(symbols_of(r) == strings{"execute_bash:pytest:ok", "execute_bash:grep:exit1", "finish:-:ok"});
  const auto t = trajectory::tokens(r.steps);
  REQUIRE(t == strings{"22:execute_bash:pytest:ok23:execute_bash:grep:exit111:finish:-:ok"});
}

TEST_CASE("every tool, an editor error, a timeout and a malformed call", "[trajectory]") {
  const auto r = trajectory::parse_line(LINE_MIXED);
  REQUIRE_FALSE(r.resolved);
  REQUIRE(symbols_of(r) == strings{
      "str_replace_editor:view:ok",
      "str_replace_editor:str_replace:error",
      "task_tracker:plan:ok",
      "think:-:ok",
      "execute_bash:pip:malformed",  // the malformed-call report sets the step before it
      "execute_bash:python:unknown",
  });
  REQUIRE(trajectory::tokens(r.steps).size() == 4);
}

TEST_CASE("tokens are a set of 3-step windows", "[trajectory]") {
  using trajectory::step;
  const step a{"think", "-", "ok"};
  const step b{"execute_bash", "ls", "ok"};
  // a b a b a b: the windows aba, bab repeat, so the set has 2 tokens.
  const auto t = trajectory::tokens({a, b, a, b, a, b});
  REQUIRE(t.size() == 2);
  REQUIRE(std::is_sorted(t.begin(), t.end()));
  REQUIRE(trajectory::tokens({a, b}).empty());
  REQUIRE(trajectory::tokens({}).empty());
  REQUIRE(trajectory::tokens({a, b}, 0).empty());
  REQUIRE(trajectory::tokens({a, b}, 1).size() == 2);
}

TEST_CASE("parse_line rejects bad input", "[trajectory]") {
  REQUIRE_THROWS_AS(trajectory::parse_line("not json"), std::invalid_argument);
  REQUIRE_THROWS_AS(trajectory::parse_line("[1, 2]"), std::invalid_argument);
  REQUIRE_THROWS_AS(trajectory::parse_line(R"({"trajectory_id":"t"})"), std::invalid_argument);
  // args that is not valid JSON gives no subcommand.
  const auto r = trajectory::parse_line(
      R"({"trajectory_id":"t","instance_id":"i","resolved":0,"steps":[{"role":"assistant","fn":"execute_bash","args":"{bad","out":""}]})");
  REQUIRE(symbols_of(r) == strings{"execute_bash:-:unknown"});
}

TEST_CASE("load_trajectories reads a file of JSON lines", "[trajectory]") {
  const temp_file file(std::string(LINE_BASH) + "\n\n" + LINE_MIXED + "\n");
  const auto all = trajectory::load_trajectories(file.path);
  REQUIRE(all.size() == 2);
  REQUIRE(all[0].trajectory_id == "t1");
  REQUIRE(all[1].trajectory_id == "t2");

  const temp_file bad(std::string(LINE_BASH) + "\n{oops\n");
  REQUIRE_THROWS_WITH(trajectory::load_trajectories(bad.path), Catch::Contains("line 2"));
  REQUIRE_THROWS_AS(trajectory::load_trajectories("/no/such/file.jsonl"), std::runtime_error);
}
