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

#ifndef IMPALA_BACKEND_CLIENT_H
#define IMPALA_BACKEND_CLIENT_H

#include "runtime/client-cache.h"
#include "testutil/fault-injection-util.h"
#include "util/runtime-profile-counters.h"

#include "gen-cpp/ImpalaInternalService.h"

namespace impala {

/// Proxy class that extends ImpalaInternalServiceClient to allow callers to time
/// the wall-clock time taken in TransmitData(), so that the time spent sending data
/// between backends in a query can be measured.
class ImpalaBackendClient : public ImpalaInternalServiceClient {
 public:
  ImpalaBackendClient(boost::shared_ptr< ::apache::thrift::protocol::TProtocol> prot)
    : ImpalaInternalServiceClient(prot), transmit_csw_(NULL) {
  }

  ImpalaBackendClient(boost::shared_ptr< ::apache::thrift::protocol::TProtocol> iprot,
      boost::shared_ptr< ::apache::thrift::protocol::TProtocol> oprot)
    : ImpalaInternalServiceClient(iprot, oprot), transmit_csw_(NULL) {
  }

/// We intentionally disable this clang warning as we intend to hide the
/// the same-named functions defined in the base class.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Woverloaded-virtual"

  void ExecQueryFInstances(TExecQueryFInstancesResult& _return,
      const TExecQueryFInstancesParams& params, bool* send_done) {
    DCHECK(!*send_done);
    FAULT_INJECTION_SEND_RPC_EXCEPTION(16);
    ImpalaInternalServiceClient::send_ExecQueryFInstances(params);
    *send_done = true;
    // Cannot inject fault on recv() side as the callers cannot handle it.
    ImpalaInternalServiceClient::recv_ExecQueryFInstances(_return);
  }

  void ReportExecStatus(TReportExecStatusResult& _return,
      const TReportExecStatusParams& params, bool* send_done) {
    DCHECK(!*send_done);
    FAULT_INJECTION_SEND_RPC_EXCEPTION(16);
    ImpalaInternalServiceClient::send_ReportExecStatus(params);
    *send_done = true;
    FAULT_INJECTION_RECV_RPC_EXCEPTION(16);
    ImpalaInternalServiceClient::recv_ReportExecStatus(_return);
  }

  void CancelQueryFInstances(TCancelQueryFInstancesResult& _return,
      const TCancelQueryFInstancesParams& params, bool* send_done) {
    DCHECK(!*send_done);
    FAULT_INJECTION_SEND_RPC_EXCEPTION(16);
    ImpalaInternalServiceClient::send_CancelQueryFInstances(params);
    *send_done = true;
    FAULT_INJECTION_RECV_RPC_EXCEPTION(16);
    ImpalaInternalServiceClient::recv_CancelQueryFInstances(_return);
  }

  void TransmitData(TTransmitDataResult& _return, const TTransmitDataParams& params,
      bool* send_done) {
    DCHECK(!*send_done);
    FAULT_INJECTION_SEND_RPC_EXCEPTION(1024);
    if (transmit_csw_ != NULL) {
      SCOPED_CONCURRENT_COUNTER(transmit_csw_);
      ImpalaInternalServiceClient::send_TransmitData(params);
    } else {
      ImpalaInternalServiceClient::send_TransmitData(params);
    }
    *send_done = true;
    FAULT_INJECTION_RECV_RPC_EXCEPTION(1024);
    ImpalaInternalServiceClient::recv_TransmitData(_return);
  }

  /// Callers of TransmitData() should provide their own counter to measure the data
  /// transmission time.
  void SetTransmitDataCounter(RuntimeProfile::ConcurrentTimerCounter* csw) {
    DCHECK(transmit_csw_ == NULL);
    transmit_csw_ = csw;
  }

  /// ImpalaBackendClient is shared by multiple queries. It's the caller's responsibility
  /// to reset the counter after data transmission.
  void ResetTransmitDataCounter() {
    transmit_csw_ = NULL;
  }

  void UpdateFilter(TUpdateFilterResult& _return, const TUpdateFilterParams& params,
      bool* send_done) {
    DCHECK(!*send_done);
    ImpalaInternalServiceClient::send_UpdateFilter(params);
    *send_done = true;
    ImpalaInternalServiceClient::recv_UpdateFilter(_return);
  }

  void PublishFilter(TPublishFilterResult& _return, const TPublishFilterParams& params,
      bool* send_done) {
    DCHECK(!*send_done);
    ImpalaInternalServiceClient::send_PublishFilter(params);
    *send_done = true;
    ImpalaInternalServiceClient::recv_PublishFilter(_return);
  }

#pragma clang diagnostic pop

 private:
  RuntimeProfile::ConcurrentTimerCounter* transmit_csw_;
};

}

#endif // IMPALA_BACKEND_CLIENT_H
