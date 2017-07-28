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

#include "service/client-request-state.h"
#include "util/auth-util.h"
#include "gen-cpp/ImpalaInternalService_types.h"

using namespace std;

namespace impala {

  const string& GetEffectiveUser(const TSessionState& session) {
    if (session.__isset.delegated_user && !session.delegated_user.empty()) {
      return session.delegated_user;
    }
    return session.connected_user;
  }

  const std::string& GetEffectiveUser(const ImpalaServer::SessionState& session) {
    return session.do_as_user.empty() ? session.connected_user : session.do_as_user;
  }

  Status CheckProfileAccess(const string& user, const string& effective_user,
      bool has_access) {
    if (user.empty() || (user == effective_user && has_access)) return Status::OK();
    stringstream ss;
    ss << "User " << user << " is not authorized to access the runtime profile or "
       << "execution summary.";
    return Status(ss.str());
  }

}
