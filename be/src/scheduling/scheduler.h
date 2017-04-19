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

#ifndef SCHEDULING_SCHEDULER_H
#define SCHEDULING_SCHEDULER_H

#include <list>
#include <string>
#include <vector>
#include <boost/heap/binomial_heap.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/unordered_map.hpp>
#include <gtest/gtest_prod.h> // for FRIEND_TEST

#include "common/global-types.h"
#include "common/status.h"
#include "gen-cpp/PlanNodes_types.h"
#include "gen-cpp/StatestoreService_types.h"
#include "gen-cpp/Types_types.h" // for TNetworkAddress
#include "rapidjson/document.h"
#include "rpc/thrift-util.h"
#include "scheduling/backend-config.h"
#include "scheduling/query-schedule.h"
#include "scheduling/request-pool-service.h"
#include "statestore/statestore-subscriber.h"
#include "util/metrics.h"
#include "util/network-util.h"
#include "util/runtime-profile.h"
#include "util/webserver.h"

namespace impala {

namespace test {
class SchedulerWrapper;
}

/// Performs simple scheduling by matching between a list of backends configured
/// either from the statestore, or from a static list of addresses, and a list
/// of target data locations. The current set of backends is stored in backend_config_.
/// When receiving changes to the backend configuration from the statestore we will make a
/// copy of this configuration, apply the updates to the copy and atomically swap the
/// contents of the backend_config_ pointer.
///
/// TODO: Notice when there are duplicate statestore registrations (IMPALA-23)
/// TODO: Track assignments (assignment_ctx in ComputeScanRangeAssignment) per query
///       instead of per plan node?
/// TODO: Remove disable_cached_reads query option in the next compatibility-breaking
///       release (IMPALA-2963)
/// TODO: Replace the usage of shared_ptr with atomic_shared_ptr once compilers support
///       it. Alternatively consider using Kudu's rw locks.
/// TODO: Inject global dependencies into the class (for example ExecEnv::GetInstance(),
///       RNG used during scheduling, FLAGS_*)
///       to make it testable.
/// TODO: Benchmark the performance of the scheduler. The tests need to include setups
///       with:
///         - Small and large number of backends.
///         - Small and large query plans.
///         - Scheduling query plans with concurrent updates to the internal backend
///           configuration.
class Scheduler {
 public:
  static const std::string IMPALA_MEMBERSHIP_TOPIC;

  /// List of server descriptors.
  typedef std::vector<TBackendDescriptor> BackendList;

  /// Initialize with a subscription manager that we can register with for updates to the
  /// set of available backends.
  ///  - backend_id - unique identifier for this Impala backend (usually a host:port)
  ///  - backend_address - the address that this backend listens on
  Scheduler(StatestoreSubscriber* subscriber, const std::string& backend_id,
      const TNetworkAddress& backend_address, MetricGroup* metrics, Webserver* webserver,
      RequestPoolService* request_pool_service);

  /// Initialize with a list of <host:port> pairs in 'static' mode - i.e. the set of
  /// backends is fixed and will not be updated.
  Scheduler(const std::vector<TNetworkAddress>& backends, MetricGroup* metrics,
      Webserver* webserver, RequestPoolService* request_pool_service);

  /// Initialises the scheduler, acquiring all resources needed to make scheduling
  /// decisions once this method returns. Register with the subscription manager if
  /// required.
  impala::Status Init();

  /// Populates given query schedule and assigns fragments to hosts based on scan
  /// ranges in the query exec request. Submits schedule to admission control before
  /// returning.
  Status Schedule(QuerySchedule* schedule);

 private:
  /// Map from a host's IP address to the next backend to be round-robin scheduled for
  /// that host (needed for setups with multiple backends on a single host)
  typedef boost::unordered_map<IpAddr, BackendConfig::BackendList::const_iterator>
      NextBackendPerHost;

  typedef std::shared_ptr<const BackendConfig> BackendConfigPtr;

  /// Internal structure to track scan range assignments for a backend host. This struct
  /// is used as the heap element in and maintained by AddressableAssignmentHeap.
  struct BackendAssignmentInfo {
    /// The number of bytes assigned to a backend host.
    int64_t assigned_bytes;

    /// Each host gets assigned a random rank to break ties in a random but deterministic
    /// order per plan node.
    const int random_rank;

