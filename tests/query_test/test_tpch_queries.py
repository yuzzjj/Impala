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

# Functional tests running the TPCH workload.
import pytest

from tests.common.impala_test_suite import ImpalaTestSuite
from tests.common.test_dimensions import create_single_exec_option_dimension

class TestTpchQuery(ImpalaTestSuite):
  @classmethod
  def get_workload(self):
    return 'tpch'

  @classmethod
  def add_test_dimensions(cls):
    super(TestTpchQuery, cls).add_test_dimensions()
    cls.ImpalaTestMatrix.add_dimension(create_single_exec_option_dimension())

    # The tpch tests take a long time to execute so restrict the combinations they
    # execute over.
    # TODO: the planner tests are based on text and need this.
    if cls.exploration_strategy() == 'core':
      cls.ImpalaTestMatrix.add_constraint(lambda v:\
          v.get_value('table_format').file_format in ['text', 'parquet', 'kudu'])

  def get_test_file_prefix(self, vector):
    if vector.get_value('table_format').file_format in ['kudu']:
      return 'tpch-kudu-q'
    else:
      return 'tpch-q'

  def idfn(val):
    return "TPC-H: Q{0}".format(val)

  @pytest.mark.parametrize("query", xrange(1, 23), ids=idfn)
  def test_tpch(self, vector, query):
    self.run_test_case('{0}{1}'.format(self.get_test_file_prefix(vector), query),
        vector)

