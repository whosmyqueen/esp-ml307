#include "air780e_udp.h"

#include <algorithm>
#include <esp_log.h>

#define TAG "Air780EUdp"

Air780EUdp::Air780EUdp(std::shared_ptr<AtUart> at_uart, int udp_id)
    : at_uart_(std::move(at_uart)), udp_id_(udp_id) {
    event_group_handle_ = xEventGroupCreate();

    urc_callback_it_ = at_uart_->RegisterUrcCallback([this](const std::string& command, const std::vector<AtArgumentValue>& arguments) {
        if (command == "CONNECT") {
            if (arguments.size() >= 2 && arguments[0].type == AtArgumentValue::Type::Int && arguments[0].int_value == udp_id_) {
                if (arguments[1].string_value == "OK" || arguments[1].string_value == "ALREADY") {
                    connected_ = true;
                    instance_active_ = true;
                    xEventGroupClearBits(event_group_handle_, AIR780E_UDP_DISCONNECTED | AIR780E_UDP_ERROR);
                    xEventGroupSetBits(event_group_handle_, AIR780E_UDP_CONNECTED);
                } else {
                    connected_ = false;
                    xEventGroupSetBits(event_group_handle_, AIR780E_UDP_ERROR);
                }
            }
        } else if (command == "CLOSE") {
            if (arguments.empty() || (arguments.size() >= 2 && arguments[0].type == AtArgumentValue::Type::Int && arguments[0].int_value == udp_id_)) {
                connected_ = false;
                instance_active_ = false;
                xEventGroupSetBits(event_group_handle_, AIR780E_UDP_DISCONNECTED);
            }
        } else if (command == "SEND" || command == "DATAACCEPT") {
            // UDP 发送完成
            if (command == "SEND") {
                bool match = arguments.size() >= 2 && arguments[0].type == AtArgumentValue::Type::Int && arguments[0].int_value == udp_id_;
                if (match) {
                    if (arguments[1].string_value == "OK") {
                        xEventGroupSetBits(event_group_handle_, AIR780E_UDP_SEND_COMPLETE);
                    } else {
                        xEventGroupSetBits(event_group_handle_, AIR780E_UDP_ERROR);
                    }
                }
            } else {
                // DATAACCEPT: [id,len] or [len]
                if (arguments.size() == 2 && arguments[0].type == AtArgumentValue::Type::Int && arguments[0].int_value == udp_id_) {
                    xEventGroupSetBits(event_group_handle_, AIR780E_UDP_SEND_COMPLETE);
                } else if (arguments.size() == 1) {
                    xEventGroupSetBits(event_group_handle_, AIR780E_UDP_SEND_COMPLETE);
                }
            }
        } else if (command == "CIPRXGET") {
            if (arguments.size() >= 1 && arguments[0].type == AtArgumentValue::Type::Int) {
                int mode = arguments[0].int_value;
                if (mode == 1) {
                    int link_id = -1;
                    if (arguments.size() >= 2 && arguments[1].type == AtArgumentValue::Type::Int) {
                        link_id = arguments[1].int_value;
                    } else {
                        link_id = 0;
                    }
                    if (link_id == udp_id_) {
                        xEventGroupSetBits(event_group_handle_, AIR780E_UDP_DATA_AVAILABLE);
                    }
                } else if (mode == 3) {
                    if (arguments.size() >= 4 && arguments[1].type == AtArgumentValue::Type::Int && arguments[1].int_value == udp_id_) {
                        last_cnlen_ = arguments[2].int_value;
                        last_rlen_ = arguments[3].int_value;
                        xEventGroupSetBits(event_group_handle_, AIR780E_UDP_RX_HEADER_READY);
                    }
                }
            }
        } else if (command == "FIFO_OVERFLOW") {
            xEventGroupSetBits(event_group_handle_, AIR780E_UDP_ERROR);
        }
    });
}

Air780EUdp::~Air780EUdp() {
    Disconnect();
    StopRxTaskIfNeeded();
    at_uart_->UnregisterUrcCallback(urc_callback_it_);
    if (event_group_handle_) {
        vEventGroupDelete(event_group_handle_);
    }
}

void Air780EUdp::StartRxTaskIfNeeded() {
    if (rx_task_handle_) return;
    xTaskCreatePinnedToCore([](void* arg) {
        static_cast<Air780EUdp*>(arg)->RxTask();
        vTaskDelete(nullptr);
    }, "air780e_udp_rx", 4096, this, configMAX_PRIORITIES - 3, &rx_task_handle_, 0);
}

void Air780EUdp::StopRxTaskIfNeeded() {
    if (rx_task_handle_) {
        vTaskDelete(rx_task_handle_);
        rx_task_handle_ = nullptr;
    }
}