    /// IP address of the backend.
    IpAddr ip;

    /// Compare two elements of this struct. The key is (assigned_bytes, random_rank).
    bool operator>(const BackendAssignmentInfo& rhs) const {
      if (assigned_bytes != rhs.assigned_bytes) {
        return assigned_bytes > rhs.assigned_bytes;
      }
      return random_rank > rhs.random_rank;
    }
  };

  /// Heap to compute candidates for scan range assignments. Elements are of type
  /// BackendAssignmentInfo and track assignment information for each backend. By default
  /// boost implements a max-heap so we use std::greater<T> to obtain a min-heap. This
  /// will make the top() element of the heap be the backend with the lowest number of
  /// assigned bytes and the lowest random rank.
  typedef boost::heap::binomial_heap<BackendAssignmentInfo,
      boost::heap::compare<std::greater<BackendAssignmentInfo>>>
      AssignmentHeap;

  /// Map to look up handles to heap elements to modify heap element keys.
  typedef boost::unordered_map<IpAddr, AssignmentHeap::handle_type> BackendHandleMap;

  /// Class to store backend information in an addressable heap. In addition to
  /// AssignmentHeap it can be used to look up heap elements by their IP address and
  /// update their key. For each plan node we create a new heap, so they are not shared
  /// between concurrent invocations of the scheduler.
  class AddressableAssignmentHeap {
   public:
    const AssignmentHeap& backend_heap() const { return backend_heap_; }
    const BackendHandleMap& backend_handles() const { return backend_handles_; }

    void InsertOrUpdate(const IpAddr& ip, int64_t assigned_bytes, int rank);

    // Forward interface for boost::heap
    decltype(auto) size() const { return backend_heap_.size(); }
    decltype(auto) top() const { return backend_heap_.top(); }

    // Forward interface for boost::unordered_map
    decltype(auto) find(const IpAddr& ip) const { return backend_handles_.find(ip); }
    decltype(auto) end() const { return backend_handles_.end(); }

   private:
    // Heap to determine next backend.
    AssignmentHeap backend_heap_;
    // Maps backend IPs to handles in the heap.
    BackendHandleMap backend_handles_;
  };

  /// Class to store context information on assignments during scheduling. It is
  /// initialized with a copy of the global backend information and assigns a random rank
  /// to each backend to break ties in cases where multiple backends have been assigned
  /// the same number or bytes. It tracks the number of assigned bytes, which backends
  /// have already been used, etc. Objects of this class are created in
  /// ComputeScanRangeAssignment() and thus don't need to be thread safe.
  class AssignmentCtx {
   public:
    AssignmentCtx(const BackendConfig& backend_config, IntCounter* total_assignments,
        IntCounter* total_local_assignments);

    /// Among hosts in 'data_locations', select the one with the minimum number of
    /// assigned bytes. If backends have been assigned equal amounts of work and
    /// 'break_ties_by_rank' is true, then the backend rank is used to break ties.
    /// Otherwise the first backend according to their order in 'data_locations' is
    /// selected.
    const IpAddr* SelectLocalBackendHost(
        const std::vector<IpAddr>& data_locations, bool break_ties_by_rank);

    /// Select a backend host for a remote read. If there are unused backend hosts, then
    /// those will be preferred. Otherwise the one with the lowest number of assigned
    /// bytes is picked. If backends have been assigned equal amounts of work, then the
    /// backend rank is used to break ties.
    const IpAddr* SelectRemoteBackendHost();

    /// Return the next backend that has not been assigned to. This assumes that a
    /// returned backend will also be assigned to. The caller must make sure that
    /// HasUnusedBackends() is true.
    const IpAddr* GetNextUnusedBackendAndIncrement();

    /// Pick a backend in round-robin fashion from multiple backends on a single host.
    void SelectBackendOnHost(const IpAddr& backend_ip, TBackendDescriptor* backend);

    /// Build a new TScanRangeParams object and append it to the assignment list for the
    /// tuple (backend, node_id) in 'assignment'. Also, update assignment_heap_ and
    /// assignment_byte_counters_, increase the counters 'total_assignments_' and
    /// 'total_local_assignments_'. 'scan_range_locations' contains information about the
    /// scan range and its replica locations.
    void RecordScanRangeAssignment(const TBackendDescriptor& backend, PlanNodeId node_id,
        const vector<TNetworkAddress>& host_list,
        const TScanRangeLocationList& scan_range_locations,
        FragmentScanRangeAssignment* assignment);

