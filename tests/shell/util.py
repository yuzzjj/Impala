#!/usr/bin/env impala-python
# encoding=utf-8
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

import os
import pytest
import re
import shlex
from subprocess import Popen, PIPE

IMPALAD_HOST_PORT_LIST = pytest.config.option.impalad.split(',')
assert len(IMPALAD_HOST_PORT_LIST) > 0, 'Must specify at least 1 impalad to target'
IMPALAD = IMPALAD_HOST_PORT_LIST[0]
SHELL_CMD = "%s/bin/impala-shell.sh -i %s" % (os.environ['IMPALA_HOME'], IMPALAD)

def assert_var_substitution(result):
  assert_pattern(r'\bfoo_number=.*$', 'foo_number= 123123', result.stdout, \
    'Numeric values not replaced correctly')
  assert_pattern(r'\bfoo_string=.*$', 'foo_string=123', result.stdout, \
    'String values not replaced correctly')
  assert_pattern(r'\bVariables:[\s\n]*BAR:\s*[0-9]*\n\s*FOO:\s*[0-9]*', \
    'Variables:\n\tBAR: 456\n\tFOO: 123', result.stdout, \
    "Set variable not listed correctly by the first SET command")
  assert_pattern(r'\bError: Unknown variable FOO1$', \
    'Error: Unknown variable FOO1', result.stderr, \
    'Missing variable FOO1 not reported correctly')
  assert_pattern(r'\bmulti_test=.*$', 'multi_test=456_123_456_123', \
    result.stdout, 'Multiple replaces not working correctly')
  assert_pattern(r'\bError:\s*Unknown\s*substitution\s*syntax\s*' +
                 r'\(RANDOM_NAME\). Use \${VAR:var_name}', \
    'Error: Unknown substitution syntax (RANDOM_NAME). Use ${VAR:var_name}', \
    result.stderr, "Invalid variable reference")
  assert_pattern(r'"This should be not replaced: \${VAR:foo} \${HIVEVAR:bar}"',
    '"This should be not replaced: ${VAR:foo} ${HIVEVAR:bar}"', \
    result.stdout, "Variable escaping not working")
  assert_pattern(r'\bVariable MYVAR set to.*$', 'Variable MYVAR set to foo123',
    result.stderr, 'No evidence of MYVAR variable being set.')
  assert_pattern(r'\bVariables:[\s\n]*BAR:.*[\s\n]*FOO:.*[\s\n]*MYVAR:.*$',
    'Variables:\n\tBAR: 456\n\tFOO: 123\n\tMYVAR: foo123', result.stdout,
    'Set variables not listed correctly by the second SET command')
  assert_pattern(r'\bUnsetting variable FOO$', 'Unsetting variable FOO',
    result.stdout, 'No evidence of variable FOO being unset')
  assert_pattern(r'\bUnsetting variable BAR$', 'Unsetting variable BAR',
    result.stdout, 'No evidence of variable BAR being unset')
  assert_pattern(r'\bVariables:[\s\n]*No variables defined\.$', \
    'Variables:\n\tNo variables defined.', result.stdout, \
    'Unset variables incorrectly listed by third SET command.')
  assert_pattern(r'\bNo variable called NONEXISTENT is set', \
    'No variable called NONEXISTENT is set', result.stdout, \
    'Problem unsetting non-existent variable.')
  assert_pattern(r'\bVariable COMMENT_TYPE1 set to.*$',
    'Variable COMMENT_TYPE1 set to ok', result.stderr,
    'No evidence of COMMENT_TYPE1 variable being set.')
  assert_pattern(r'\bVariable COMMENT_TYPE2 set to.*$',
    'Variable COMMENT_TYPE2 set to ok', result.stderr,
    'No evidence of COMMENT_TYPE2 variable being set.')
  assert_pattern(r'\bVariable COMMENT_TYPE3 set to.*$',
    'Variable COMMENT_TYPE3 set to ok', result.stderr,
    'No evidence of COMMENT_TYPE3 variable being set.')
  assert_pattern(r'\bVariables:[\s\n]*COMMENT_TYPE1:.*[\s\n]*' + \
    'COMMENT_TYPE2:.*[\s\n]*COMMENT_TYPE3:.*$',
    'Variables:\n\tCOMMENT_TYPE1: ok\n\tCOMMENT_TYPE2: ok\n\tCOMMENT_TYPE3: ok', \
    result.stdout, 'Set variables not listed correctly by the SET command')

def assert_pattern(pattern, result, text, message):
  """Asserts that the pattern, when applied to text, returns the expected result"""
  m = re.search(pattern, text, re.MULTILINE)
  assert m and m.group(0) == result, message

def run_impala_shell_cmd(shell_args, expect_success=True, stdin_input=None):
  """Runs the Impala shell on the commandline.

  'shell_args' is a string which represents the commandline options.
  Returns a ImpalaShellResult.
  """
  result = run_impala_shell_cmd_no_expect(shell_args, stdin_input)
  if expect_success:
    assert result.rc == 0, "Cmd %s was expected to succeed: %s" % (shell_args,
                                                                   result.stderr)
  else:
    assert result.rc != 0, "Cmd %s was expected to fail" % shell_args
  return result

def run_impala_shell_cmd_no_expect(shell_args, stdin_input=None):
  """Runs the Impala shell on the commandline.

  'shell_args' is a string which represents the commandline options.
  Returns a ImpalaShellResult.

  Does not assert based on success or failure of command.
  """
  p = ImpalaShell(shell_args)
  result = p.get_result(stdin_input)
  cmd = "%s %s" % (SHELL_CMD, shell_args)
  return result

class ImpalaShellResult(object):
  def __init__(self):
    self.rc = 0
    self.stdout = str()
    self.stderr = str()

class ImpalaShell(object):
  """A single instance of the Impala shell. The proces is started when this object is
     constructed, and then users should repeatedly call send_cmd(), followed eventually by
     get_result() to retrieve the process output."""
  def __init__(self, args=None, env=None):
    self.shell_process = self._start_new_shell_process(args, env=env)

  def pid(self):
    return self.shell_process.pid

  def send_cmd(self, cmd):
    """Send a single command to the shell. This method adds the end-of-query
       terminator (';'). """
    self.shell_process.stdin.write("%s;\n" % cmd)
    self.shell_process.stdin.flush()
    # Allow fluent-style chaining of commands
    return self

  def get_result(self, stdin_input=None):
    """Returns an ImpalaShellResult produced by the shell process on exit. After this
       method returns, send_cmd() no longer has any effect."""
    result = ImpalaShellResult()
    result.stdout, result.stderr = self.shell_process.communicate(input=stdin_input)
    # We need to close STDIN if we gave it an input, in order to send an EOF that will
    # allow the subprocess to exit.
    if stdin_input is not None: self.shell_process.stdin.close()
    result.rc = self.shell_process.returncode
    return result

  def _start_new_shell_process(self, args=None, env=None):
    """Starts a shell process and returns the process handle"""
    shell_args = SHELL_CMD
    if args is not None: shell_args = "%s %s" % (SHELL_CMD, args)
    lex = shlex.split(shell_args)
    if not env: env = os.environ
    return Popen(lex, shell=False, stdout=PIPE, stdin=PIPE, stderr=PIPE,
                 env=env)
