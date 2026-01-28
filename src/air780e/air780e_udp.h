#ifndef AIR780E_UDP_H
#define AIR780E_UDP_H

#include "udp.h"
#include "at_uart.h"

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>

#include <list>
#include <mutex>
#include <string>

#define AIR780E_UDP_CONNECTED      BIT0
#define AIR780E_UDP_DISCONNECTED   BIT1
#define AIR780E_UDP_ERROR          BIT2
#define AIR780E_UDP_SEND_COMPLETE  BIT3
#define AIR780E_UDP_DATA_AVAILABLE BIT4
#define AIR780E_UDP_RX_HEADER_READY BIT5

#define AIR780E_UDP_CONNECT_TIMEOUT_MS 15000

class Air780EUdp : public Udp {
public:
    Air780EUdp(std::shared_ptr<AtUart> at_uart, int udp_id);
    ~Air780EUdp() override;

    bool Connect(const std::string& host, int port) override;
    void Disconnect() override;
    int Send(const std::string& data) override;
    int GetLastError() override;

private:
    void StartRxTaskIfNeeded();
    void StopRxTaskIfNeeded();
    void RxTask();
    void ReadAvailableData();

    std::shared_ptr<AtUart> at_uart_;
    int udp_id_;
    bool instance_active_ = false;
    EventGroupHandle_t event_group_handle_ = nullptr;
    std::list<UrcCallback>::iterator urc_callback_it_;
    TaskHandle_t rx_task_handle_ = nullptr;

    std::mutex rx_mutex_;
    int last_error_ = 0;
    int last_cnlen_ = 0;
    int last_rlen_ = 0;
};

#endif // AIR780E_UDP_H