    const BackendConfig& backend_config() const { return backend_config_; }

    /// Print the assignment and statistics to VLOG_FILE.
    void PrintAssignment(const FragmentScanRangeAssignment& assignment);

   private:
    /// A struct to track various counts of assigned bytes during scheduling.
    struct AssignmentByteCounters {
      int64_t remote_bytes = 0;
      int64_t local_bytes = 0;
      int64_t cached_bytes = 0;
    };

    /// Used to look up hostnames to IP addresses and IP addresses to backend.
    const BackendConfig& backend_config_;

    // Addressable heap to select remote backends from. Elements are ordered by the number
    // of already assigned bytes (and a random rank to break ties).
    AddressableAssignmentHeap assignment_heap_;

    /// Store a random rank per backend host to break ties between otherwise equivalent
    /// replicas (e.g., those having the same number of assigned bytes).
    boost::unordered_map<IpAddr, int> random_backend_rank_;

    // Index into random_backend_order. It points to the first unused backend and is used
    // to select unused backends and inserting them into the assignment_heap_.
    int first_unused_backend_idx_;

    /// Store a random permutation of backend hosts to select backends from.
    std::vector<IpAddr> random_backend_order_;

    /// Track round robin information per backend host.
    NextBackendPerHost next_backend_per_host_;

    /// Track number of assigned bytes that have been read from cache, locally, or
    /// remotely.
    AssignmentByteCounters assignment_byte_counters_;

    /// Pointers to the scheduler's counters.
    IntCounter* total_assignments_;
    IntCounter* total_local_assignments_;

    /// Return whether there are backends that have not been assigned a scan range.
    bool HasUnusedBackends() const;

    /// Return the rank of a backend.
    int GetBackendRank(const IpAddr& ip) const;
  };

  /// The scheduler's backend configuration. When receiving changes to the backend
  /// configuration from the statestore we will make a copy of the stored object, apply
  /// the updates to the copy and atomically swap the contents of this pointer. Each plan
  /// node creates a read-only copy of the scheduler's current backend_config_ to use
  /// during scheduling.
  BackendConfigPtr backend_config_;

  /// A backend configuration which only contains the local backend. It is used when
  /// scheduling on the coordinator.
  BackendConfig coord_only_backend_config_;

  /// Protect access to backend_config_ which might otherwise be updated asynchronously
  /// with respect to reads.
  mutable boost::mutex backend_config_lock_;

  /// Total number of scan ranges assigned to backends during the lifetime of the
  /// scheduler.
  int64_t num_assignments_;

  /// Map from unique backend id to TBackendDescriptor. Used to track the known backends
  /// from the statestore. It's important to track both the backend ID as well as the
  /// TBackendDescriptor so we know what is being removed in a given update.
  /// Locking of this map is not needed since it should only be read/modified from
  /// within the UpdateMembership() function.
  typedef boost::unordered_map<std::string, TBackendDescriptor> BackendIdMap;
  BackendIdMap current_membership_;

  /// MetricGroup subsystem access
  MetricGroup* metrics_;

  /// Webserver for /backends. Not owned by us.
  Webserver* webserver_;

  /// Pointer to a subscription manager (which we do not own) which is used to register
  /// for dynamic updates to the set of available backends. May be NULL if the set of
  /// backends is fixed.
  StatestoreSubscriber* statestore_subscriber_;

  /// Unique - across the cluster - identifier for this impala backend.
  const std::string local_backend_id_;

  /// Describe this backend, including the Impalad service address.
  TBackendDescriptor local_backend_descriptor_;

  ThriftSerializer thrift_serializer_;

  /// Locality metrics
  IntCounter* total_assignments_;
  IntCounter* total_local_assignments_;

  /// Initialization metric
  BooleanProperty* initialized_;

  /// Current number of backends
  IntGauge* num_fragment_instances_metric_;

  /// Used for user-to-pool resolution and looking up pool configurations. Not owned by
  /// us.
  RequestPoolService* request_pool_service_;

