#include "air780e_at_modem.h"
#include <esp_log.h>
#include <esp_err.h>
#include <cassert>
#include <sstream>
#include <iomanip>
#include <cstring>
#include "air780e_ssl.h"
#include "air780e_tcp.h"
#include "air780e_udp.h"
#include "air780e_mqtt.h"
#include "http_client.h"
#include "web_socket.h"

#define TAG "Air780EAtModem"


Air780EAtModem::Air780EAtModem(std::shared_ptr<AtUart> at_uart) : AtModem(at_uart) {
    // 子类特定的初始化在这里
    // ATE0 关闭 echo
    at_uart_->SendCommand("ATE0");
    // 设置 URC 端口为 UART1
    at_uart_->SendCommand("AT+QURCCFG=\"urcport\",\"uart1\"");
}

void Air780EAtModem::HandleUrc(const std::string& command, const std::vector<AtArgumentValue>& arguments) {
    // Handle Common URC
    AtModem::HandleUrc(command, arguments);
}

bool Air780EAtModem::SetSleepMode(bool enable, int delay_seconds) {
    if (enable) {
        if (delay_seconds > 0) {
            at_uart_->SendCommand("AT+QSCLKEX=1," + std::to_string(delay_seconds) + ",30");
        }
        return at_uart_->SendCommand("AT+QSCLK=1");
    } else {
        return at_uart_->SendCommand("AT+QSCLK=0");
    }
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