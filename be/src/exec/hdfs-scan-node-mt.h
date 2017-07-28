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


#ifndef IMPALA_EXEC_HDFS_SCAN_NODE_MT_H_
#define IMPALA_EXEC_HDFS_SCAN_NODE_MT_H_

#include <boost/scoped_ptr.hpp>

#include "exec/hdfs-scanner.h"
#include "exec/hdfs-scan-node-base.h"
#include "exec/scanner-context.h"

namespace impala {

class DescriptorTbl;
class ObjectPool;
class RuntimeState;
class RowBatch;
class TPlanNode;

/// Scan node that materializes tuples, evaluates conjuncts and runtime filters
/// in the thread calling GetNext(). Uses the HdfsScanner::GetNext() interface.
class HdfsScanNodeMt : public HdfsScanNodeBase {
 public:
  HdfsScanNodeMt(ObjectPool* pool, const TPlanNode& tnode, const DescriptorTbl& descs);
  ~HdfsScanNodeMt();

  virtual Status Prepare(RuntimeState* state) WARN_UNUSED_RESULT;
  virtual Status Open(RuntimeState* state) WARN_UNUSED_RESULT;
  virtual Status GetNext(RuntimeState* state, RowBatch* row_batch, bool* eos)
      WARN_UNUSED_RESULT;
  virtual void Close(RuntimeState* state);

  virtual bool HasRowBatchQueue() const { return false; }

 private:
  /// Current scan range and corresponding scanner.
  DiskIoMgr::ScanRange* scan_range_;
  boost::scoped_ptr<ScannerContext> scanner_ctx_;
  boost::scoped_ptr<HdfsScanner> scanner_;
};

}

#endif
