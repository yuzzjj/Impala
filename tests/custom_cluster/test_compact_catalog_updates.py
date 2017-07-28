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
# Test Catalog behavior when --compact_catalog_topic is true.

import pytest

from tests.common.custom_cluster_test_suite import CustomClusterTestSuite

class TestCompactCatalogUpdates(CustomClusterTestSuite):
  @pytest.mark.execute_serially
  @CustomClusterTestSuite.with_args(impalad_args="--compact_catalog_topic=true",
      catalogd_args="--compact_catalog_topic=true")
  def test_compact_catalog_topic_updates(self):
    """Start Impala cluster with compact catalog update topics enabled and run a set
    of smoke tests to verify that catalog updates are received properly."""

    try:
      # Check that initial catalop update topic has been received
      impalad1 = self.cluster.impalads[0]
      assert impalad1.service.get_metric_value("catalog.num-tables") > 0
      impalad2 = self.cluster.impalads[1]
      assert impalad2.service.get_metric_value("catalog.num-tables") > 0

      client1 = impalad1.service.create_beeswax_client()
      client2 = impalad2.service.create_beeswax_client()
      query_options = {"sync_ddl" : 1}
      self.execute_query_expect_success(client1, "refresh functional.alltypes",
          query_options)
      result = client2.execute("select count(*) from functional.alltypes")
      assert result.data[0] == "7300"

      self.execute_query_expect_success(client1, "invalidate metadata", query_options)
      self.execute_query_expect_success(client2, "show databases")
      assert impalad1.service.get_metric_value("catalog.num-databases") > 0
      assert impalad2.service.get_metric_value("catalog.num-databases") > 0
    finally:
      client1.close()
      client2.close()

