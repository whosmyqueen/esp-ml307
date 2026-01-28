#include "air780e_tcp.h"

#include <algorithm>
#include <esp_log.h>

#define TAG "Air780ETcp"

Air780ETcp::Air780ETcp(std::shared_ptr<AtUart> at_uart, int tcp_id)
    : at_uart_(std::move(at_uart)), tcp_id_(tcp_id) {
    event_group_handle_ = xEventGroupCreate();

    urc_callback_it_ = at_uart_->RegisterUrcCallback([this](const std::string& command, const std::vector<AtArgumentValue>& arguments) {
        // CONNECT / CLOSE / SEND / DATAACCEPT 是 AtUart 对纯文本 URC 的标准化结果
        if (command == "CONNECT") {
            // 多链接：CONNECT, [id, OK|FAIL|ALREADY]
            if (arguments.size() >= 2 && arguments[0].type == AtArgumentValue::Type::Int && arguments[0].int_value == tcp_id_) {
                if (arguments[1].string_value == "OK" || arguments[1].string_value == "ALREADY") {
                    connected_ = true;
                    instance_active_ = true;
                    xEventGroupClearBits(event_group_handle_, AIR780E_TCP_DISCONNECTED | AIR780E_TCP_ERROR);
                    xEventGroupSetBits(event_group_handle_, AIR780E_TCP_CONNECTED);
                } else {
                    connected_ = false;
                    xEventGroupSetBits(event_group_handle_, AIR780E_TCP_ERROR);
                }
            } else if (arguments.size() >= 1 && arguments[0].type == AtArgumentValue::Type::String) {
                // 单链接：CONNECT, [OK|FAIL|ALREADY]
                if (tcp_id_ == 0 && (arguments[0].string_value == "OK" || arguments[0].string_value == "ALREADY")) {
                    connected_ = true;
                    instance_active_ = true;
                    xEventGroupSetBits(event_group_handle_, AIR780E_TCP_CONNECTED);
                }
            }
        } else if (command == "CLOSE") {
            if (arguments.empty() || (arguments.size() >= 2 && arguments[0].type == AtArgumentValue::Type::Int && arguments[0].int_value == tcp_id_)) {
                connected_ = false;
                instance_active_ = false;
                xEventGroupSetBits(event_group_handle_, AIR780E_TCP_DISCONNECTED);
                if (disconnect_callback_) {
                    disconnect_callback_();
                }
            }
        } else if (command == "SEND") {
            // SEND, [id, OK|FAIL] or [OK|FAIL]
            bool match = arguments.empty();
            if (arguments.size() >= 2 && arguments[0].type == AtArgumentValue::Type::Int) {
                match = arguments[0].int_value == tcp_id_;
            } else if (arguments.size() >= 1 && arguments[0].type == AtArgumentValue::Type::String) {
                match = (tcp_id_ == 0);
            }
            if (match) {
                std::string status = arguments.size() >= 2 ? arguments[1].string_value
                                   : (arguments.size() >= 1 ? arguments[0].string_value : "OK");
                if (status == "OK") {
                    xEventGroupSetBits(event_group_handle_, AIR780E_TCP_SEND_COMPLETE);
                } else {
                    xEventGroupSetBits(event_group_handle_, AIR780E_TCP_ERROR);
                }
            }
        } else if (command == "DATAACCEPT") {
            // DATAACCEPT: [len] or [id,len]
            if (arguments.size() == 2 && arguments[0].type == AtArgumentValue::Type::Int) {
                if (arguments[0].int_value == tcp_id_) {
                    xEventGroupSetBits(event_group_handle_, AIR780E_TCP_SEND_COMPLETE);
                }
            } else if (arguments.size() == 1) {
                xEventGroupSetBits(event_group_handle_, AIR780E_TCP_SEND_COMPLETE);
            }
        } else if (command == "CIPRXGET") {
            // +CIPRXGET: 1,<n>  收到数据通知
            if (arguments.size() >= 1 && arguments[0].type == AtArgumentValue::Type::Int) {
                int mode = arguments[0].int_value;
                if (mode == 1) {
                    // 单链接：+CIPRXGET: 1；多链接：+CIPRXGET: 1,<n>
                    int link_id = -1;
                    if (arguments.size() >= 2 && arguments[1].type == AtArgumentValue::Type::Int) {
                        link_id = arguments[1].int_value;
                    } else {
                        link_id = 0;
                    }
                    if (link_id == tcp_id_) {
                        xEventGroupSetBits(event_group_handle_, AIR780E_TCP_DATA_AVAILABLE);
                    }
                } else if (mode == 3) {
                    // +CIPRXGET: 3,<n>,<cnlen>,<rlen>
                    if (arguments.size() >= 4 && arguments[1].type == AtArgumentValue::Type::Int && arguments[1].int_value == tcp_id_) {
                        last_cnlen_ = arguments[2].int_value;
                        last_rlen_ = arguments[3].int_value;
                        xEventGroupSetBits(event_group_handle_, AIR780E_TCP_RX_HEADER_READY);
                    }
                }
            }
        } else if (command == "FIFO_OVERFLOW") {
            xEventGroupSetBits(event_group_handle_, AIR780E_TCP_ERROR);
        }
    });
}

