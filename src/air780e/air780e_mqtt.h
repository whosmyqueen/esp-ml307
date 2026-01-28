#ifndef AIR780E_MQTT_H
#define AIR780E_MQTT_H

#include "mqtt.h"
#include "at_uart.h"

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#include <list>
#include <string>

#define AIR780E_MQTT_CONNECT_TIMEOUT_MS 15000

#define AIR780E_MQTT_TCP_CONNECTED_EVENT BIT0
#define AIR780E_MQTT_CONNECTED_EVENT     BIT1
#define AIR780E_MQTT_DISCONNECTED_EVENT  BIT2
#define AIR780E_MQTT_SUBACK_EVENT        BIT3
#define AIR780E_MQTT_UNSUBACK_EVENT      BIT4
#define AIR780E_MQTT_PUBACK_EVENT        BIT5
#define AIR780E_MQTT_ERROR_EVENT         BIT6

class Air780EMqtt : public Mqtt {
public:
    Air780EMqtt(std::shared_ptr<AtUart> at_uart, int mqtt_id);
    ~Air780EMqtt() override;

    bool Connect(const std::string broker_address, int broker_port, const std::string client_id, const std::string username, const std::string password) override;
    void Disconnect() override;
    bool Publish(const std::string topic, const std::string payload, int qos = 0) override;
    bool Subscribe(const std::string topic, int qos = 0) override;
    bool Unsubscribe(const std::string topic) override;
    bool IsConnected() override;
    int GetLastError() override;

private:
    std::shared_ptr<AtUart> at_uart_;
    int mqtt_id_;
    bool connected_ = false;
    bool mqtt_hex_mode_ = true;
    EventGroupHandle_t event_group_handle_ = nullptr;
    std::list<UrcCallback>::iterator urc_callback_it_;
    int last_error_ = 0;

    static int ParseLeadingInt(const std::string& s);
};

#endif // AIR780E_MQTT_H

