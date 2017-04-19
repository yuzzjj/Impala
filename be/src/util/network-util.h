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

#include "common/status.h"
#include "gen-cpp/StatestoreService_types.h"
#include "gen-cpp/Types_types.h"
#include <vector>

namespace impala {

/// Type to store hostnames, which can be rfc1123 hostnames or IPv4 addresses.
typedef std::string Hostname;

/// Type to store IPv4 addresses.
typedef std::string IpAddr;

/// Looks up all IP addresses associated with a given hostname and returns one of them via
/// 'address'. If the IP addresses of a host don't change, then subsequent calls will
/// always return the same address. Returns an error status if any system call failed,
/// otherwise OK. Even if OK is returned, addresses may still be of zero length.
Status HostnameToIpAddr(const Hostname& hostname, IpAddr* ip);

/// Finds the first non-localhost IP address in the given list. Returns
/// true if such an address was found, false otherwise.
bool FindFirstNonLocalhost(const std::vector<std::string>& addresses, std::string* addr);

/// Sets the output argument to the system defined hostname.
/// Returns OK if a hostname can be found, false otherwise.
Status GetHostname(std::string* hostname);

/// Utility method because Thrift does not supply useful constructors
TNetworkAddress MakeNetworkAddress(const std::string& hostname, int port);

/// Utility method to parse the given string into a network address.
/// Accepted format: "host:port" or "host". For the latter format the port is set to zero.
/// If the given string address is malformed, returns a network address with an empty
/// hostname and a port of 0.
TNetworkAddress MakeNetworkAddress(const std::string& address);

/// Utility method because Thrift does not supply useful constructors
TBackendDescriptor MakeBackendDescriptor(const Hostname& hostname, const IpAddr& ip,
    int port);

/// Returns true if the ip address parameter is the wildcard interface (0.0.0.0)
bool IsWildcardAddress(const std::string& ipaddress);

/// Utility method to print address as address:port
std::string TNetworkAddressToString(const TNetworkAddress& address);

/// Prints a hostport as ipaddress:port
std::ostream& operator<<(std::ostream& out, const TNetworkAddress& hostport);

/// Returns a ephemeral port that is unused when this function executes. Returns -1 on an
/// error or if a free ephemeral port can't be found after 10 tries.
int FindUnusedEphemeralPort();
}
