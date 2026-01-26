#ifndef _AIR780E_AT_MODEM_H_
#define _AIR780E_AT_MODEM_H_

#include "at_modem.h"

class Air780EAtModem : public AtModem {
public:
    Air780EAtModem(std::shared_ptr<AtUart> at_uart);
    ~Air780EAtModem() override = default;

    bool SetSleepMode(bool enable, int delay_seconds=0) override;

    // 实现基类的纯虚函数
    std::unique_ptr<Http> CreateHttp(int connect_id) override;
    std::unique_ptr<Tcp> CreateTcp(int connect_id) override;
    std::unique_ptr<Tcp> CreateSsl(int connect_id) override;
    std::unique_ptr<Udp> CreateUdp(int connect_id) override;
    std::unique_ptr<Mqtt> CreateMqtt(int connect_id) override;
    std::unique_ptr<WebSocket> CreateWebSocket(int connect_id) override;

protected:
    void HandleUrc(const std::string& command, const std::vector<AtArgumentValue>& arguments) override;
};


#endif // _AIR780E_AT_MODEM_H_