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


#ifndef IMPALA_UTIL_RUNTIME_PROFILE_COUNTERS_H
#define IMPALA_UTIL_RUNTIME_PROFILE_COUNTERS_H

#include <boost/scoped_ptr.hpp>
#include <boost/unordered_map.hpp>
#include <sys/time.h>
#include <sys/resource.h>

#include "common/atomic.h"
#include "common/logging.h"
#include "util/runtime-profile.h"
#include "util/stopwatch.h"
#include "util/streaming-sampler.h"

namespace impala {

/// Define macros for updating counters.  The macros make it very easy to disable
/// all counters at compile time.  Set this to 0 to remove counters.  This is useful
/// to do to make sure the counters aren't affecting the system.
#define ENABLE_COUNTERS 1

/// Some macro magic to generate unique ids using __COUNTER__
#define CONCAT_IMPL(x, y) x##y
#define MACRO_CONCAT(x, y) CONCAT_IMPL(x, y)

#if ENABLE_COUNTERS
  #define ADD_COUNTER(profile, name, unit) (profile)->AddCounter(name, unit)
  #define ADD_TIME_SERIES_COUNTER(profile, name, src_counter) \
      (profile)->AddTimeSeriesCounter(name, src_counter)
  #define ADD_TIMER(profile, name) (profile)->AddCounter(name, TUnit::TIME_NS)
  #define ADD_SUMMARY_STATS_TIMER(profile, name) \
      (profile)->AddSummaryStatsCounter(name, TUnit::TIME_NS)
  #define ADD_SUMMARY_STATS_COUNTER(profile, name, unit) \
      (profile)->AddSummaryStatsCounter(name, unit)
  #define ADD_CHILD_TIMER(profile, name, parent) \
      (profile)->AddCounter(name, TUnit::TIME_NS, parent)
  #define SCOPED_TIMER(c) \
      ScopedTimer<MonotonicStopWatch> MACRO_CONCAT(SCOPED_TIMER, __COUNTER__)(c)
  #define CANCEL_SAFE_SCOPED_TIMER(c, is_cancelled) \
      ScopedTimer<MonotonicStopWatch> MACRO_CONCAT(SCOPED_TIMER, __COUNTER__)(c, is_cancelled)
  #define COUNTER_ADD(c, v) (c)->Add(v)
  #define COUNTER_SET(c, v) (c)->Set(v)
  #define ADD_THREAD_COUNTERS(profile, prefix) (profile)->AddThreadCounters(prefix)
  #define SCOPED_THREAD_COUNTER_MEASUREMENT(c) \
    ThreadCounterMeasurement \
      MACRO_CONCAT(SCOPED_THREAD_COUNTER_MEASUREMENT, __COUNTER__)(c)
  #define SCOPED_CONCURRENT_COUNTER(c) \
    ScopedStopWatch<RuntimeProfile::ConcurrentTimerCounter> \
      MACRO_CONCAT(SCOPED_CONCURRENT_COUNTER, __COUNTER__)(c)
#else
  #define ADD_COUNTER(profile, name, unit) NULL
  #define ADD_TIME_SERIES_COUNTER(profile, name, src_counter) NULL
  #define ADD_TIMER(profile, name) NULL
  #define ADD_SUMMARY_STATS_TIMER(profile, name) NULL
  #define ADD_CHILD_TIMER(profile, name, parent) NULL
  #define SCOPED_TIMER(c)
  #define CANCEL_SAFE_SCOPED_TIMER(c)
  #define COUNTER_ADD(c, v)
  #define COUNTER_SET(c, v)
  #define ADD_THREAD_COUNTERS(profile, prefix) NULL
  #define SCOPED_THREAD_COUNTER_MEASUREMENT(c)
  #define SCOPED_CONCURRENT_COUNTER(c)
#endif

/// A counter that keeps track of the highest value seen (reporting that
/// as value()) and the current value.
class RuntimeProfile::HighWaterMarkCounter : public RuntimeProfile::Counter {
 public:
  HighWaterMarkCounter(TUnit::type unit) : Counter(unit) {}

