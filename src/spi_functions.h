#ifndef SPI_FUNCTIONS_H
#define SPI_FUNCTIONS_H
#include <stdint.h>
#include <stddef.h>

struct regs {
    uint8_t address;
    const char *name;
};

extern const struct regs registers[];
extern const size_t num_registers;

int spi_init(void);
uint8_t read_register(uint8_t address);
void write_register(uint8_t address, uint8_t value);
void interrupt_init(void);
void dump_registers(void);

#endif /* SPI_FUNCTIONS_H */