Air780ETcp::~Air780ETcp() {
    Disconnect();
    StopRxTaskIfNeeded();
    at_uart_->UnregisterUrcCallback(urc_callback_it_);
    if (event_group_handle_) {
        vEventGroupDelete(event_group_handle_);
    }
}

bool Air780ETcp::ConfigureSsl(int /*port*/) {
    // 默认关闭 SSL
    return at_uart_->SendCommand("AT+CIPSSL=0");
}

void Air780ETcp::StartRxTaskIfNeeded() {
    if (rx_task_handle_) {
        return;
    }
    xTaskCreatePinnedToCore([](void* arg) {
        static_cast<Air780ETcp*>(arg)->RxTask();
        vTaskDelete(nullptr);
    }, "air780e_tcp_rx", 4096, this, configMAX_PRIORITIES - 3, &rx_task_handle_, 0);
}

void Air780ETcp::StopRxTaskIfNeeded() {
    if (rx_task_handle_) {
        vTaskDelete(rx_task_handle_);
        rx_task_handle_ = nullptr;
    }
}

bool Air780ETcp::Connect(const std::string& host, int port) {
    xEventGroupClearBits(event_group_handle_, AIR780E_TCP_CONNECTED | AIR780E_TCP_DISCONNECTED | AIR780E_TCP_ERROR);

    // 建议使用多链接模式，将 tcp_id 映射到 link id
    at_uart_->SendCommand("AT+CIPMODE=0");
    at_uart_->SendCommand("AT+CIPMUX=1");
    at_uart_->SendCommand("AT+CIPQSEND=1");
    at_uart_->SendCommand("AT+CIPSPRT=1");
    at_uart_->SendCommand("AT+CIPRXGET=5");

    // TCP/UDP/PING 场景下，多链接建议先激活场景（手册要求）
    at_uart_->SendCommand("AT+CSTT");
    at_uart_->SendCommand("AT+CIICR");
    at_uart_->SendCommand("AT+CIFSR");

    // 断开旧连接（不触发回调事件）
    if (instance_active_) {
        at_uart_->SendCommand("AT+CIPCLOSE=" + std::to_string(tcp_id_));
        xEventGroupWaitBits(event_group_handle_, AIR780E_TCP_DISCONNECTED, pdTRUE, pdFALSE,
                            pdMS_TO_TICKS(AIR780E_TCP_CONNECT_TIMEOUT_MS));
        instance_active_ = false;
        connected_ = false;
    }

    if (!ConfigureSsl(port)) {
        ESP_LOGE(TAG, "Configure SSL failed");
        last_error_ = at_uart_->GetCmeErrorCode();
        return false;
    }

    StartRxTaskIfNeeded();

    // 打开 TCP 连接
    std::string cmd = "AT+CIPSTART=" + std::to_string(tcp_id_) + ",\"TCP\",\"" + host + "\"," + std::to_string(port);
    if (!at_uart_->SendCommand(cmd)) {
        last_error_ = at_uart_->GetCmeErrorCode();
        ESP_LOGE(TAG, "CIPSTART failed, cme=%d", last_error_);
        return false;
    }

    auto bits = xEventGroupWaitBits(event_group_handle_, AIR780E_TCP_CONNECTED | AIR780E_TCP_ERROR, pdTRUE, pdFALSE,
                                    pdMS_TO_TICKS(AIR780E_TCP_CONNECT_TIMEOUT_MS));
    if (bits & AIR780E_TCP_ERROR) {
        ESP_LOGE(TAG, "Connect failed");
        return false;
    }
    if (!(bits & AIR780E_TCP_CONNECTED)) {
        ESP_LOGE(TAG, "Connect timeout");
        return false;
    }
    return true;
}

