#pragma once

// Streams the ESP-IDF log console over the Thread network via a small TCP
// server, so it can be viewed wirelessly with:
//
//     idf.py monitor -p 'socket://[<device-ipv6>]:2333'
//
// The UART/USB console keeps working unchanged. The server is off by default
// and toggled at runtime (from the settings menu). It is backpressure-safe: log
// lines are queued into a bounded buffer and dropped rather than ever blocking
// the task that logged them, so a slow reader or congested mesh can't stall the
// device.
namespace NetLog {

// Installs the log tee and starts the (idle) server task. Call once at boot,
// after the log system is up. Safe to call before the network is ready.
void Init();

// Starts or stops the TCP log server at runtime.
void SetEnabled(bool enabled);

// Whether the server is currently enabled.
bool IsEnabled();

} // namespace NetLog