  virtual void Add(int64_t delta) {
    int64_t new_val = current_value_.Add(delta);
    UpdateMax(new_val);
  }

  /// Tries to increase the current value by delta. If current_value() + delta
  /// exceeds max, return false and current_value is not changed.
  bool TryAdd(int64_t delta, int64_t max) {
    while (true) {
      int64_t old_val = current_value_.Load();
      int64_t new_val = old_val + delta;
      if (UNLIKELY(new_val > max)) return false;
      if (LIKELY(current_value_.CompareAndSwap(old_val, new_val))) {
        UpdateMax(new_val);
        return true;
      }
    }
  }

  virtual void Set(int64_t v) {
    current_value_.Store(v);
    UpdateMax(v);
  }

  int64_t current_value() const { return current_value_.Load(); }

 private:
  /// Set 'value_' to 'v' if 'v' is larger than 'value_'. The entire operation is
  /// atomic.
  void UpdateMax(int64_t v) {
    while (true) {
      int64_t old_max = value_.Load();
      int64_t new_max = std::max(old_max, v);
      if (new_max == old_max) break; // Avoid atomic update.
      if (LIKELY(value_.CompareAndSwap(old_max, new_max))) break;
    }
  }

  /// The current value of the counter. value_ in the super class represents
  /// the high water mark.
  AtomicInt64 current_value_;
};

/// A DerivedCounter also has a name and unit, but the value is computed.
/// Do not call Set() and Add().
class RuntimeProfile::DerivedCounter : public RuntimeProfile::Counter {
 public:
  DerivedCounter(TUnit::type unit, const DerivedCounterFunction& counter_fn)
    : Counter(unit),
      counter_fn_(counter_fn) {}

  virtual int64_t value() const {
    return counter_fn_();
  }

 private:
  DerivedCounterFunction counter_fn_;
};

/// An AveragedCounter maintains a set of counters and its value is the
/// average of the values in that set. The average is updated through calls
/// to UpdateCounter(), which may add a new counter or update an existing counter.
/// Set() and Add() should not be called.
class RuntimeProfile::AveragedCounter : public RuntimeProfile::Counter {
 public:
  AveragedCounter(TUnit::type unit)
   : Counter(unit),
     current_double_sum_(0.0),
     current_int_sum_(0) {
  }

  /// Update counter_value_map_ with the new counter. This may require the counter
  /// to be added to the map.
  /// No locks are obtained within this class because UpdateCounter() is called from
  /// UpdateAverage(), which obtains locks on the entire counter map in a profile.
  void UpdateCounter(Counter* new_counter) {
    DCHECK_EQ(new_counter->unit_, unit_);
    boost::unordered_map<Counter*, int64_t>::iterator it =
        counter_value_map_.find(new_counter);
    int64_t old_val = 0;
    if (it != counter_value_map_.end()) {
      old_val = it->second;
      it->second = new_counter->value();
    } else {
      counter_value_map_[new_counter] = new_counter->value();
    }

    if (unit_ == TUnit::DOUBLE_VALUE) {
      double old_double_val = *reinterpret_cast<double*>(&old_val);
      current_double_sum_ += (new_counter->double_value() - old_double_val);
      double result_val = current_double_sum_ / (double) counter_value_map_.size();
      value_.Store(*reinterpret_cast<int64_t*>(&result_val));
    } else {
      current_int_sum_ += (new_counter->value() - old_val);
      value_.Store(current_int_sum_ / counter_value_map_.size());
    }
  }

  /// The value for this counter should be updated through UpdateCounter().
  /// Set() and Add() should not be used.
  virtual void Set(double value) { DCHECK(false); }
  virtual void Set(int64_t value) { DCHECK(false); }
  virtual void Add(int64_t delta) { DCHECK(false); }

 private:
  /// Map from counters to their existing values. Modified via UpdateCounter().
  boost::unordered_map<Counter*, int64_t> counter_value_map_;

