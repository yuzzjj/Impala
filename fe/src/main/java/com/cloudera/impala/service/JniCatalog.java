// Copyright 2012 Cloudera Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package com.cloudera.impala.service;

import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

import org.apache.thrift.TException;
import org.apache.thrift.TSerializer;
import org.apache.thrift.protocol.TBinaryProtocol;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.cloudera.impala.catalog.CatalogException;
import com.cloudera.impala.catalog.CatalogServiceCatalog;
import com.cloudera.impala.catalog.Function;
import com.cloudera.impala.common.ImpalaException;
import com.cloudera.impala.common.InternalException;
import com.cloudera.impala.common.JniUtil;
import com.cloudera.impala.thrift.TCatalogObject;
import com.cloudera.impala.thrift.TDdlExecRequest;
import com.cloudera.impala.thrift.TGetAllCatalogObjectsResponse;
import com.cloudera.impala.thrift.TGetDbsParams;
import com.cloudera.impala.thrift.TGetDbsResult;
import com.cloudera.impala.thrift.TGetTablesParams;
import com.cloudera.impala.thrift.TGetTablesResult;
import com.cloudera.impala.thrift.TLogLevel;
import com.cloudera.impala.thrift.TPrioritizeLoadRequest;
import com.cloudera.impala.thrift.TResetMetadataRequest;
import com.cloudera.impala.thrift.TUniqueId;
import com.cloudera.impala.thrift.TUpdateCatalogRequest;
import com.cloudera.impala.util.GlogAppender;
import com.google.common.base.Preconditions;

/**
 * JNI-callable interface for the CatalogService. The main point is to serialize
 * and de-serialize thrift structures between C and Java parts of the CatalogService.
 */
public class JniCatalog {
  private final static Logger LOG = LoggerFactory.getLogger(JniCatalog.class);
  private final static TBinaryProtocol.Factory protocolFactory_ =
      new TBinaryProtocol.Factory();
  private final CatalogServiceCatalog catalog_;
  private final CatalogOpExecutor catalogOpExecutor_;

  // A unique identifier for this instance of the Catalog Service.
  private static final TUniqueId catalogServiceId_ = generateId();

  private static TUniqueId generateId() {
    UUID uuid = UUID.randomUUID();
    return new TUniqueId(uuid.getMostSignificantBits(), uuid.getLeastSignificantBits());
  }

  public JniCatalog(boolean loadInBackground, int numMetadataLoadingThreads,
      int impalaLogLevel, int otherLogLevel) throws InternalException {
    Preconditions.checkArgument(numMetadataLoadingThreads > 0);
    // This trick saves having to pass a TLogLevel enum, which is an object and more
    // complex to pass through JNI.
    GlogAppender.Install(TLogLevel.values()[impalaLogLevel],
        TLogLevel.values()[otherLogLevel]);

    catalog_ = new CatalogServiceCatalog(loadInBackground,
        numMetadataLoadingThreads, getServiceId());
    try {
      catalog_.reset();
    } catch (CatalogException e) {
      LOG.error("Error initialializing Catalog. Please run 'invalidate metadata'", e);
    }
    catalogOpExecutor_ = new CatalogOpExecutor(catalog_);
  }

  public static TUniqueId getServiceId() { return catalogServiceId_; }

  /**
   * Gets all catalog objects
   */
  public byte[] getCatalogObjects(long from_version) throws ImpalaException, TException {
    TGetAllCatalogObjectsResponse resp =
        catalog_.getCatalogObjects(from_version);
    TSerializer serializer = new TSerializer(protocolFactory_);
    return serializer.serialize(resp);
  }

  /**
   * Executes the given DDL request and returns the result.
   */
  public byte[] execDdl(byte[] thriftDdlExecReq) throws ImpalaException {
    TDdlExecRequest params = new TDdlExecRequest();
    JniUtil.deserializeThrift(protocolFactory_, params, thriftDdlExecReq);
    TSerializer serializer = new TSerializer(protocolFactory_);
    try {
      return serializer.serialize(catalogOpExecutor_.execDdlRequest(params));
    } catch (TException e) {
      throw new InternalException(e.getMessage());
    }
  }

  /**
   * Execute a reset metadata statement. See comment in CatalogOpExecutor.java.
   */
  public byte[] resetMetadata(byte[] thriftResetMetadataReq)
      throws ImpalaException, TException {
    TResetMetadataRequest req = new TResetMetadataRequest();
    JniUtil.deserializeThrift(protocolFactory_, req, thriftResetMetadataReq);
    TSerializer serializer = new TSerializer(protocolFactory_);
    return serializer.serialize(catalogOpExecutor_.execResetMetadata(req));
  }

  /**
   * Returns a list of table names matching an optional pattern.
   * The argument is a serialized TGetTablesParams object.
   * The return type is a serialized TGetTablesResult object.
   */
  public byte[] getDbNames(byte[] thriftGetTablesParams) throws ImpalaException,
      TException {
    TGetDbsParams params = new TGetDbsParams();
    JniUtil.deserializeThrift(protocolFactory_, params, thriftGetTablesParams);
    TGetDbsResult result = new TGetDbsResult();
    result.setDbs(catalog_.getDbNames(null));
    TSerializer serializer = new TSerializer(protocolFactory_);
    return serializer.serialize(result);
  }

  /**
   * Returns a list of table names matching an optional pattern.
   * The argument is a serialized TGetTablesParams object.
   * The return type is a serialized TGetTablesResult object.
   */
  public byte[] getTableNames(byte[] thriftGetTablesParams) throws ImpalaException,
      TException {
    TGetTablesParams params = new TGetTablesParams();
    JniUtil.deserializeThrift(protocolFactory_, params, thriftGetTablesParams);
    List<String> tables = catalog_.getTableNames(params.db, params.pattern);
    TGetTablesResult result = new TGetTablesResult();
    result.setTables(tables);
    TSerializer serializer = new TSerializer(protocolFactory_);
    return serializer.serialize(result);
  }

  /**
   * Gets the thrift representation of a catalog object.
   */
  public byte[] getCatalogObject(byte[] thriftParams) throws ImpalaException,
      TException {
    TCatalogObject objectDescription = new TCatalogObject();
    JniUtil.deserializeThrift(protocolFactory_, objectDescription, thriftParams);
    TSerializer serializer = new TSerializer(protocolFactory_);
    return serializer.serialize(catalog_.getTCatalogObject(objectDescription));
  }

  public void prioritizeLoad(byte[] thriftLoadReq) throws ImpalaException,
      TException  {
    TPrioritizeLoadRequest request = new TPrioritizeLoadRequest();
    JniUtil.deserializeThrift(protocolFactory_, request, thriftLoadReq);
    catalog_.prioritizeLoad(request.getObject_descs());
  }

  /**
   * Process any updates to the metastore required after a query executes.
   * The argument is a serialized TCatalogUpdate.
   */
  public byte[] updateCatalog(byte[] thriftUpdateCatalog) throws ImpalaException,
      TException  {
    TUpdateCatalogRequest request = new TUpdateCatalogRequest();
    JniUtil.deserializeThrift(protocolFactory_, request, thriftUpdateCatalog);
    TSerializer serializer = new TSerializer(protocolFactory_);
    return serializer.serialize(catalogOpExecutor_.updateCatalog(request));
  }
}
