#ifndef AIR780E_SSL_H
#define AIR780E_SSL_H

#include "tcp.h"
#include "at_uart.h"

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#define AIR780E_SSL_CONNECTED BIT0
#define AIR780E_SSL_DISCONNECTED BIT1
#define AIR780E_SSL_ERROR BIT2
#define AIR780E_SSL_SEND_COMPLETE BIT3
#define AIR780E_SSL_SEND_FAILED BIT4
#define AIR780E_SSL_INITIALIZED BIT5

#define SSL_CONNECT_TIMEOUT_MS 10000

class Air780ESsl : public Tcp {
public:
    Air780ESsl(std::shared_ptr<AtUart> at_uart, int ssl_id);
    ~Air780ESsl();

    bool Connect(const std::string& host, int port) override;
    void Disconnect() override;
    int Send(const std::string& data) override;
    int GetLastError() override;

private:
    std::shared_ptr<AtUart> at_uart_;
    int ssl_id_;
    bool instance_active_ = false;
    EventGroupHandle_t event_group_handle_;
    std::list<UrcCallback>::iterator urc_callback_it_;
    int last_error_ = 0;
};

#endif // AIR780E_SSL_H