  /// Current sums of values from counter_value_map_. Only one of these is used,
  /// depending on the unit of the counter. current_double_sum_ is used for
  /// DOUBLE_VALUE, current_int_sum_ otherwise.
  double current_double_sum_;
  int64_t current_int_sum_;
};

/// This counter records multiple values and keeps a track of the minimum, maximum and
/// average value of all the values seen so far.
/// Unlike the AveragedCounter, this only keeps track of statistics of raw values
/// whereas the AveragedCounter maintains an average of counters.
/// value() stores the average.
class RuntimeProfile::SummaryStatsCounter : public RuntimeProfile::Counter {
 public:
  SummaryStatsCounter(TUnit::type unit, int32_t total_num_values,
      int64_t min_value, int64_t max_value, int64_t sum)
   : Counter(unit),
     total_num_values_(total_num_values),
     min_(min_value),
     max_(max_value),
     sum_(sum) {
    value_.Store(total_num_values == 0 ? 0 : sum / total_num_values);
  }

  SummaryStatsCounter(TUnit::type unit)
   : Counter(unit),
     total_num_values_(0),
     min_(numeric_limits<int64_t>::max()),
     max_(numeric_limits<int64_t>::min()),
     sum_(0) {
  }

  int64_t MinValue();
  int64_t MaxValue();
  int32_t TotalNumValues();

  /// Update sum_ with the new value and also update the min and the max values
  /// seen so far.
  void UpdateCounter(int64_t new_value);

  /// The value for this counter should be updated through UpdateCounter() or SetStats().
  /// Set() and Add() should not be used.
  virtual void Set(double value) { DCHECK(false); }
  virtual void Set(int64_t value) { DCHECK(false); }
  virtual void Add(int64_t delta) { DCHECK(false); }

  /// Overwrites the existing counter with 'counter'
  void SetStats(const TSummaryStatsCounter& counter);

  void ToThrift(TSummaryStatsCounter* counter, const std::string& name);

 private:
  /// The total number of values seen so far.
  int32_t total_num_values_;

  /// Summary statistics of values seen so far.
  int64_t min_;
  int64_t max_;
  int64_t sum_;

  // Protects min_, max_, sum_, total_num_values_ and value_.
  SpinLock lock_;
};

/// A set of counters that measure thread info, such as total time, user time, sys time.
class RuntimeProfile::ThreadCounters {
 private:
  friend class ThreadCounterMeasurement;
  friend class RuntimeProfile;

  Counter* total_time_; // total wall clock time
  Counter* user_time_;  // user CPU time
  Counter* sys_time_;   // system CPU time

  /// The number of times a context switch resulted due to a process voluntarily giving
  /// up the processor before its time slice was completed.
  Counter* voluntary_context_switches_;

  /// The number of times a context switch resulted due to a higher priority process
  /// becoming runnable or because the current process exceeded its time slice.
  Counter* involuntary_context_switches_;
};

/// An EventSequence captures a sequence of events (each added by
/// calling MarkEvent). Each event has a text label, and a time
/// (measured relative to the moment Start() was called as t=0). It is
/// useful for tracking the evolution of some serial process, such as
/// the query lifecycle.
class RuntimeProfile::EventSequence {
 public:
  EventSequence() { }

  /// Helper constructor for building from Thrift
  EventSequence(const std::vector<int64_t>& timestamps,
                const std::vector<std::string>& labels) {
    DCHECK(timestamps.size() == labels.size());
    for (int i = 0; i < timestamps.size(); ++i) {
      events_.push_back(make_pair(labels[i], timestamps[i]));
    }
  }

  /// Starts the timer without resetting it.
  void Start() { sw_.Start(); }

  /// Stores an event in sequence with the given label and the current time
  /// (relative to the first time Start() was called) as the timestamp.
  void MarkEvent(const std::string& label) {
    Event event = make_pair(label, sw_.ElapsedTime());
    boost::lock_guard<SpinLock> event_lock(lock_);
    events_.push_back(event);
  }

  int64_t ElapsedTime() { return sw_.ElapsedTime(); }

