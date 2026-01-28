#include "air780e_at_modem.h"

#include <cassert>
#include <esp_log.h>

#include "air780e_tcp.h"
#include "air780e_ssl.h"
#include "air780e_udp.h"
#include "air780e_mqtt.h"
#include "http_client.h"
#include "web_socket.h"

#define TAG "Air780EAtModem"

Air780EAtModem::Air780EAtModem(std::shared_ptr<AtUart> at_uart) : AtModem(at_uart) {
    // 关闭回显
    at_uart_->SendCommand("ATE0");
    // 更详细的错误码
    at_uart_->SendCommand("AT+CMEE=2");

    // TCP/IP 默认配置：多链接 + 非透传 + 快发 + 手动接收（通过 +CIPRXGET）
    at_uart_->SendCommand("AT+CIPMODE=0");
    at_uart_->SendCommand("AT+CIPMUX=1");
    at_uart_->SendCommand("AT+CIPQSEND=1");
    at_uart_->SendCommand("AT+CIPSPRT=1");
    at_uart_->SendCommand("AT+CIPRXGET=5");
}

void Air780EAtModem::HandleUrc(const std::string& command, const std::vector<AtArgumentValue>& arguments) {
    // 通用 URC
    AtModem::HandleUrc(command, arguments);

    // Air780E 常见：^MODE 等（由 AtUart 支持 ^ 前缀后会进到这里）
    (void)command;
    (void)arguments;
}

void Air780EAtModem::Reboot() {
    // AT 手册：AT+RESET
    at_uart_->SendCommand("AT+RESET");
}

bool Air780EAtModem::SetSleepMode(bool enable, int delay_seconds) {
    // AT 手册：AT+CSCLK 通过 UART 口设置睡眠唤醒
    // delay_seconds: 可选设置唤醒等待时间（如果不支持就忽略）
    if (enable) {
        if (delay_seconds > 0) {
            // AT+WAKETIM: 设置睡眠等待时间（单位秒）
            at_uart_->SendCommand("AT+WAKETIM=" + std::to_string(delay_seconds));
        }
        return at_uart_->SendCommand("AT+CSCLK=1");
    }
    return at_uart_->SendCommand("AT+CSCLK=0");
}

std::unique_ptr<Http> Air780EAtModem::CreateHttp(int connect_id) {
    assert(connect_id >= 0);
    return std::make_unique<HttpClient>(this, connect_id);
}

std::unique_ptr<Tcp> Air780EAtModem::CreateTcp(int connect_id) {
    assert(connect_id >= 0);
    return std::make_unique<Air780ETcp>(at_uart_, connect_id);
}

std::unique_ptr<Tcp> Air780EAtModem::CreateSsl(int connect_id) {
    assert(connect_id >= 0);
    return std::make_unique<Air780ESsl>(at_uart_, connect_id);
}

std::unique_ptr<Udp> Air780EAtModem::CreateUdp(int connect_id) {
    assert(connect_id >= 0);
    return std::make_unique<Air780EUdp>(at_uart_, connect_id);
}

std::unique_ptr<Mqtt> Air780EAtModem::CreateMqtt(int connect_id) {
    assert(connect_id >= 0);
    return std::make_unique<Air780EMqtt>(at_uart_, connect_id);
}

std::unique_ptr<WebSocket> Air780EAtModem::CreateWebSocket(int connect_id) {
    assert(connect_id >= 0);
    return std::make_unique<WebSocket>(this, connect_id);
}

