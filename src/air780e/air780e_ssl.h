#ifndef AIR780E_SSL_H
#define AIR780E_SSL_H

#include "air780e_tcp.h"

class Air780ESsl : public Air780ETcp {
public:
    Air780ESsl(std::shared_ptr<AtUart> at_uart, int ssl_id);
    ~Air780ESsl() override = default;

protected:
    bool ConfigureSsl(int port) override;
};

#endif // AIR780E_SSL_H