  /// An Event is a <label, timestamp> pair.
  typedef std::pair<std::string, int64_t> Event;

  /// An EventList is a sequence of Events, in increasing timestamp order.
  typedef std::vector<Event> EventList;

  /// Copies the member events_ into the supplied vector 'events'.
  /// The supplied vector 'events' is cleared before this.
  void GetEvents(std::vector<Event>* events) {
    events->clear();
    boost::lock_guard<SpinLock> event_lock(lock_);
    events->insert(events->end(), events_.begin(), events_.end());
  }

  void ToThrift(TEventSequence* seq) const;

 private:
  /// Protect access to events_.
  SpinLock lock_;

  /// Stored in increasing time order.
  EventList events_;

  /// Timer which allows events to be timestamped when they are recorded.
  MonotonicStopWatch sw_;
};

typedef StreamingSampler<int64_t, 64> StreamingCounterSampler;
class RuntimeProfile::TimeSeriesCounter {
 public:
  std::string DebugString() const;

  void AddSample(int ms_elapsed) {
    int64_t sample = sample_fn_();
    samples_.AddSample(sample, ms_elapsed);
  }

 private:
  friend class RuntimeProfile;

  TimeSeriesCounter(const std::string& name, TUnit::type unit,
      DerivedCounterFunction fn)
    : name_(name), unit_(unit), sample_fn_(fn) {
  }

  /// Construct a time series object from existing sample data. This counter
  /// is then read-only (i.e. there is no sample function).
  TimeSeriesCounter(const std::string& name, TUnit::type unit, int period,
      const std::vector<int64_t>& values)
    : name_(name), unit_(unit), sample_fn_(NULL), samples_(period, values) {
  }

  void ToThrift(TTimeSeriesCounter* counter);

  std::string name_;
  TUnit::type unit_;
  DerivedCounterFunction sample_fn_;
  StreamingCounterSampler samples_;
};

/// Counter whose value comes from an internal ConcurrentStopWatch to track multiple threads
/// concurrent running time.
class RuntimeProfile::ConcurrentTimerCounter : public Counter {
 public:
  ConcurrentTimerCounter(TUnit::type unit) : Counter(unit) {}

  virtual int64_t value() const { return csw_.TotalRunningTime(); }

  void Start() { csw_.Start(); }

  void Stop() { csw_.Stop(); }

  /// Returns lap time for caller who wants delta update of concurrent running time.
  uint64_t LapTime() { return csw_.LapTime(); }

  /// The value for this counter should come from internal ConcurrentStopWatch.
  /// Set() and Add() should not be used.
  virtual void Set(double value) {
    DCHECK(false);
  }

  virtual void Set(int64_t value) {
    DCHECK(false);
  }

  virtual void Set(int value) {
    DCHECK(false);
  }

  virtual void Add(int64_t delta) {
    DCHECK(false);
  }

 private:
  ConcurrentStopWatch csw_;
};

/// Utility class to mark an event when the object is destroyed.
class ScopedEvent {
 public:
  ScopedEvent(RuntimeProfile::EventSequence* event_sequence, const std::string& label)
    : label_(label),
      event_sequence_(event_sequence) {
  }

  /// Mark the event when the object is destroyed
  ~ScopedEvent() {
    event_sequence_->MarkEvent(label_);
  }

 private:
  /// Disable copy constructor and assignment
  ScopedEvent(const ScopedEvent& event);
  ScopedEvent& operator=(const ScopedEvent& event);

  const std::string label_;
  RuntimeProfile::EventSequence* event_sequence_;
};

/// Utility class to update time elapsed when the object goes out of scope.
/// 'T' must implement the StopWatch "interface" (Start,Stop,ElapsedTime) but
/// we use templates not to pay for virtual function overhead. In some cases
/// the runtime profile may be deleted while the counter is still active. In this
/// case the is_cancelled argument can be provided so that ScopedTimer will not
/// update the counter when the query is cancelled. The destructor for ScopedTimer
/// can access both is_cancelled and the counter, so the caller must ensure that it
/// is safe to access both at the end of the scope in which the timer is used.
template<class T>
class ScopedTimer {
 public:
  ScopedTimer(RuntimeProfile::Counter* counter, const bool* is_cancelled = NULL) :
    counter_(counter), is_cancelled_(is_cancelled){
    if (counter == NULL) return;
    DCHECK(counter->unit() == TUnit::TIME_NS);
    sw_.Start();
  }

