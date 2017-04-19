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

#include "exec/topn-node.h"

using namespace impala;

void TopNNode::InsertBatch(RowBatch* batch) {
  for (int i = 0; i < batch->num_rows(); ++i) {
    InsertTupleRow(batch->GetRow(i));
  }
}

// Insert if either not at the limit or it's a new TopN tuple_row
void TopNNode::InsertTupleRow(TupleRow* input_row) {
  Tuple* insert_tuple = NULL;

  if (priority_queue_->size() < limit_ + offset_) {
    insert_tuple = reinterpret_cast<Tuple*>(
        tuple_pool_->Allocate(materialized_tuple_desc_->byte_size()));
    insert_tuple->MaterializeExprs<false, false>(input_row, *materialized_tuple_desc_,
        sort_exec_exprs_.sort_tuple_slot_expr_ctxs(), tuple_pool_.get());
  } else {
    DCHECK(!priority_queue_->empty());
    Tuple* top_tuple = priority_queue_->top();
    tmp_tuple_->MaterializeExprs<false, true>(input_row, *materialized_tuple_desc_,
        sort_exec_exprs_.sort_tuple_slot_expr_ctxs(), NULL);
    if (tuple_row_less_than_->Less(tmp_tuple_, top_tuple)) {
      // TODO: DeepCopy() will allocate new buffers for the string data. This needs
      // to be fixed to use a freelist
      tmp_tuple_->DeepCopy(top_tuple, *materialized_tuple_desc_, tuple_pool_.get());
      insert_tuple = top_tuple;
      priority_queue_->pop();
    }
  }

  if (insert_tuple != NULL) priority_queue_->push(insert_tuple);
}
