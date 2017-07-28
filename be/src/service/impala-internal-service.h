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

#ifndef IMPALA_SERVICE_IMPALA_INTERNAL_SERVICE_H
#define IMPALA_SERVICE_IMPALA_INTERNAL_SERVICE_H

#include "gen-cpp/ImpalaInternalService.h"
#include "gen-cpp/ImpalaInternalService_types.h"

namespace impala {

class ImpalaServer;
class QueryExecMgr;

/// Proxies Thrift RPC requests onto their implementing objects for the
/// ImpalaInternalService service.
class ImpalaInternalService : public ImpalaInternalServiceIf {
 public:
  ImpalaInternalService();
  virtual void ExecQueryFInstances(TExecQueryFInstancesResult& return_val,
      const TExecQueryFInstancesParams& params);
  virtual void CancelQueryFInstances(TCancelQueryFInstancesResult& return_val,
      const TCancelQueryFInstancesParams& params);
  virtual void ReportExecStatus(TReportExecStatusResult& return_val,
      const TReportExecStatusParams& params);
  virtual void TransmitData(TTransmitDataResult& return_val,
      const TTransmitDataParams& params);
  virtual void UpdateFilter(TUpdateFilterResult& return_val,
      const TUpdateFilterParams& params);
  virtual void PublishFilter(TPublishFilterResult& return_val,
      const TPublishFilterParams& params);

 private:
  ImpalaServer* impala_server_;
  QueryExecMgr* query_exec_mgr_;
};

}

#endif
