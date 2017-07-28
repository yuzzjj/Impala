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

package org.apache.impala.analysis;

import java.util.HashMap;
import java.util.List;
import java.util.Map;

import org.apache.impala.catalog.Column;
import org.apache.impala.catalog.HBaseTable;
import org.apache.impala.catalog.HdfsTable;
import org.apache.impala.catalog.KuduTable;
import org.apache.impala.catalog.Table;
import org.apache.impala.common.AnalysisException;
import org.apache.impala.thrift.TAlterTableParams;
import org.apache.impala.thrift.TAlterTableSetTblPropertiesParams;
import org.apache.impala.thrift.TAlterTableType;
import org.apache.impala.thrift.TTablePropertyType;

import com.google.common.base.Joiner;
import com.google.common.base.Preconditions;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.Maps;

/**
* Represents an ALTER TABLE SORT BY (c1, c2, ...) statement.
*/
public class AlterTableSortByStmt extends AlterTableStmt {
  // Table property key for sort.columns
  public static final String TBL_PROP_SORT_COLUMNS = "sort.columns";

  private final List<String> columns_;

  public AlterTableSortByStmt(TableName tableName, List<String> columns) {
    super(tableName);
    Preconditions.checkNotNull(columns);
    columns_ = columns;
  }

  @Override
  public TAlterTableParams toThrift() {
   TAlterTableParams params = super.toThrift();
   params.setAlter_type(TAlterTableType.SET_TBL_PROPERTIES);
   TAlterTableSetTblPropertiesParams tblPropertyParams =
       new TAlterTableSetTblPropertiesParams();
   tblPropertyParams.setTarget(TTablePropertyType.TBL_PROPERTY);
   Map<String, String> properties = Maps.newHashMap();
   properties.put(TBL_PROP_SORT_COLUMNS, Joiner.on(",").join(columns_));
   tblPropertyParams.setProperties(properties);
   params.setSet_tbl_properties_params(tblPropertyParams);
   return params;
  }

  @Override
  public void analyze(Analyzer analyzer) throws AnalysisException {
    super.analyze(analyzer);

    // Disallow setting sort columns on HBase and Kudu tables.
    Table targetTable = getTargetTable();
    if (targetTable instanceof HBaseTable) {
      throw new AnalysisException("ALTER TABLE SORT BY not supported on HBase tables.");
    }
    if (targetTable instanceof KuduTable) {
      throw new AnalysisException("ALTER TABLE SORT BY not supported on Kudu tables.");
    }

    TableDef.analyzeSortColumns(columns_, targetTable);
  }
}