  /// Helper methods to access backend_config_ (the shared_ptr, not its contents),
  /// protecting the access with backend_config_lock_.
  BackendConfigPtr GetBackendConfig() const;
  void SetBackendConfig(const BackendConfigPtr& backend_config);

  /// Called asynchronously when an update is received from the subscription manager
  void UpdateMembership(const StatestoreSubscriber::TopicDeltaMap& incoming_topic_deltas,
      std::vector<TTopicDelta>* subscriber_topic_updates);

  /// Webserver callback that produces a list of known backends.
  /// Example output:
  /// "backends": [
  ///     "henry-metrics-pkg-cdh5.ent.cloudera.com:22000"
  ///              ],
  void BackendsUrlCallback(
      const Webserver::ArgumentMap& args, rapidjson::Document* document);

  /// Determine the pool for a user and query options via request_pool_service_.
  Status GetRequestPool(const std::string& user, const TQueryOptions& query_options,
      std::string* pool) const;

  /// Compute the assignment of scan ranges to hosts for each scan node in
  /// the schedule's TQueryExecRequest.plan_exec_info.
  /// Unpartitioned fragments are assigned to the coordinator. Populate the schedule's
  /// fragment_exec_params_ with the resulting scan range assignment.
  Status ComputeScanRangeAssignment(QuerySchedule* schedule);

  /// Process the list of scan ranges of a single plan node and compute scan range
  /// assignments (returned in 'assignment'). The result is a mapping from hosts to their
  /// assigned scan ranges per plan node.
  ///
  /// If exec_at_coord is true, all scan ranges will be assigned to the coordinator host.
  /// Otherwise the assignment is computed for each scan range as follows:
  ///
  /// Scan ranges refer to data, which is usually replicated on multiple hosts. All scan
  /// ranges where one of the replica hosts also runs an impala backend are processed
  /// first. If more than one of the replicas run an impala backend, then the 'memory
  /// distance' of each backend is considered. The concept of memory distance reflects the
  /// cost of moving data into the processing backend's main memory. Reading from cached
  /// replicas is generally considered less costly than reading from a local disk, which
  /// in turn is cheaper than reading data from a remote node. If multiple backends of the
  /// same memory distance are found, then the one with the least amount of previously
  /// assigned work is picked, thus aiming to distribute the work as evenly as possible.
  ///
  /// Finally, scan ranges are considered which do not have an impalad backend running on
  /// any of their data nodes. They will be load-balanced by assigned bytes across all
  /// backends
  ///
  /// The resulting assignment is influenced by the following query options:
  ///
  /// replica_preference:
  ///   This value is used as a minimum memory distance for all replicas. For example, by
  ///   setting this to DISK_LOCAL, all cached replicas will be treated as if they were
  ///   not cached, but local disk replicas. This can help prevent hot-spots by spreading
  ///   the assignments over more replicas. Allowed values are CACHE_LOCAL (default),
  ///   DISK_LOCAL and REMOTE.
  ///
  /// disable_cached_reads:
  ///   Setting this value to true is equivalent to setting replica_preference to
  ///   DISK_LOCAL and takes precedence over replica_preference. The default setting is
  ///   false.
  ///
  /// schedule_random_replica:
  ///   When equivalent backends with a memory distance of DISK_LOCAL are found for a scan
  ///   range (same memory distance, same amount of assigned work), then the first one
  ///   will be picked deterministically. This aims to make better use of OS buffer
  ///   caches, but can lead to performance bottlenecks on individual hosts. Setting this
  ///   option to true will randomly change the order in which equivalent replicas are
  ///   picked for different plan nodes. This helps to compute a more even assignment,
  ///   with the downside being an increased memory usage for OS buffer caches. The
  ///   default setting is false. Selection between equivalent replicas with memory
  ///   distance of CACHE_LOCAL or REMOTE happens based on a random order.
  ///
  /// The method takes the following parameters:
  ///
  /// backend_config:          Backend configuration to use for scheduling.
  /// node_id:                 ID of the plan node.
  /// node_replica_preference: Query hint equivalent to replica_preference.
  /// node_random_replica:     Query hint equivalent to schedule_random_replica.
  /// locations:               List of scan ranges to be assigned to backends.
  /// host_list:               List of hosts, into which 'locations' will index.
  /// exec_at_coord:           Whether to schedule all scan ranges on the coordinator.
  /// query_options:           Query options for the current query.
  /// timer:                   Tracks execution time of ComputeScanRangeAssignment.
  /// assignment:              Output parameter, to which new assignments will be added.
  Status ComputeScanRangeAssignment(const BackendConfig& backend_config,
      PlanNodeId node_id, const TReplicaPreference::type* node_replica_preference,
      bool node_random_replica, const std::vector<TScanRangeLocationList>& locations,
      const std::vector<TNetworkAddress>& host_list, bool exec_at_coord,
      const TQueryOptions& query_options, RuntimeProfile::Counter* timer,
      FragmentScanRangeAssignment* assignment);

