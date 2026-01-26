#ifndef AIR780E_UDP_H
#define AIR780E_UDP_H

#include "udp.h"
#include "at_uart.h"

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#define AIR780E_UDP_CONNECTED BIT0
#define AIR780E_UDP_DISCONNECTED BIT1
#define AIR780E_UDP_ERROR BIT2
#define AIR780E_UDP_SEND_COMPLETE BIT3
#define AIR780E_UDP_SEND_FAILED BIT4
#define AIR780E_UDP_INITIALIZED BIT5

#define UDP_CONNECT_TIMEOUT_MS 10000

class Air780EUdp : public Udp {
public:
    Air780EUdp(std::shared_ptr<AtUart> at_uart, int udp_id);
    ~Air780EUdp();

    bool Connect(const std::string& host, int port) override;
    void Disconnect() override;
    int Send(const std::string& data) override;
    int GetLastError() override;

private:
    std::shared_ptr<AtUart> at_uart_;
    int udp_id_;
    bool instance_active_ = false;
    EventGroupHandle_t event_group_handle_;
    std::list<UrcCallback>::iterator urc_callback_it_;
    int last_error_ = 0;
};

#endif // AIR780E_UDP_H