  void Stop() { sw_.Stop(); }
  void Start() { sw_.Start(); }

  void UpdateCounter() {
    if (counter_ != NULL && !IsCancelled()) {
      counter_->Add(sw_.ElapsedTime());
    }
  }

  /// Updates the underlying counter for the final time and clears the pointer to it.
  void ReleaseCounter() {
    UpdateCounter();
    counter_ = NULL;
  }

  bool IsCancelled() {
    return is_cancelled_ != NULL && *is_cancelled_;
  }

  /// Update counter when object is destroyed
  ~ScopedTimer() {
    sw_.Stop();
    UpdateCounter();
  }

 private:
  /// Disable copy constructor and assignment
  ScopedTimer(const ScopedTimer& timer);
  ScopedTimer& operator=(const ScopedTimer& timer);

  T sw_;
  RuntimeProfile::Counter* counter_;
  const bool* is_cancelled_;
};


#ifdef __APPLE__
// On OS X rusage via thread is not supported. In addition, the majority of the fields of
// the usage structs will be zeroed out. Since Apple is not going to be a major plaform
// initially it will most likely be enough to capture only time.
// C.f. http://blog.kuriositaet.de/?p=257
#define RUSAGE_THREAD RUSAGE_SELF
#endif

/// Utility class to update ThreadCounter when the object goes out of scope or when Stop is
/// called. Threads measurements will then be taken using getrusage.
/// This is ~5x slower than ScopedTimer due to calling getrusage.
class ThreadCounterMeasurement {
 public:
  ThreadCounterMeasurement(RuntimeProfile::ThreadCounters* counters) :
    stop_(false), counters_(counters) {
    DCHECK(counters != NULL);
    sw_.Start();
    int ret = getrusage(RUSAGE_THREAD, &usage_base_);
    DCHECK_EQ(ret, 0);
  }

  /// Stop and update the counter
  void Stop() {
    if (stop_) return;
    stop_ = true;
    sw_.Stop();
    rusage usage;
    int ret = getrusage(RUSAGE_THREAD, &usage);
    DCHECK_EQ(ret, 0);
    int64_t utime_diff =
        (usage.ru_utime.tv_sec - usage_base_.ru_utime.tv_sec) * 1000L * 1000L * 1000L +
        (usage.ru_utime.tv_usec - usage_base_.ru_utime.tv_usec) * 1000L;
    int64_t stime_diff =
        (usage.ru_stime.tv_sec - usage_base_.ru_stime.tv_sec) * 1000L * 1000L * 1000L +
        (usage.ru_stime.tv_usec - usage_base_.ru_stime.tv_usec) * 1000L;
    counters_->total_time_->Add(sw_.ElapsedTime());
    counters_->user_time_->Add(utime_diff);
    counters_->sys_time_->Add(stime_diff);
    counters_->voluntary_context_switches_->Add(usage.ru_nvcsw - usage_base_.ru_nvcsw);
    counters_->involuntary_context_switches_->Add(
        usage.ru_nivcsw - usage_base_.ru_nivcsw);
  }

  /// Update counter when object is destroyed
  ~ThreadCounterMeasurement() {
    Stop();
  }

 private:
  /// Disable copy constructor and assignment
  ThreadCounterMeasurement(const ThreadCounterMeasurement& timer);
  ThreadCounterMeasurement& operator=(const ThreadCounterMeasurement& timer);

  bool stop_;
  rusage usage_base_;
  MonotonicStopWatch sw_;
  RuntimeProfile::ThreadCounters* counters_;
};
}

#endif
