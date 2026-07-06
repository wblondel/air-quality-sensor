#include "NetLog.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <errno.h>
#include <sys/time.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "lwip/sockets.h"

namespace {

const char* TAG = "NetLog";

// Tunables. Kept as constants (rather than Kconfig) so the tracked sdkconfig is
// untouched; promote to Kconfig if per-build configuration is ever needed.
constexpr uint16_t kPort = 2333;        // idf.py monitor -p 'socket://[<ipv6>]:2333'
constexpr size_t kRingBufSize = 4096;   // bytes of log buffered awaiting send
constexpr size_t kLineMax = 192;        // max bytes captured per log line
constexpr int kBacklog = 1;             // single client at a time

std::atomic<bool> s_enabled{false};
RingbufHandle_t s_ringbuf = nullptr;
vprintf_like_t s_uartVprintf = nullptr; // original (UART) log sink
TaskHandle_t s_task = nullptr;

// Replacement log sink: always writes to the original UART sink, and also
// queues the formatted line for the network when enabled. Runs in the context
// of whichever task is logging, so it stays quick and never blocks.
int LogVprintf(const char* fmt, va_list args)
{
    int ret;
    if (s_uartVprintf) {
        va_list uart_args;
        va_copy(uart_args, args);
        ret = s_uartVprintf(fmt, uart_args);
        va_end(uart_args);
    } else {
        va_list std_args;
        va_copy(std_args, args);
        ret = vprintf(fmt, std_args);
        va_end(std_args);
    }

    if (s_enabled.load(std::memory_order_relaxed) && s_ringbuf) {
        char line[kLineMax];
        va_list net_args;
        va_copy(net_args, args);
        int n = vsnprintf(line, sizeof(line), fmt, net_args);
        va_end(net_args);
        if (n > 0) {
            size_t len = (n < (int)sizeof(line)) ? (size_t)n : sizeof(line) - 1;
            // Non-blocking (0 ticks): the line is dropped if the buffer is full.
            xRingbufferSend(s_ringbuf, line, len, 0);
        }
    }

    return ret;
}

// Drains buffered log lines to a connected client until it disconnects or
// logging is disabled. Uses non-blocking sends and drops lines rather than
// blocking when the socket buffer is full (slow reader / congested mesh).
void ServeClient(int client)
{
    while (s_enabled.load(std::memory_order_relaxed)) {
        size_t item_size = 0;
        void* item = xRingbufferReceive(s_ringbuf, &item_size, pdMS_TO_TICKS(500));
        if (item == nullptr) {
            continue; // timeout: no new logs; keep the connection open
        }
        int sent = send(client, item, item_size, MSG_DONTWAIT);
        vRingbufferReturnItem(s_ringbuf, item);
        if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            break; // client gone (EPIPE/ECONNRESET/...)
        }
    }
}

void ServerTask(void*)
{
    for (;;) {
        if (!s_enabled.load(std::memory_order_relaxed)) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        int listen_fd = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
        if (listen_fd < 0) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        int opt = 1;
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in6 addr;
        memset(&addr, 0, sizeof(addr)); // zeroed sin6_addr == in6addr_any (::)
        addr.sin6_family = AF_INET6;
        addr.sin6_port = htons(kPort);

        if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) != 0 ||
            listen(listen_fd, kBacklog) != 0) {
            ESP_LOGE(TAG, "bind/listen on port %u failed (errno %d)", (unsigned)kPort, errno);
            close(listen_fd);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // Time out accept() so a disable request is noticed within ~1 s.
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        setsockopt(listen_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        ESP_LOGI(TAG, "Log server listening on TCP port %u", (unsigned)kPort);

        while (s_enabled.load(std::memory_order_relaxed)) {
            int client = accept(listen_fd, nullptr, nullptr);
            if (client < 0) {
                continue; // accept timeout -> re-check enabled
            }
            ESP_LOGI(TAG, "Log client connected");
            ServeClient(client);
            close(client);
            ESP_LOGI(TAG, "Log client disconnected");
        }

        close(listen_fd);
    }
}

} // namespace

namespace NetLog {

void Init()
{
    if (s_ringbuf) {
        return; // already initialized
    }

    s_ringbuf = xRingbufferCreate(kRingBufSize, RINGBUF_TYPE_NOSPLIT);
    if (!s_ringbuf) {
        ESP_LOGE(TAG, "Failed to allocate log ring buffer; network logging unavailable");
        return;
    }

    // Install our sink and keep the previous (UART) one so USB stays functional.
    s_uartVprintf = esp_log_set_vprintf(&LogVprintf);

    if (xTaskCreate(&ServerTask, "netlog", 4096, nullptr, 3, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start network log task");
    }
}

void SetEnabled(bool enabled)
{
    bool was = s_enabled.exchange(enabled);
    if (was != enabled) {
        ESP_LOGI(TAG, "Network logging %s", enabled ? "ENABLED" : "disabled");
    }
}

bool IsEnabled()
{
    return s_enabled.load(std::memory_order_relaxed);
}

} // namespace NetLog
