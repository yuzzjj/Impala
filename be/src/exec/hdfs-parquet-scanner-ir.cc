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

#include "exec/hdfs-parquet-scanner.h"

#include "exec/filter-context.h"
#include "exec/parquet-scratch-tuple-batch.h"
#include "exprs/expr.h"
#include "runtime/runtime-filter.h"
#include "runtime/runtime-filter.inline.h"
#include "runtime/tuple-row.h"

using namespace impala;

int HdfsParquetScanner::ProcessScratchBatch(RowBatch* dst_batch) {
  ExprContext* const* conjunct_ctxs = &(*scanner_conjunct_ctxs_)[0];
  const int num_conjuncts = scanner_conjunct_ctxs_->size();

  // Start/end/current iterators over the output rows.
  Tuple** output_row_start =
      reinterpret_cast<Tuple**>(dst_batch->GetRow(dst_batch->num_rows()));
  Tuple** output_row_end =
      output_row_start + (dst_batch->capacity() - dst_batch->num_rows());
  Tuple** output_row = output_row_start;

  // Start/end/current iterators over the scratch tuples.
  uint8_t* scratch_tuple_start = scratch_batch_->CurrTuple();
  uint8_t* scratch_tuple_end = scratch_batch_->TupleEnd();
  uint8_t* scratch_tuple = scratch_tuple_start;
  const int tuple_size = scratch_batch_->tuple_byte_size;

  // Loop until the scratch batch is exhausted or the output batch is full.
  // Do not use batch_->AtCapacity() in this loop because it is not necessary
  // to perform the memory capacity check.
  while (scratch_tuple != scratch_tuple_end) {
    *output_row = reinterpret_cast<Tuple*>(scratch_tuple);
    scratch_tuple += tuple_size;
    // Evaluate runtime filters and conjuncts. Short-circuit the evaluation if
    // the filters/conjuncts are empty to avoid function calls.
    if (!EvalRuntimeFilters(reinterpret_cast<TupleRow*>(output_row))) {
      continue;
    }
    if (!ExecNode::EvalConjuncts(conjunct_ctxs, num_conjuncts,
        reinterpret_cast<TupleRow*>(output_row))) {
      continue;
    }
    // Row survived runtime filters and conjuncts.
    ++output_row;
    if (output_row == output_row_end) break;
  }
  scratch_batch_->tuple_idx += (scratch_tuple - scratch_tuple_start) / tuple_size;
  return output_row - output_row_start;
}

bool HdfsParquetScanner::EvalRuntimeFilter(int i, TupleRow* row) {
  LocalFilterStats* stats = &filter_stats_[i];
  const FilterContext* ctx = filter_ctxs_[i];
  ++stats->total_possible;
  if (stats->enabled && ctx->filter->HasBloomFilter()) {
    ++stats->considered;
    if (!ctx->Eval(row)) {
      ++stats->rejected;
      return false;
    }
  }
  return true;
}