bool Air780EUdp::Connect(const std::string& host, int port) {
    xEventGroupClearBits(event_group_handle_, AIR780E_UDP_CONNECTED | AIR780E_UDP_DISCONNECTED | AIR780E_UDP_ERROR);

    at_uart_->SendCommand("AT+CIPMODE=0");
    at_uart_->SendCommand("AT+CIPMUX=1");
    at_uart_->SendCommand("AT+CIPQSEND=1");
    at_uart_->SendCommand("AT+CIPSPRT=1");
    at_uart_->SendCommand("AT+CIPRXGET=5");

    at_uart_->SendCommand("AT+CSTT");
    at_uart_->SendCommand("AT+CIICR");
    at_uart_->SendCommand("AT+CIFSR");

    if (instance_active_) {
        at_uart_->SendCommand("AT+CIPCLOSE=" + std::to_string(udp_id_));
        xEventGroupWaitBits(event_group_handle_, AIR780E_UDP_DISCONNECTED, pdTRUE, pdFALSE,
                            pdMS_TO_TICKS(AIR780E_UDP_CONNECT_TIMEOUT_MS));
        instance_active_ = false;
        connected_ = false;
    }

    StartRxTaskIfNeeded();

    std::string cmd = "AT+CIPSTART=" + std::to_string(udp_id_) + ",\"UDP\",\"" + host + "\"," + std::to_string(port);
    if (!at_uart_->SendCommand(cmd)) {
        last_error_ = at_uart_->GetCmeErrorCode();
        ESP_LOGE(TAG, "CIPSTART(UDP) failed, cme=%d", last_error_);
        return false;
    }

    auto bits = xEventGroupWaitBits(event_group_handle_, AIR780E_UDP_CONNECTED | AIR780E_UDP_ERROR, pdTRUE, pdFALSE,
                                    pdMS_TO_TICKS(AIR780E_UDP_CONNECT_TIMEOUT_MS));
    if (bits & AIR780E_UDP_ERROR) return false;
    if (!(bits & AIR780E_UDP_CONNECTED)) return false;
    return true;
}

void Air780EUdp::Disconnect() {
    if (!instance_active_) return;
    at_uart_->SendCommand("AT+CIPCLOSE=" + std::to_string(udp_id_));
    xEventGroupWaitBits(event_group_handle_, AIR780E_UDP_DISCONNECTED, pdTRUE, pdFALSE,
                        pdMS_TO_TICKS(AIR780E_UDP_CONNECT_TIMEOUT_MS));
    instance_active_ = false;
    connected_ = false;
}

int Air780EUdp::Send(const std::string& data) {
    const size_t MAX_PACKET_SIZE = 1460;
    if (!connected_) return -1;
    if (data.size() > MAX_PACKET_SIZE) return -1;

    xEventGroupClearBits(event_group_handle_, AIR780E_UDP_SEND_COMPLETE | AIR780E_UDP_ERROR);
    std::string cmd = "AT+CIPSEND=" + std::to_string(udp_id_) + "," + std::to_string(data.size());
    if (!at_uart_->SendCommandWithData(cmd, 3000, true, data.data(), data.size())) {
        last_error_ = at_uart_->GetCmeErrorCode();
        return -1;
    }

    auto bits = xEventGroupWaitBits(event_group_handle_, AIR780E_UDP_SEND_COMPLETE | AIR780E_UDP_ERROR, pdTRUE, pdFALSE,
                                    pdMS_TO_TICKS(AIR780E_UDP_CONNECT_TIMEOUT_MS));
    if (bits & AIR780E_UDP_ERROR) return -1;
    if (!(bits & AIR780E_UDP_SEND_COMPLETE)) return -1;
    return static_cast<int>(data.size());
}

void Air780EUdp::RxTask() {
    while (true) {
        auto bits = xEventGroupWaitBits(event_group_handle_,
                                        AIR780E_UDP_DATA_AVAILABLE | AIR780E_UDP_DISCONNECTED | AIR780E_UDP_ERROR,
                                        pdTRUE, pdFALSE, portMAX_DELAY);
        if (bits & (AIR780E_UDP_DISCONNECTED | AIR780E_UDP_ERROR)) continue;
        if (bits & AIR780E_UDP_DATA_AVAILABLE) ReadAvailableData();
    }
}

void Air780EUdp::ReadAvailableData() {
    if (!connected_) return;
    std::lock_guard<std::mutex> lock(rx_mutex_);

    while (connected_) {
        xEventGroupClearBits(event_group_handle_, AIR780E_UDP_RX_HEADER_READY);
        last_cnlen_ = 0;
        last_rlen_ = 0;

        std::string cmd = "AT+CIPRXGET=3," + std::to_string(udp_id_) + ",730";
        if (!at_uart_->SendCommand(cmd)) {
            last_error_ = at_uart_->GetCmeErrorCode();
            xEventGroupSetBits(event_group_handle_, AIR780E_UDP_ERROR);
            return;
        }

        auto bits = xEventGroupWaitBits(event_group_handle_, AIR780E_UDP_RX_HEADER_READY, pdTRUE, pdFALSE,
                                        pdMS_TO_TICKS(2000));
        if (!(bits & AIR780E_UDP_RX_HEADER_READY)) return;

        std::string hex_payload = at_uart_->GetResponse();
        if (!hex_payload.empty() && message_callback_) {
            message_callback_(at_uart_->DecodeHex(hex_payload));
        }

        if (last_rlen_ <= 0) return;
    }
}

int Air780EUdp::GetLastError() {
    return last_error_;
}

