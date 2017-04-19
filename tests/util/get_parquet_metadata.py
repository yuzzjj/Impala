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

import os
import struct

from datetime import date, datetime, time, timedelta
from decimal import Decimal
from parquet.ttypes import FileMetaData, Type
from thrift.protocol import TCompactProtocol
from thrift.transport import TTransport

PARQUET_VERSION_NUMBER = 'PAR1'


def julian_day_to_date(julian_day):
  """Converts a julian day number into a Gregorian date. The reference date is picked
  arbitrarily and can be validated with an online converter like
  http://aa.usno.navy.mil/jdconverter?ID=AA&jd=2457755
  """
  return date(2017, 01, 01) + timedelta(julian_day - 2457755)


def nanos_to_time(nanos):
  """Converts nanoseconds to time of day."""
  micros = nanos // 1000  # integer division
  seconds, micros = divmod(micros, 10**6)
  minutes, seconds = divmod(seconds, 60)
  hours, minutes = divmod(minutes, 60)
  return time(hours, minutes, seconds, micros)


def parse_boolean(s):
  """Parses a single boolean value from a single byte"""
  return struct.unpack('<?', s)[0]


def parse_int32(s):
  """Reinterprets the string 's' as a signed 4-byte integer."""
  return struct.unpack('<i', s)[0]


def parse_int64(s):
  """Reinterprets the string 's' as a signed 8-byte integer."""
  return struct.unpack('<q', s)[0]


def parse_float(s):
  """Reinterprets the string 's' as an IEEE single precision float."""
  return struct.unpack('<f', s)[0]


def parse_double(s):
  """Reinterprets the string 's' as an IEEE double precision float."""
  return struct.unpack('<d', s)[0]


def decode_timestamp(s):
  """Reinterprets the string 's' as a 12-byte timestamp as written by Impala and decode it
  into a datetime object."""
  # Impala writes timestamps as 12-byte values. The first 8 byte store a
  # boost::posix_time::time_duration, which is the time within the current day in
  # nanoseconds stored as int64. The last 4 bytes store a boost::gregorian::date,
  # which is the Julian date, stored as utin32.
  day_nanos, julian_day = struct.unpack('<qI', s)
  return datetime.combine(julian_day_to_date(julian_day), nanos_to_time(day_nanos))


def decode_decimal(schema, value):
  """Decodes 'value' into a decimal by interpreting its contents according to 'schema'."""
  assert len(value) > 0
  assert schema.type_length == len(value)
  assert schema.type == Type.FIXED_LEN_BYTE_ARRAY

  numeric = Decimal(reduce(lambda x, y: x * 256 + y, map(ord, value)))

  # Compute two's complement for negative values.
  if (ord(value[0]) > 127):
    bit_width = 8 * len(value)
    numeric = numeric - (2 ** bit_width)

  return numeric / 10 ** schema.scale


def decode_stats_value(schema, value):
  """Decodes 'value' according to 'schema. It expects 'value' to be plain encoded. For
  BOOLEAN values, only the least significant bit is parsed and returned. Binary arrays are
  expected to be stored as such, without a preceding length."""
  column_type = schema.type
  if column_type == Type.BOOLEAN:
    return parse_boolean(value)
  elif column_type == Type.INT32:
    return parse_int32(value)
  elif column_type == Type.INT64:
    return parse_int64(value)
  elif column_type == Type.INT96:
    # Impala uses INT96 to store timestamps
    return decode_timestamp(value)
  elif column_type == Type.FLOAT:
    return parse_float(value)
  elif column_type == Type.DOUBLE:
    return parse_double(value)
  elif column_type == Type.BYTE_ARRAY:
    # In parquet::Statistics, strings are stored as is.
    return value
  elif column_type == Type.FIXED_LEN_BYTE_ARRAY:
    return decode_decimal(schema, value)
  assert False
  return None


def get_parquet_metadata(filename):
  """Returns a FileMetaData as defined in parquet.thrift. 'filename' must be a local
  file path."""
  file_size = os.path.getsize(filename)
  with open(filename) as f:
    # Check file starts and ends with magic bytes
    start_magic = f.read(len(PARQUET_VERSION_NUMBER))
    assert start_magic == PARQUET_VERSION_NUMBER

    f.seek(file_size - len(PARQUET_VERSION_NUMBER))
    end_magic = f.read(len(PARQUET_VERSION_NUMBER))
    assert end_magic == PARQUET_VERSION_NUMBER

    # Read metadata length
    f.seek(file_size - len(PARQUET_VERSION_NUMBER) - 4)
    metadata_len = parse_int32(f.read(4))

    # Read metadata
    f.seek(file_size - len(PARQUET_VERSION_NUMBER) - 4 - metadata_len)
    serialized_metadata = f.read(metadata_len)

    # Deserialize metadata
    transport = TTransport.TMemoryBuffer(serialized_metadata)
    protocol = TCompactProtocol.TCompactProtocol(transport)
    metadata = FileMetaData()
    metadata.read(protocol)
    return metadata
