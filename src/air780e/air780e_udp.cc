#include "air780e_udp.h"

#include <esp_log.h>

#define TAG "Air780EUdp"


Air780EUdp::Air780EUdp(std::shared_ptr<AtUart> at_uart, int udp_id) : at_uart_(at_uart), udp_id_(udp_id) {
    event_group_handle_ = xEventGroupCreate();

    urc_callback_it_ = at_uart_->RegisterUrcCallback([this](const std::string& command, const std::vector<AtArgumentValue>& arguments) {
        if (command == "+UDPCONNECT" && arguments.size() == 2) {
            if (arguments[0].int_value == udp_id_) {
                connected_ = arguments[1].int_value == 0;
                if (connected_) {
                    instance_active_ = true;
                    xEventGroupClearBits(event_group_handle_, AIR780E_UDP_DISCONNECTED | AIR780E_UDP_ERROR);
                    xEventGroupSetBits(event_group_handle_, AIR780E_UDP_CONNECTED);
                } else {
                    last_error_ = arguments[1].int_value;  // Store error code from UDPCONNECT response
                    xEventGroupSetBits(event_group_handle_, AIR780E_UDP_ERROR);
                }
            }
        } else if (command == "+UDPSEND" && arguments.size() == 3) {
            if (arguments[0].int_value == udp_id_) {
                if (arguments[1].int_value == 0) {
                    xEventGroupSetBits(event_group_handle_, AIR780E_UDP_SEND_COMPLETE);
                } else {
                    xEventGroupSetBits(event_group_handle_, AIR780E_UDP_SEND_FAILED);
                }
            }
        } else if (command == "+UDPURC" && arguments.size() >= 2) {
            if (arguments[1].int_value == udp_id_) {
                if (arguments[0].string_value == "recv" && arguments.size() >= 4) {
                    if (connected_ && message_callback_) {
                        message_callback_(at_uart_->DecodeHex(arguments[3].string_value));
                    }
                } else if (arguments[0].string_value == "closed") {
                    connected_ = false;
                    instance_active_ = false;
                    xEventGroupSetBits(event_group_handle_, AIR780E_UDP_DISCONNECTED);
                } else {
                    ESP_LOGE(TAG, "Unknown UDPURC command: %s", arguments[0].string_value.c_str());
                }
            }
        } else if (command == "+UDPSTATE" && arguments.size() > 5) {
            if (arguments[0].int_value == udp_id_) {
                connected_ = arguments[5].int_value == 2;
                instance_active_ = true;
                xEventGroupSetBits(event_group_handle_, AIR780E_UDP_INITIALIZED);
            }
        } else if (command == "FIFO_OVERFLOW") {
            xEventGroupSetBits(event_group_handle_, AIR780E_UDP_ERROR);
            Disconnect();
        }
    });
}

Air780EUdp::~Air780EUdp() {
    Disconnect();
    at_uart_->UnregisterUrcCallback(urc_callback_it_);
    if (event_group_handle_) {
        vEventGroupDelete(event_group_handle_);
    }
}

bool Air780EUdp::Connect(const std::string& host, int port) {
    // Clear bits
    xEventGroupClearBits(event_group_handle_, AIR780E_UDP_CONNECTED | AIR780E_UDP_DISCONNECTED | AIR780E_UDP_ERROR);

    // Keep data in one line; Use HEX encoding in response
    at_uart_->SendCommand("AT+UDPCFG=\"close/mode\",1;+UDPCFG=\"viewmode\",1;+UDPCFG=\"sendinfo\",1;+UDPCFG=\"dataformat\",0,1");

    // 检查这个 id 是否已经连接
    std::string command = "AT+UDPSTATE=1," + std::to_string(udp_id_);
    at_uart_->SendCommand(command);

    // 断开之前的连接（不触发回调事件）
    if (instance_active_) {
        at_uart_->SendCommand("AT+UDPCLS=" + std::to_string(udp_id_));
        xEventGroupWaitBits(event_group_handle_, AIR780E_UDP_DISCONNECTED, pdTRUE, pdFALSE, UDP_CONNECT_TIMEOUT_MS / portTICK_PERIOD_MS);
        instance_active_ = false;
    }

    // 打开 UDP 连接
    command = "AT+UDPCONNECT=1," + std::to_string(udp_id_) + ",\"UDP\",\"" + host + "\"," + std::to_string(port) + ",0,1";
    if (!at_uart_->SendCommand(command)) {
        ESP_LOGE(TAG, "Failed to open UDP connection");
        return false;
    }

    // 等待连接完成
    auto bits = xEventGroupWaitBits(event_group_handle_, AIR780E_UDP_CONNECTED | AIR780E_UDP_ERROR, pdTRUE, pdFALSE, UDP_CONNECT_TIMEOUT_MS / portTICK_PERIOD_MS);
    if (bits & AIR780E_UDP_ERROR) {
        ESP_LOGE(TAG, "Failed to connect to %s:%d", host.c_str(), port);
        return false;
    }
    return true;
}


void Air780EUdp::Disconnect() {
    if (!instance_active_) {
        return;
    }

    if (at_uart_->SendCommand("AT+UDPCLS=" + std::to_string(udp_id_))) {
        instance_active_ = false;
    }
}

int Air780EUdp::Send(const std::string& data) {
    const size_t MAX_PACKET_SIZE = 1460;

    if (!connected_) {
        ESP_LOGE(TAG, "Not connected");
        return -1;
    }

    if (data.size() > MAX_PACKET_SIZE) {
        ESP_LOGE(TAG, "Data block exceeds maximum limit");
        return -1;
    }

    // 在循环外预先分配command
    std::string command = "AT+UDPSEND=" + std::to_string(udp_id_) + "," + std::to_string(data.size());
    if (!at_uart_->SendCommandWithData(command, 1000, true, data.data(), data.size())) {
        ESP_LOGE(TAG, "Failed to send command");
        return -1;
    }

    auto bits = xEventGroupWaitBits(event_group_handle_, AIR780E_UDP_SEND_COMPLETE | AIR780E_UDP_SEND_FAILED, pdTRUE, pdFALSE, pdMS_TO_TICKS(UDP_CONNECT_TIMEOUT_MS));
    if (bits & AIR780E_UDP_SEND_FAILED) {
        ESP_LOGE(TAG, "Failed to send data");
        return -1;
    } else if (!(bits & AIR780E_UDP_SEND_COMPLETE)) {
        ESP_LOGE(TAG, "Send timeout");
        return -1;
    }

    return data.size();
}

int Air780EUdp::GetLastError() {
    return last_error_;
}