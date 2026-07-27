#include "RN4871.h"
#include <iostream>

RN4871Slave::RN4871Slave(uart_inst_t* uart, uint tx, uint rx, uint baud) 
    : uart_id(uart), tx_pin(tx), rx_pin(rx), baud_rate(baud) {}

RN4871Slave::~RN4871Slave() {
    uart_deinit(uart_id);
}

void RN4871Slave::begin() {
    // Initialise the specified UART instance (uart1)
    uart_init(uart_id, baud_rate);

    // Dynamic pin mapping based on your custom GPIO layout
    gpio_set_function(tx_pin, GPIO_FUNC_UART);
    gpio_set_function(rx_pin, GPIO_FUNC_UART);

    // Explicit hardware configurations matching RN4871 standard parameters
    uart_set_hw_flow(uart_id, false, false);
    uart_set_format(uart_id, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(uart_id, false);
    
    flush_buffer();
}

void RN4871Slave::flush_buffer() {
    while (uart_is_readable(uart_id)) {
        uart_getc(uart_id);
    }
}

void RN4871Slave::send_string(const std::string& data) {
    for (char c : data) {
        uart_putc(uart_id, c);
    }
}

std::string RN4871Slave::read_response(uint32_t timeout_ms) {
    std::string response = "";
    uint32_t total_timeout_us = timeout_ms * 1000;
    uint32_t elapsed_us = 0;
    const uint32_t chunk_us = 50;

    while (elapsed_us < total_timeout_us) {
        if (uart_is_readable_within_us(uart_id, chunk_us)) {
            char c = uart_getc(uart_id);
            response.push_back(c);
            total_timeout_us = elapsed_us + 50000; 
        } else {
            elapsed_us += chunk_us;
        }
    }
    return response;
}

bool RN4871Slave::enter_command_mode() {
    flush_buffer();
    send_string("$$$");
    sleep_ms(200); 
    std::string resp = read_response(500);
    return (resp.find("CMD>") != std::string::npos);
}

bool RN4871Slave::exit_command_mode() {
    std::string dump;
    return send_command("---", dump);
}

bool RN4871Slave::send_command(const std::string& cmd, std::string& out_response) {
    flush_buffer();
    send_string(cmd + "\r\n");
    out_response = read_response(500);
    return true;
}

bool RN4871Slave::setup_slave_mode(const std::string& device_name) {
    std::string response;

    printf("1. Restoring Factory Defaults...\n");
    send_command("SF,1", response);
    sleep_ms(500); 

    if (!enter_command_mode()) {
        printf("Critical Error: Link dropped after factory reset.\n");
        return false;
    }

    printf("2. Setting Identity Name to: %s\n", device_name.c_str());
    send_command("S-," + device_name, response);

    printf("3. Binding Server Bitmap to Transparent Stream mode...\n");
    send_command("SR,2000", response);

    printf("4. Hard Booting Module...\n");
    send_command("R,1", response);
    sleep_ms(600); 

    printf("Success: Module configured into a listening Slave device!\n");
    return true;
}
