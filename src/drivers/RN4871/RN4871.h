#ifndef RN4871_SLAVE_H
#define RN4871_SLAVE_H

#include <string>
#include "pico/stdlib.h"
#include "hardware/uart.h"

class RN4871Slave {
private:
    uart_inst_t* uart_id;
    uint tx_pin;
    uint rx_pin;
    uint baud_rate;

public:
    RN4871Slave(uart_inst_t* uart, uint tx, uint rx, uint baud);
    ~RN4871Slave();

    // Hardware Lifecycle
    void begin();

    // Communication Primitives
    void send_string(const std::string& data);
    std::string read_response(uint32_t timeout_ms = 1000);
    void flush_buffer();

    // Core Interaction Modes
    bool enter_command_mode();
    bool exit_command_mode();
    bool send_command(const std::string& cmd, std::string& out_response);

    // Specific Slave Routines
    bool setup_slave_mode(const std::string& device_name);
};

#endif // RN4871_SLAVE_H