  /// Compute the FragmentExecParams for all plans in the schedule's
  /// TQueryExecRequest.plan_exec_info.
  /// This includes the routing information (destinations, per_exch_num_senders,
  /// sender_id)
  void ComputeFragmentExecParams(QuerySchedule* schedule);

  /// Recursively create FInstanceExecParams and set per_node_scan_ranges for
  /// fragment_params and its input fragments via a depth-first traversal.
  /// All fragments are part of plan_exec_info.
  void ComputeFragmentExecParams(const TPlanExecInfo& plan_exec_info,
      FragmentExecParams* fragment_params, QuerySchedule* schedule);

  /// Create instances of the fragment corresponding to fragment_params, which contains
  /// a Union node.
  /// UnionNodes are special because they can consume multiple partitioned inputs,
  /// as well as execute multiple scans in the same fragment.
  /// Fragments containing a UnionNode are executed on the union of hosts of all
  /// scans in the fragment as well as the hosts of all its input fragments (s.t.
  /// a UnionNode with partitioned joins or grouping aggregates as children runs on
  /// at least as many hosts as the input to those children).
  /// TODO: is this really necessary? If not, revise.
  void CreateUnionInstances(FragmentExecParams* fragment_params, QuerySchedule* schedule);

  /// Create instances of the fragment corresponding to fragment_params to run on the
  /// selected replica hosts of the scan ranges of the node with id scan_id.
  /// The maximum number of instances is the value of query option mt_dop.
  /// For HDFS, this attempts to load balance among instances by computing the average
  /// number of bytes per instances and then in a single pass assigning scan ranges to
  /// each
  /// instances to roughly meet that average.
  /// For all other storage mgrs, it load-balances the number of splits per instance.
  void CreateScanInstances(
      PlanNodeId scan_id, FragmentExecParams* fragment_params, QuerySchedule* schedule);

  /// For each instance of fragment_params's input fragment, create a collocated
  /// instance for fragment_params's fragment.
  /// Expects that fragment_params only has a single input fragment.
  void CreateCollocatedInstances(
      FragmentExecParams* fragment_params, QuerySchedule* schedule);

  /// Return the id of the leftmost node of any of the given types in 'plan', or
  /// INVALID_PLAN_NODE_ID if no such node present.
  PlanNodeId FindLeftmostNode(
      const TPlan& plan, const std::vector<TPlanNodeType::type>& types);
  /// Same for scan nodes.
  PlanNodeId FindLeftmostScan(const TPlan& plan);

  /// Add all hosts the given scan is executed on to scan_hosts.
  void GetScanHosts(TPlanNodeId scan_id, const FragmentExecParams& params,
      std::vector<TNetworkAddress>* scan_hosts);

  /// Return true if 'plan' contains a node of the given type.
  bool ContainsNode(const TPlan& plan, TPlanNodeType::type type);

  /// Return all ids of nodes in 'plan' of any of the given types.
  void FindNodes(const TPlan& plan, const std::vector<TPlanNodeType::type>& types,
      std::vector<TPlanNodeId>* results);

  friend class impala::test::SchedulerWrapper;
  FRIEND_TEST(SimpleAssignmentTest, ComputeAssignmentDeterministicNonCached);
  FRIEND_TEST(SimpleAssignmentTest, ComputeAssignmentRandomNonCached);
  FRIEND_TEST(SimpleAssignmentTest, ComputeAssignmentRandomDiskLocal);
  FRIEND_TEST(SimpleAssignmentTest, ComputeAssignmentRandomRemote);
};

}

#endif
