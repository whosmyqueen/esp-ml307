#ifndef AIR780E_TCP_H
#define AIR780E_TCP_H

#include "tcp.h"
#include "at_uart.h"

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>

#include <list>
#include <mutex>
#include <string>

#define AIR780E_TCP_CONNECTED     BIT0
#define AIR780E_TCP_DISCONNECTED  BIT1
#define AIR780E_TCP_ERROR         BIT2
#define AIR780E_TCP_SEND_COMPLETE BIT3
#define AIR780E_TCP_DATA_AVAILABLE BIT4
#define AIR780E_TCP_RX_HEADER_READY BIT5

#define AIR780E_TCP_CONNECT_TIMEOUT_MS 15000

class Air780ETcp : public Tcp {
public:
    Air780ETcp(std::shared_ptr<AtUart> at_uart, int tcp_id);
    ~Air780ETcp() override;

    bool Connect(const std::string& host, int port) override;
    void Disconnect() override;
    int Send(const std::string& data) override;
    int GetLastError() override;

protected:
    virtual bool ConfigureSsl(int port);

    void StartRxTaskIfNeeded();
    void StopRxTaskIfNeeded();
    void RxTask();
    void ReadAvailableData();

    std::shared_ptr<AtUart> at_uart_;
    int tcp_id_;
    bool instance_active_ = false;
    EventGroupHandle_t event_group_handle_ = nullptr;
    std::list<UrcCallback>::iterator urc_callback_it_;
    TaskHandle_t rx_task_handle_ = nullptr;

    std::mutex rx_mutex_;
    int last_error_ = 0;
    int last_cnlen_ = 0;
    int last_rlen_ = 0;
};

#endif // AIR780E_TCP_H

