#include "air780e_mqtt.h"

#include <esp_log.h>
#include <freertos/task.h>

#define TAG "Air780EMqtt"

int Air780EMqtt::ParseLeadingInt(const std::string& s) {
    // 兼容 "9 byte" / "9  byte" 这类格式
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
    size_t start = i;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') i++;
    if (i == start) return -1;
    return std::stoi(s.substr(start, i - start));
}

Air780EMqtt::Air780EMqtt(std::shared_ptr<AtUart> at_uart, int mqtt_id)
    : at_uart_(std::move(at_uart)), mqtt_id_(mqtt_id) {
    event_group_handle_ = xEventGroupCreate();

    urc_callback_it_ = at_uart_->RegisterUrcCallback([this](const std::string& command, const std::vector<AtArgumentValue>& arguments) {
        if (command == "CONNECT") {
            // 来自 AT+MIPSTART/AT+SSLMIPSTART 的纯文本 URC：CONNECT OK
            if (arguments.size() >= 1 && (arguments.back().string_value == "OK" || arguments.back().string_value == "ALREADY")) {
                xEventGroupSetBits(event_group_handle_, AIR780E_MQTT_TCP_CONNECTED_EVENT);
            } else {
                xEventGroupSetBits(event_group_handle_, AIR780E_MQTT_ERROR_EVENT);
            }
        } else if (command == "CONNACK") {
            if (arguments.empty() || (arguments.size() >= 1 && arguments[0].string_value == "OK")) {
                connected_ = true;
                xEventGroupSetBits(event_group_handle_, AIR780E_MQTT_CONNECTED_EVENT);
                if (on_connected_callback_) on_connected_callback_();
            } else {
                connected_ = false;
                xEventGroupSetBits(event_group_handle_, AIR780E_MQTT_DISCONNECTED_EVENT);
                if (on_disconnected_callback_) on_disconnected_callback_();
            }
        } else if (command == "SUBACK") {
            xEventGroupSetBits(event_group_handle_, AIR780E_MQTT_SUBACK_EVENT);
        } else if (command == "UNSUBACK") {
            xEventGroupSetBits(event_group_handle_, AIR780E_MQTT_UNSUBACK_EVENT);
        } else if (command == "PUBACK") {
            xEventGroupSetBits(event_group_handle_, AIR780E_MQTT_PUBACK_EVENT);
        } else if (command == "MSUB") {
            // +MSUB: <topic>,<len>,<message>  （MQTTMSGSET=0）
            // +MSUB: <store_addr>            （MQTTMSGSET=1）
            if (arguments.size() >= 3) {
                const std::string& topic = arguments[0].string_value;
                int len = -1;
                if (arguments[1].type == AtArgumentValue::Type::Int) {
                    len = arguments[1].int_value;
                } else {
                    len = ParseLeadingInt(arguments[1].string_value);
                }
                std::string payload = arguments[2].string_value;
                if (mqtt_hex_mode_) {
                    payload = at_uart_->DecodeHex(payload);
                    if (len > 0 && static_cast<int>(payload.size()) > len) {
                        payload.resize(static_cast<size_t>(len));
                    }
                }
                if (on_message_callback_) on_message_callback_(topic, payload);
            }
        }
    });
}

Air780EMqtt::~Air780EMqtt() {
    at_uart_->UnregisterUrcCallback(urc_callback_it_);
    if (event_group_handle_) vEventGroupDelete(event_group_handle_);
}

