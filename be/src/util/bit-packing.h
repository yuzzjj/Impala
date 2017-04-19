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

#ifndef IMPALA_UTIL_BIT_PACKING_H
#define IMPALA_UTIL_BIT_PACKING_H

namespace impala {

#include <cstdint>

#include <utility>

/// Utilities for manipulating bit-packed values. Bit-packing is a technique for
/// compressing integer values that do not use the full range of the integer type.
/// E.g. an array of uint32_t values with range [0, 31] only uses the lower 5 bits
/// of every uint32_t value, or an array of 0/1 booleans only uses the lowest bit
/// of each integer.
///
/// Bit-packing always has a "bit width" parameter that determines the range of
/// representable unsigned values: [0, 2^bit_width - 1]. The packed representation
/// is logically the concatenatation of the lower bits of the input values (in
/// little-endian order). E.g. the values 1, 2, 3, 4 packed with bit width 4 results
/// in the two output bytes: [ 0 0 1 0 | 0 0 0 1 ] [ 0 1 0 0 | 0 0 1 1 ]
///                               2         1           4         3
///
/// Packed values can be split across words, e.g. packing 1, 17 with bit_width 5 results
/// in the two output bytes: [ 0 0 1 | 0 0 0 0 1 ] [ x x x x x x | 1 0 ]
///            lower bits of 17--^         1         next value     ^--upper bits of 17
///
/// Bit widths from 0 to 32 are supported (0 bit width means that every value is 0).
/// The batched unpacking functions operate on batches of 32 values. This batch size
/// is convenient because for every supported bit width, the end of a 32 value batch
/// falls on a byte boundary. It is also large enough to amortise loop overheads.
class BitPacking {
 public:
  /// Unpack bit-packed values with 'bit_width' from 'in' to 'out'. Keeps unpacking until
  /// either all 'in_bytes' are read or 'num_values' values are unpacked. 'out' must have
  /// enough space for 'num_values'. 0 <= 'bit_width' <= 32 and 'bit_width' <= # of bits
  /// in OutType. 'in' must point to 'in_bytes' of addressable memory.
  ///
  /// Returns a pointer to the byte after the last byte of 'in' that was read and also the
  /// number of values that were read. If the caller wants to continue reading packed
  /// values after the last one returned, it must ensure that the next value to unpack
  /// starts at a byte boundary. This is true if 'num_values' is a multiple of 32, or
  /// more generally if (bit_width * num_values) % 8 == 0.
  template <typename OutType>
  static std::pair<const uint8_t*, int64_t> UnpackValues(int bit_width,
      const uint8_t* __restrict__ in, int64_t in_bytes, int64_t num_values,
      OutType* __restrict__ out);

  /// Unpack exactly 32 values of 'bit_width' from 'in' to 'out'. 'in' must point to
  /// 'in_bytes' of addressable memory, and 'in_bytes' must be at least
  /// (32 * bit_width / 8). 'out' must have space for 32 OutType values.
  /// 0 <= 'bit_width' <= 32 and 'bit_width' <= # of bits in OutType.
  template <typename OutType>
  static const uint8_t* Unpack32Values(int bit_width, const uint8_t* __restrict__ in,
      int64_t in_bytes, OutType* __restrict__ out);

 private:
  /// Implementation of Unpack32Values() that uses 32-bit integer loads to
  /// unpack values with the given BIT_WIDTH from 'in' to 'out'.
  template <typename OutType, int BIT_WIDTH>
  static const uint8_t* Unpack32Values(
      const uint8_t* __restrict__ in, int64_t in_bytes, OutType* __restrict__ out);

  /// Function that unpacks 'num_values' values with the given BIT_WIDTH from 'in' to
  /// 'out'. 'num_values' can be at most 32. The version with 'bit_width' as an argument
  /// dispatches based on 'bit_width' to the appropriate templated implementation.
  template <typename OutType, int BIT_WIDTH>
  static const uint8_t* UnpackUpTo32Values(const uint8_t* __restrict__ in,
      int64_t in_bytes, int num_values, OutType* __restrict__ out);
  template <typename OutType>
  static const uint8_t* UnpackUpTo32Values(int bit_width, const uint8_t* __restrict__ in,
      int64_t in_bytes, int num_values, OutType* __restrict__ out);
};
}

#endif
