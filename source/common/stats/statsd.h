#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/local_info/local_info.h"
#include "envoy/network/connection.h"
#include "envoy/stats/stats.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

namespace Envoy {
namespace Stats {
namespace Statsd {

/**
 * This is a simple UDP localhost writer for statsd messages.
 */
class Writer : public ThreadLocal::ThreadLocalObject {
public:
  Writer(Network::Address::InstanceConstSharedPtr address);
  ~Writer();

  void writeCounter(const std::string& name, uint64_t increment);
  void writeGauge(const std::string& name, uint64_t value);
  void writeTimer(const std::string& name, const std::chrono::milliseconds& ms);
  void shutdown() override;
  // Called in unit test to validate address.
  int getFdForTests() const { return fd_; };

private:
  void send(const std::string& message);

  int fd_;
  bool shutdown_{};
};

/**
 * Implementation of Sink that writes to a UDP statsd address.
 */
class UdpStatsdSink : public Sink {
public:
  UdpStatsdSink(ThreadLocal::Instance& tls, Network::Address::InstanceConstSharedPtr address);

  // Stats::Sink
  void beginFlush() override {}
  void flushCounter(const std::string& name, uint64_t delta) override;
  void flushGauge(const std::string& name, uint64_t value) override;
  void endFlush() override {}
  void onHistogramComplete(const std::string& name, uint64_t value) override {
    // For statsd histograms are just timers.
    onTimespanComplete(name, std::chrono::milliseconds(value));
  }
  void onTimespanComplete(const std::string& name, std::chrono::milliseconds ms) override;
  // Called in unit test to validate writer construction and address.
  int getFdForTests() { return tls_.getTyped<Writer>(tls_slot_).getFdForTests(); }

private:
  ThreadLocal::Instance& tls_;
  const uint32_t tls_slot_;
  Network::Address::InstanceConstSharedPtr server_address_;
};

/**
 * Per thread implementation of a TCP stats flusher for statsd.
 */
class TcpStatsdSink : public Sink {
public:
  TcpStatsdSink(const LocalInfo::LocalInfo& local_info, const std::string& cluster_name,
                ThreadLocal::Instance& tls, Upstream::ClusterManager& cluster_manager,
                Stats::Scope& scope);

  // Stats::Sink
  void beginFlush() override {
    tls_.getTyped<TlsSink>(tls_slot_).beginFlush();
  }

  void flushCounter(const std::string& name, uint64_t delta) override {
    tls_.getTyped<TlsSink>(tls_slot_).flushCounter(name, delta);
  }

  void flushGauge(const std::string& name, uint64_t value) override {
    tls_.getTyped<TlsSink>(tls_slot_).flushGauge(name, value);
  }

  void endFlush() override {
    tls_.getTyped<TlsSink>(tls_slot_).endFlush();
  }

  void onHistogramComplete(const std::string& name, uint64_t value) override {
    // For statsd histograms are just timers.
    onTimespanComplete(name, std::chrono::milliseconds(value));
  }

  void onTimespanComplete(const std::string& name, std::chrono::milliseconds ms) override {
    tls_.getTyped<TlsSink>(tls_slot_).onTimespanComplete(name, ms);
  }

private:
  struct TlsSink : public ThreadLocal::ThreadLocalObject, public Network::ConnectionCallbacks {
    TlsSink(TcpStatsdSink& parent, Event::Dispatcher& dispatcher);
    ~TlsSink();

    void beginFlush();
    void flushCounter(const std::string& name, uint64_t delta);
    void flushGauge(const std::string& name, uint64_t value);
    void endFlush();
    void onTimespanComplete(const std::string& name, std::chrono::milliseconds ms);
    void write(const std::string& stat);

    // ThreadLocal::ThreadLocalObject
    void shutdown() override;

    // Network::ConnectionCallbacks
    void onEvent(uint32_t events) override;
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    TcpStatsdSink& parent_;
    Event::Dispatcher& dispatcher_;
    Network::ClientConnectionPtr connection_;
    bool shutdown_{};
    Buffer::OwnedImpl buffer_;
  };

  // Somewhat arbitrary 16MiB limit for buffered stats.
  static constexpr uint32_t MaxBufferedStatsBytes = (1024 * 1024 * 16);

  Upstream::ClusterInfoConstSharedPtr cluster_info_;
  ThreadLocal::Instance& tls_;
  uint32_t tls_slot_;
  Upstream::ClusterManager& cluster_manager_;
  Stats::Counter& cx_overflow_stat_;
};

} // namespace Statsd
} // namespace Stats
} // namespace Envoy
