// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "kudu/util/subprocess.h"

#include <unistd.h>

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "kudu/gutil/strings/substitute.h"
#include "kudu/util/test_util.h"

using std::string;
using std::vector;
using strings::Substitute;

namespace kudu {

class SubprocessTest : public KuduTest {};

TEST_F(SubprocessTest, TestSimplePipe) {
  Subprocess p({ "/usr/bin/tr", "a-z", "A-Z" });
  p.ShareParentStdout(false);
  ASSERT_OK(p.Start());

  FILE* out = fdopen(p.ReleaseChildStdinFd(), "w");
  PCHECK(out);
  FILE* in = fdopen(p.from_child_stdout_fd(), "r");
  PCHECK(in);

  fprintf(out, "hello world\n");
  // We have to close 'out' or else tr won't write any output, since
  // it enters a buffered mode if it detects that its input is a FIFO.
  fclose(out);

  char buf[1024];
  ASSERT_EQ(buf, fgets(buf, sizeof(buf), in));
  ASSERT_STREQ("HELLO WORLD\n", &buf[0]);

  int wait_status = 0;
  ASSERT_OK(p.Wait(&wait_status));
  ASSERT_TRUE(WIFEXITED(wait_status));
  ASSERT_EQ(0, WEXITSTATUS(wait_status));
}

TEST_F(SubprocessTest, TestErrPipe) {
  Subprocess p({ "/usr/bin/tee", "/dev/stderr" });
  p.ShareParentStderr(false);
  ASSERT_OK(p.Start());

  FILE* out = fdopen(p.ReleaseChildStdinFd(), "w");
  PCHECK(out);

  fprintf(out, "Hello, World\n");
  fclose(out); // same reasoning as above, flush to prevent tee buffering

  FILE* in = fdopen(p.from_child_stderr_fd(), "r");
  PCHECK(in);

  char buf[1024];
  ASSERT_EQ(buf, fgets(buf, sizeof(buf), in));
  ASSERT_STREQ("Hello, World\n", &buf[0]);

  int wait_status = 0;
  ASSERT_OK(p.Wait(&wait_status));
  ASSERT_TRUE(WIFEXITED(wait_status));
  ASSERT_EQ(0, WEXITSTATUS(wait_status));
}

TEST_F(SubprocessTest, TestKill) {
  Subprocess p({ "/bin/cat" });
  ASSERT_OK(p.Start());

  ASSERT_OK(p.Kill(SIGKILL));

  int wait_status = 0;
  ASSERT_OK(p.Wait(&wait_status));
  ASSERT_TRUE(WIFSIGNALED(wait_status));
  ASSERT_EQ(SIGKILL, WTERMSIG(wait_status));

  // Test that calling Wait() a second time returns the same
  // cached value instead of trying to wait on some other process
  // that was assigned the same pid.
  wait_status = 0;
  ASSERT_OK(p.Wait(&wait_status));
  ASSERT_TRUE(WIFSIGNALED(wait_status));
  ASSERT_EQ(SIGKILL, WTERMSIG(wait_status));
}

// Writes enough bytes to stdout and stderr concurrently that if Call() were
// fully reading them one at a time, the test would deadlock.
TEST_F(SubprocessTest, TestReadFromStdoutAndStderr) {
  // Set an alarm to break out of any potential deadlocks (if the implementation
  // regresses).
  alarm(60);

  string stdout;
  string stderr;
  ASSERT_OK(Subprocess::Call({
    "/bin/bash",
    "-c",
    "dd if=/dev/urandom of=/dev/stdout bs=512 count=2048 &"
    "dd if=/dev/urandom of=/dev/stderr bs=512 count=2048 &"
    "wait"
  }, "", &stdout, &stderr));
}

// Test that environment variables can be passed to the subprocess.
TEST_F(SubprocessTest, TestEnvVars) {
  Subprocess p({ "/bin/bash", "-c", "echo $FOO" });
  p.SetEnvVars({{"FOO", "bar"}});
  p.ShareParentStdout(false);
  ASSERT_OK(p.Start());
  FILE* in = fdopen(p.from_child_stdout_fd(), "r");
  PCHECK(in);
  char buf[1024];
  ASSERT_EQ(buf, fgets(buf, sizeof(buf), in));
  ASSERT_STREQ("bar\n", &buf[0]);
  ASSERT_OK(p.Wait());
}

// Tests writing to the subprocess stdin.
TEST_F(SubprocessTest, TestCallWithStdin) {
  string stdout;
  ASSERT_OK(Subprocess::Call({ "/bin/bash" },
                             "echo \"quick brown fox\"",
                             &stdout));
  EXPECT_EQ("quick brown fox\n", stdout);
}

// Test KUDU-1674: '/bin/bash -c "echo"' command below is expected to
// capture a string on stderr. This test validates that passing
// stderr alone doesn't result in SIGSEGV as reported in the bug and
// also check for sanity of stderr in the output.
TEST_F(SubprocessTest, TestReadSingleFD) {
  string stderr;
  const string str = "ApacheKudu";
  const string cmd_str = Substitute("/bin/echo -n $0 1>&2", str);
  ASSERT_OK(Subprocess::Call({"/bin/sh", "-c", cmd_str}, "", nullptr, &stderr));
  ASSERT_EQ(stderr, str);

  // Also sanity check other combinations.
  string stdout;
  ASSERT_OK(Subprocess::Call({"/bin/ls", "/dev/null"}, "", &stdout, nullptr));
  ASSERT_STR_CONTAINS(stdout, "/dev/null");

  ASSERT_OK(Subprocess::Call({"/bin/ls", "/dev/zero"}, "", nullptr, nullptr));
}

TEST_F(SubprocessTest, TestGetExitStatusExitSuccess) {
  Subprocess p({ "/bin/sh", "-c", "exit 0" });
  ASSERT_OK(p.Start());
  ASSERT_OK(p.Wait());
  int exit_status;
  string exit_info;
  ASSERT_OK(p.GetExitStatus(&exit_status, &exit_info));
  ASSERT_EQ(0, exit_status);
  ASSERT_STR_CONTAINS(exit_info, "process successfully exited");
}

TEST_F(SubprocessTest, TestGetExitStatusExitFailure) {
  static const vector<int> kStatusCodes = { 1, 255 };
  for (auto code : kStatusCodes) {
    Subprocess p({ "/bin/sh", "-c", Substitute("exit $0", code) });
    ASSERT_OK(p.Start());
    ASSERT_OK(p.Wait());
    int exit_status;
    string exit_info;
    ASSERT_OK(p.GetExitStatus(&exit_status, &exit_info));
    ASSERT_EQ(code, exit_status);
    ASSERT_STR_CONTAINS(exit_info,
                        Substitute("process exited with non-zero status $0",
                                   exit_status));
  }
}

TEST_F(SubprocessTest, TestGetExitStatusSignaled) {
  static const vector<int> kSignals = {
    SIGHUP,
    SIGABRT,
    SIGKILL,
    SIGTERM,
    SIGUSR2,
  };
  for (auto signum : kSignals) {
    Subprocess p({ "/bin/cat" });
    ASSERT_OK(p.Start());
    ASSERT_OK(p.Kill(signum));
    ASSERT_OK(p.Wait());
    int exit_status;
    string exit_info;
    ASSERT_OK(p.GetExitStatus(&exit_status, &exit_info));
    EXPECT_EQ(signum, exit_status);
    ASSERT_STR_CONTAINS(exit_info, Substitute("process exited on signal $0",
                                              signum));
  }
}

TEST_F(SubprocessTest, TestSubprocessDestroyWithCustomSignal) {
  string kTestFile = GetTestPath("foo");

  // Start a subprocess that creates kTestFile immediately and deletes it on exit.
  //
  // Note: it's important that the shell not invoke a command while waiting
  // to be killed (i.e. "sleep 60"); if it did, the signal could be delivered
  // just after the command starts but just before the shell decides to forward
  // signals to it, and we wind up with a deadlock.
  vector<string> argv = {
      "/bin/bash",
      "-c",
      Substitute(
          // Delete kTestFile on exit.
          "trap \"rm $0\" EXIT;"
          // Create kTestFile on start.
          "touch $0;"
          // Spin in a tight loop waiting to be killed.
          "while true;"
          "  do FOO=$$((FOO + 1));"
          "done", kTestFile)
  };

  {
    Subprocess s(argv);
    ASSERT_OK(s.Start());
    AssertEventually([&]{
        ASSERT_TRUE(env_->FileExists(kTestFile));
    });
  }

  // The subprocess went out of scope and was killed with SIGKILL, so it left
  // kTestFile behind.
  ASSERT_TRUE(env_->FileExists(kTestFile));

  ASSERT_OK(env_->DeleteFile(kTestFile));
  {
    Subprocess s(argv, SIGTERM);
    ASSERT_OK(s.Start());
    AssertEventually([&]{
        ASSERT_TRUE(env_->FileExists(kTestFile));
    });
  }

  // The subprocess was killed with SIGTERM, giving it a chance to delete kTestFile.
  ASSERT_FALSE(env_->FileExists(kTestFile));
}

} // namespace kudu
