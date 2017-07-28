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


#ifndef IMPALA_UTIL_AUTH_UTIL_H
#define IMPALA_UTIL_AUTH_UTIL_H

#include <string>
#include "service/impala-server.h"

namespace impala {

class TSessionState;

/// Returns a reference to the "effective user" from the specified session. Queries
/// are run and authorized on behalf of the effective user. When a delegated_user is
/// specified (is not empty), the effective user is the delegated_user. This is because
/// the connected_user is acting as a "proxy user" for the delegated_user. When
/// delegated_user is empty, the effective user is the connected user.
const std::string& GetEffectiveUser(const TSessionState& session);

/// Same behavior as the function above with different input parameter type.
const std::string& GetEffectiveUser(const ImpalaServer::SessionState& session);

/// Checks if 'user' can access the runtime profile or execution summary of a
/// statement by comparing 'user' with the user that run the statement, 'effective_user',
/// and checking if 'effective_user' is authorized to access the profile, as indicated by
/// 'has_access'. An error Status is returned if 'user' is not authorized to
/// access the runtime profile or execution summary.
Status CheckProfileAccess(const std::string& user, const std::string& effective_user,
    bool has_access);

} // namespace impala
#endif