void Air780ETcp::Disconnect() {
    if (!instance_active_) {
        return;
    }

    // 尝试关闭连接
    at_uart_->SendCommand("AT+CIPCLOSE=" + std::to_string(tcp_id_));
    xEventGroupWaitBits(event_group_handle_, AIR780E_TCP_DISCONNECTED, pdTRUE, pdFALSE,
                        pdMS_TO_TICKS(AIR780E_TCP_CONNECT_TIMEOUT_MS));

    instance_active_ = false;
    if (connected_) {
        connected_ = false;
        if (disconnect_callback_) {
            disconnect_callback_();
        }
    }
}

int Air780ETcp::Send(const std::string& data) {
    const size_t MAX_PACKET_SIZE = 1460;
    size_t total_sent = 0;

    if (!connected_) {
        ESP_LOGE(TAG, "Not connected");
        return -1;
    }

    while (total_sent < data.size()) {
        size_t chunk_size = std::min(data.size() - total_sent, MAX_PACKET_SIZE);
        xEventGroupClearBits(event_group_handle_, AIR780E_TCP_SEND_COMPLETE | AIR780E_TCP_ERROR);

        std::string cmd = "AT+CIPSEND=" + std::to_string(tcp_id_) + "," + std::to_string(chunk_size);
        if (!at_uart_->SendCommandWithData(cmd, 3000, true, data.data() + total_sent, chunk_size)) {
            last_error_ = at_uart_->GetCmeErrorCode();
            ESP_LOGE(TAG, "CIPSEND failed, cme=%d", last_error_);
            Disconnect();
            return -1;
        }

        auto bits = xEventGroupWaitBits(event_group_handle_, AIR780E_TCP_SEND_COMPLETE | AIR780E_TCP_ERROR, pdTRUE, pdFALSE,
                                        pdMS_TO_TICKS(AIR780E_TCP_CONNECT_TIMEOUT_MS));
        if (bits & AIR780E_TCP_ERROR) {
            ESP_LOGE(TAG, "Send failed");
            return -1;
        }
        if (!(bits & AIR780E_TCP_SEND_COMPLETE)) {
            ESP_LOGE(TAG, "Send timeout");
            return -1;
        }

        total_sent += chunk_size;
    }
    return static_cast<int>(data.size());
}

void Air780ETcp::RxTask() {
    while (true) {
        auto bits = xEventGroupWaitBits(event_group_handle_,
                                        AIR780E_TCP_DATA_AVAILABLE | AIR780E_TCP_DISCONNECTED | AIR780E_TCP_ERROR,
                                        pdTRUE, pdFALSE, portMAX_DELAY);
        if (bits & (AIR780E_TCP_DISCONNECTED | AIR780E_TCP_ERROR)) {
            continue;
        }
        if (bits & AIR780E_TCP_DATA_AVAILABLE) {
            ReadAvailableData();
        }
    }
}

void Air780ETcp::ReadAvailableData() {
    if (!connected_) {
        return;
    }

    // 避免并发读取同一连接
    std::lock_guard<std::mutex> lock(rx_mutex_);

    while (connected_) {
        xEventGroupClearBits(event_group_handle_, AIR780E_TCP_RX_HEADER_READY);
        last_cnlen_ = 0;
        last_rlen_ = 0;

        // 以 HEX 方式读取，单次最大 730 字节（手册）
        std::string cmd = "AT+CIPRXGET=3," + std::to_string(tcp_id_) + ",730";
        if (!at_uart_->SendCommand(cmd)) {
            last_error_ = at_uart_->GetCmeErrorCode();
            xEventGroupSetBits(event_group_handle_, AIR780E_TCP_ERROR);
            return;
        }

        auto bits = xEventGroupWaitBits(event_group_handle_, AIR780E_TCP_RX_HEADER_READY, pdTRUE, pdFALSE,
                                        pdMS_TO_TICKS(2000));
        if (!(bits & AIR780E_TCP_RX_HEADER_READY)) {
            // 没拿到头，退出等待下一次通知
            return;
        }

        std::string hex_payload = at_uart_->GetResponse();
        if (!hex_payload.empty() && stream_callback_) {
            stream_callback_(at_uart_->DecodeHex(hex_payload));
        }

        if (last_rlen_ <= 0) {
            return;
        }
    }
}

int Air780ETcp::GetLastError() {
    return last_error_;
}

