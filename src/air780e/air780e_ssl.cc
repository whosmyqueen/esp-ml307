#include "air780e_ssl.h"

#include <esp_log.h>

#define TAG "Air780ESsl"

Air780ESsl::Air780ESsl(std::shared_ptr<AtUart> at_uart, int ssl_id)
    : Air780ETcp(std::move(at_uart), ssl_id) {}

bool Air780ESsl::ConfigureSsl(int /*port*/) {
    // AT 手册：AT+CIPSSL=1 + AT+SSLCFG 配置 SSL 上下文参数
    // 这里给每个连接 id 使用同一个 context id（=tcp_id_），便于多路区分。
    // 默认不校验证书（seclevel=0），TLS1.2（sslversion=4），全套 cipher（0xFFFF）。
    const int ctx = tcp_id_;
    if (!at_uart_->SendCommand("AT+SSLCFG=\"sslversion\"," + std::to_string(ctx) + ",4")) {
        ESP_LOGW(TAG, "SSLCFG sslversion failed");
    }
    if (!at_uart_->SendCommand("AT+SSLCFG=\"ciphersuite\"," + std::to_string(ctx) + ",0xFFFF")) {
        ESP_LOGW(TAG, "SSLCFG ciphersuite failed");
    }
    if (!at_uart_->SendCommand("AT+SSLCFG=\"seclevel\"," + std::to_string(ctx) + ",0")) {
        ESP_LOGW(TAG, "SSLCFG seclevel failed");
    }
    return at_uart_->SendCommand("AT+CIPSSL=1");
}