bool Air780EMqtt::Connect(const std::string broker_address, int broker_port, const std::string client_id,
                          const std::string username, const std::string password) {
    // 如果已经连着，先断开
    if (connected_) {
        Disconnect();
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    xEventGroupClearBits(event_group_handle_,
                         AIR780E_MQTT_TCP_CONNECTED_EVENT |
                         AIR780E_MQTT_CONNECTED_EVENT |
                         AIR780E_MQTT_DISCONNECTED_EVENT |
                         AIR780E_MQTT_SUBACK_EVENT |
                         AIR780E_MQTT_UNSUBACK_EVENT |
                         AIR780E_MQTT_PUBACK_EVENT |
                         AIR780E_MQTT_ERROR_EVENT);

    // MQTT 消息直接上报 + HEX 编码（便于传输二进制 payload）
    mqtt_hex_mode_ = true;
    at_uart_->SendCommand("AT+MQTTMSGSET=0");
    at_uart_->SendCommand("AT+MQTTMODE=1");

    // 配置账号信息（用户名/密码允许为空）
    std::string cfg = "AT+MCONFIG=\"" + client_id + "\",\"" + username + "\",\"" + password + "\"";
    if (!at_uart_->SendCommand(cfg)) {
        last_error_ = at_uart_->GetCmeErrorCode();
        return false;
    }

    // TCP/SSL 链接（MQTT 场景使用 MIPSTART/SSLMIPSTART）
    std::string start_cmd;
    if (broker_port == 8883) {
        // 默认不校验证书（如需证书校验，请先用 FSWRITE + SSLCFG 配好）
        at_uart_->SendCommand("AT+SSLCFG=\"seclevel\",88,0");
        start_cmd = "AT+SSLMIPSTART=\"" + broker_address + "\"," + std::to_string(broker_port);
    } else {
        start_cmd = "AT+MIPSTART=\"" + broker_address + "\"," + std::to_string(broker_port);
    }

    if (!at_uart_->SendCommand(start_cmd)) {
        last_error_ = at_uart_->GetCmeErrorCode();
        return false;
    }

    auto bits = xEventGroupWaitBits(event_group_handle_, AIR780E_MQTT_TCP_CONNECTED_EVENT | AIR780E_MQTT_ERROR_EVENT,
                                    pdTRUE, pdFALSE, pdMS_TO_TICKS(AIR780E_MQTT_CONNECT_TIMEOUT_MS));
    if (!(bits & AIR780E_MQTT_TCP_CONNECTED_EVENT)) {
        return false;
    }

    // MCONNECT：clean_session + keepalive + (可选)长心跳模式
    int keep = keep_alive_seconds_;
    int long_mode = keep > 300 ? 1 : 0;
    std::string conn_cmd = "AT+MCONNECT=1," + std::to_string(keep);
    if (long_mode) conn_cmd += ",1";

    if (!at_uart_->SendCommand(conn_cmd)) {
        last_error_ = at_uart_->GetCmeErrorCode();
        return false;
    }

    bits = xEventGroupWaitBits(event_group_handle_, AIR780E_MQTT_CONNECTED_EVENT | AIR780E_MQTT_DISCONNECTED_EVENT,
                               pdTRUE, pdFALSE, pdMS_TO_TICKS(AIR780E_MQTT_CONNECT_TIMEOUT_MS));
    if (!(bits & AIR780E_MQTT_CONNECTED_EVENT)) {
        return false;
    }
    return true;
}

void Air780EMqtt::Disconnect() {
    if (!connected_) {
        // 仍然尝试清理链路
        at_uart_->SendCommand("AT+MIPCLOSE");
        return;
    }

    at_uart_->SendCommand("AT+MDISCONNECT");
    at_uart_->SendCommand("AT+MIPCLOSE");

    connected_ = false;
    if (on_disconnected_callback_) on_disconnected_callback_();
}

bool Air780EMqtt::Publish(const std::string topic, const std::string payload, int qos) {
    if (!connected_) return false;

    // HEX 模式：message 需要传十六进制字符串
    std::string hex = mqtt_hex_mode_ ? at_uart_->EncodeHex(payload) : payload;
    std::string cmd = "AT+MPUB=\"" + topic + "\"," + std::to_string(qos) + ",0,\"" + hex + "\"";

    xEventGroupClearBits(event_group_handle_, AIR780E_MQTT_PUBACK_EVENT);
    if (!at_uart_->SendCommand(cmd)) {
        last_error_ = at_uart_->GetCmeErrorCode();
        return false;
    }

    if (qos == 1) {
        auto bits = xEventGroupWaitBits(event_group_handle_, AIR780E_MQTT_PUBACK_EVENT, pdTRUE, pdFALSE,
                                        pdMS_TO_TICKS(AIR780E_MQTT_CONNECT_TIMEOUT_MS));
        if (!(bits & AIR780E_MQTT_PUBACK_EVENT)) return false;
    }
    return true;
}

bool Air780EMqtt::Subscribe(const std::string topic, int qos) {
    if (!connected_) return false;
    xEventGroupClearBits(event_group_handle_, AIR780E_MQTT_SUBACK_EVENT);
    std::string cmd = "AT+MSUB=\"" + topic + "\"," + std::to_string(qos);
    if (!at_uart_->SendCommand(cmd)) {
        last_error_ = at_uart_->GetCmeErrorCode();
        return false;
    }
    auto bits = xEventGroupWaitBits(event_group_handle_, AIR780E_MQTT_SUBACK_EVENT, pdTRUE, pdFALSE,
                                    pdMS_TO_TICKS(AIR780E_MQTT_CONNECT_TIMEOUT_MS));
    return (bits & AIR780E_MQTT_SUBACK_EVENT);
}

bool Air780EMqtt::Unsubscribe(const std::string topic) {
    if (!connected_) return false;
    xEventGroupClearBits(event_group_handle_, AIR780E_MQTT_UNSUBACK_EVENT);
    std::string cmd = "AT+MUNSUB=\"" + topic + "\"";
    if (!at_uart_->SendCommand(cmd)) {
        last_error_ = at_uart_->GetCmeErrorCode();
        return false;
    }
    auto bits = xEventGroupWaitBits(event_group_handle_, AIR780E_MQTT_UNSUBACK_EVENT, pdTRUE, pdFALSE,
                                    pdMS_TO_TICKS(AIR780E_MQTT_CONNECT_TIMEOUT_MS));
    return (bits & AIR780E_MQTT_UNSUBACK_EVENT);
}

bool Air780EMqtt::IsConnected() {
    return connected_;
}

int Air780EMqtt::GetLastError() {
    return last_error_;
}

