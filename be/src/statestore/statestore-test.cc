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

#include "testutil/gtest-util.h"
#include "testutil/in-process-servers.h"
#include "common/init.h"
#include "util/metrics.h"
#include "statestore/statestore-subscriber.h"

#include "common/names.h"

using namespace impala;

DECLARE_string(ssl_server_certificate);
DECLARE_string(ssl_private_key);
DECLARE_string(ssl_client_ca_certificate);

DECLARE_int32(webserver_port);
DECLARE_int32(state_store_port);

namespace impala {

TEST(StatestoreTest, SmokeTest) {
  // All allocations done by 'new' to avoid problems shutting down Thrift servers
  // gracefully.
  InProcessStatestore* ips = InProcessStatestore::StartWithEphemeralPorts();
  ASSERT_TRUE(ips != NULL) << "Could not start Statestore";
  // Port already in use
  InProcessStatestore* statestore_wont_start =
      new InProcessStatestore(ips->port(), ips->port() + 10);
  ASSERT_FALSE(statestore_wont_start->Start().ok());

  int subscriber_port = FindUnusedEphemeralPort(nullptr);
  ASSERT_NE(subscriber_port, -1) << "Could not find unused port";

  StatestoreSubscriber* sub_will_start = new StatestoreSubscriber("sub1",
      MakeNetworkAddress("localhost", subscriber_port),
      MakeNetworkAddress("localhost", ips->port()), new MetricGroup(""));
  ASSERT_OK(sub_will_start->Start());

  // Confirm that a subscriber trying to use an in-use port will fail to start.
  StatestoreSubscriber* sub_will_not_start = new StatestoreSubscriber("sub2",
      MakeNetworkAddress("localhost", subscriber_port),
      MakeNetworkAddress("localhost", ips->port()), new MetricGroup(""));
  ASSERT_FALSE(sub_will_not_start->Start().ok());
}

TEST(StatestoreSslTest, SmokeTest) {
  string impala_home(getenv("IMPALA_HOME"));
  stringstream server_cert;
  server_cert << impala_home << "/be/src/testutil/server-cert.pem";
  FLAGS_ssl_server_certificate = server_cert.str();
  FLAGS_ssl_client_ca_certificate = server_cert.str();
  stringstream server_key;
  server_key << impala_home << "/be/src/testutil/server-key.pem";
  FLAGS_ssl_private_key = server_key.str();

  InProcessStatestore* statestore =  InProcessStatestore::StartWithEphemeralPorts();
  if (statestore == NULL) FAIL() << "Unable to start Statestore";

  vector<int> used_ports;
  int subscriber_port = FindUnusedEphemeralPort(&used_ports);
  ASSERT_NE(subscriber_port, -1) << "Could not find unused port";

  StatestoreSubscriber* sub_will_start = new StatestoreSubscriber("smoke_sub1",
      MakeNetworkAddress("localhost", subscriber_port),
      MakeNetworkAddress("localhost", statestore->port()), new MetricGroup(""));
  ASSERT_OK(sub_will_start->Start());

  stringstream invalid_server_cert;
  invalid_server_cert << impala_home << "/be/src/testutil/invalid-server-cert.pem";
  FLAGS_ssl_client_ca_certificate = invalid_server_cert.str();
  int another_subscriber_port = FindUnusedEphemeralPort(&used_ports);
  ASSERT_NE(another_subscriber_port, -1) << "Could not find unused port";

  StatestoreSubscriber* sub_will_not_start = new StatestoreSubscriber("smoke_sub2",
      MakeNetworkAddress("localhost", another_subscriber_port),
      MakeNetworkAddress("localhost", statestore->port()), new MetricGroup(""));
  ASSERT_FALSE(sub_will_not_start->Start().ok());
}

}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  impala::InitCommonRuntime(argc, argv, false, impala::TestInfo::BE_TEST);
  int rc = RUN_ALL_TESTS();
  // IMPALA-5291: statestore services and subscribers may still be running at this point
  // and accessing global state. Exit without running global destructors to avoid
  // races with other threads when tearing down the proces.
  _exit(rc);
}
