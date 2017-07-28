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

#ifndef STATESTORE_STATESTORE_SERVICE_CLIENT_WRAPPER_H
#define STATESTORE_STATESTORE_SERVICE_CLIENT_WRAPPER_H

#include "gen-cpp/StatestoreService.h"

namespace impala {

class StatestoreServiceClientWrapper : public StatestoreServiceClient {
 public:
  StatestoreServiceClientWrapper(
      boost::shared_ptr<::apache::thrift::protocol::TProtocol> prot)
    : StatestoreServiceClient(prot) {
  }

  StatestoreServiceClientWrapper(
      boost::shared_ptr<::apache::thrift::protocol::TProtocol> iprot,
      boost::shared_ptr<::apache::thrift::protocol::TProtocol> oprot)
    : StatestoreServiceClient(iprot, oprot) {
  }

/// We intentionally disable this clang warning as we intend to hide the
/// the same-named functions defined in the base class.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Woverloaded-virtual"

  void RegisterSubscriber(TRegisterSubscriberResponse& _return,
      const TRegisterSubscriberRequest& params, bool* send_done) {
    DCHECK(!*send_done);
    send_RegisterSubscriber(params);
    *send_done = true;
    recv_RegisterSubscriber(_return);
  }

#pragma clang diagnostic pop
};

}
#endif
