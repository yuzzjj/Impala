#!/bin/bash
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
set -euo pipefail
trap 'echo Error in $0 at line $LINENO: $(cd "'$PWD'" && awk "NR == $LINENO" $0)' ERR

: ${REMOTE_LOAD:=}

# Create a cache pool and encryption keys for tests

PREVIOUS_PRINCIPAL=""
CACHEADMIN_ARGS=""

# If we're kerberized, we need to become hdfs for this:
if ${CLUSTER_DIR}/admin is_kerberized; then
  if [[ -n "${REMOTE_LOAD:-}" ]]; then
    echo "REMOTE_LOAD: $REMOTE_LOAD"
    echo "Remote cluster testing is not supported with Kerberos"
    exit 1
  else
    PREVIOUS_PRINCIPAL=`klist | grep ^Default | awk '{print $3}'`
    PREVIOUS_USER=`echo ${PREVIOUS_PRINCIPAL} | awk -F/ '{print $1}'`
    CACHEADMIN_ARGS="-group supergroup -owner ${PREVIOUS_USER}"
    kinit -k -t ${KRB5_KTNAME} ${MINIKDC_PRINC_HDFS}
  fi
fi

# TODO: Investigate how to setup encryption keys for running HDFS encryption tests
# against a remote cluster, rather than the local mini-cluster (i.e., when REMOTE_LOAD
# is true. See: IMPALA-4344)

if [[ $TARGET_FILESYSTEM == hdfs && -z "$REMOTE_LOAD" ]]; then  # Otherwise assume KMS isn't setup.
  # Create encryption keys for HDFS encryption tests. Keys are stored by the KMS.
  EXISTING_KEYS=$(hadoop key list)
  for KEY in testkey{1,2}; do
    if grep $KEY <<< $EXISTING_KEYS &>/dev/null; then
      hadoop key delete $KEY -f
    fi
    hadoop key create $KEY
  done
fi

if [[ -n "${REMOTE_LOAD:-}" ]]; then
  # Create test cache pool if HADOOP_USER_NAME has a non-zero length
  if [[ -n "${HADOOP_USER_NAME:-}" ]]; then
      CACHEADMIN_ARGS="${CACHEADMIN_ARGS} -owner ${USER}"
  fi
fi

if hdfs cacheadmin -listPools testPool | grep testPool &>/dev/null; then
  hdfs cacheadmin -removePool testPool
fi
hdfs cacheadmin -addPool testPool ${CACHEADMIN_ARGS}

# Back to ourselves:
if [ "${PREVIOUS_PRINCIPAL}" != "" ]; then
  kinit -k -t ${KRB5_KTNAME} ${PREVIOUS_PRINCIPAL}
fi
