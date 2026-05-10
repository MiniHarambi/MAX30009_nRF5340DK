#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <errno.h>

#include "spi_functions.h"

#define MAX30009_NODE DT_NODELABEL(max30009)

static const struct gpio_dt_spec cs_gpio =
    SPI_CS_GPIOS_DT_SPEC_GET(MAX30009_NODE);

static const struct gpio_dt_spec int_gpio = {
    .port = DEVICE_DT_GET(DT_NODELABEL(gpio1)),
    .pin = 5,
    .dt_flags = GPIO_ACTIVE_LOW,
};

struct spi_config spi_cfg = {
    .frequency = 1000000,  /* 1 MHz */
    .operation = SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB | SPI_WORD_SET(8),
    .slave = 0,
};
const struct device *spi_bus;
static struct gpio_callback int_cb_data;

const struct regs registers[] = 
{
    { 0x00, "Status 1"              },
    { 0x01, "Status 2"              },
    { 0x08, "FIFO Write Pointer"    },
    { 0x09, "FIFO Read Pointer"     },
    { 0x0A, "FIFO Counter 1"        },
    { 0x0B, "FIFO Counter 2"        },
    { 0x0C, "FIFO Data Register"    },
    { 0x0D, "FIFO Configuration 1"  },
    { 0x0E, "FIFO Configuration 2"  },
    { 0x10, "System Sync"           },
    { 0x11, "System Configuration 1"},
    { 0x12, "Pin Functional Cfg"    },
    { 0x13, "Output Pin Cfg"        },
    { 0x14, "I2C Broadcast Address" },
    { 0x17, "PLL Configuration 1"   },
    { 0x18, "PLL Configuration 2"   },
    { 0x19, "PLL Configuration 3"   },
    { 0x1A, "PLL Configuration 4"   },
    { 0x20, "BioZ Configuration 1"  },
    { 0x21, "BioZ Configuration 2"  },
    { 0x22, "BioZ Configuration 3"  },
    { 0x23, "BioZ Configuration 4"  },
    { 0x24, "BioZ Configuration 5"  },
    { 0x25, "BioZ Configuration 6"  },
    { 0x26, "BioZ Low Threshold"    },
    { 0x27, "BioZ High Threshold"   },
    { 0x28, "BioZ Configuration 7"  },
    { 0x41, "BioZ Mux Cfg 1"        },
    { 0x42, "BioZ Mux Cfg 2"        },
    { 0x43, "BioZ Mux Cfg 3"        },
    { 0x44, "BioZ Mux Cfg 4"        },
    { 0x50, "DC Leads Configuration" },
    { 0x51, "DC Lead Detect Thresh"  },
    { 0x58, "Lead Bias Cfg 1"       },
    { 0x80, "Interrupt Enable 1"    },
    { 0x81, "Interrupt Enable 2"    },
    { 0xFF, "Part ID"               },
};

const size_t num_registers = sizeof(registers) / sizeof(registers[0]);

void cs_select(void)
{
    gpio_pin_set_dt(&cs_gpio, 1);
    k_busy_wait(10);  // Short delay to ensure CS is recognized
}

void cs_deselect(void)
{
    k_busy_wait(10);  // Short delay to ensure last SPI operation completes
    gpio_pin_set_dt(&cs_gpio, 0);
}

int spi_init(void)
{
    spi_bus = DEVICE_DT_GET(DT_BUS(MAX30009_NODE));
    if (!device_is_ready(spi_bus)) {
        printk("Failed to get SPI bus device\n");
        return -ENODEV;
    }

    if (!gpio_is_ready_dt(&cs_gpio)) {
        printk("CS GPIO not ready\n");
        return -ENODEV;
    }
    gpio_pin_configure_dt(&cs_gpio, GPIO_OUTPUT_INACTIVE);

    return 0;
}

/// @brief Loeb MAX30009 registri väärtuse, kasutades SPI transceive'i, kus saadetakse registeri aadress koos lugemisbitiga ja vastu võetakse registeri väärtus
/// @param address Registeri aadress, mida soovitakse lugeda
/// @return Registeri väärtus, mida loeti, või 0xFF, kui lugemine ebaõnnestus
uint8_t read_register(uint8_t address)
{
    uint8_t tx_buf[3] = {address, 0x80, 0xFF};
    uint8_t rx_buf[3] = {0};

    const struct spi_buf tx_spi = {.buf = tx_buf, .len = 3};
    const struct spi_buf rx_spi = {.buf = rx_buf, .len = 3};
    const struct spi_buf_set tx_set = {.buffers = &tx_spi, .count = 1};
    const struct spi_buf_set rx_set = {.buffers = &rx_spi, .count = 1};

    cs_select();
    spi_transceive(spi_bus, &spi_cfg, &tx_set, &rx_set);
    cs_deselect();

    return rx_buf[2];  // The last byte contains the register value
}

/// @brief Kirjutab MAX30009 registri väärtuse, kasutades SPI write'i, kus saadetakse registeri aadress
/// koos kirjutamisbitiga ja väärtus, mida soovitakse kirjutada
/// @param address Registeri aadress, mida soovitakse kirjutada
/// @param value Väärtus, mida soovitakse registerisse kirjutada
void write_register(uint8_t address, uint8_t value)
{
    uint8_t tx_buf[3] = {address, 0x00, value};

    const struct spi_buf tx_spi = {.buf = tx_buf, .len = 3};
    const struct spi_buf_set tx_set = {.buffers = &tx_spi, .count = 1};

    cs_select();
    spi_write(spi_bus, &spi_cfg, &tx_set);
    cs_deselect();
}

static void max30009_int_handler(const struct device *dev,
                                  struct gpio_callback *cb,
                                  uint32_t pins)
{
    /* INT pin fired — FIFO almost full, read data here */
    printk("MAX30009 interrupt triggered\n");

    /* TODO: read FIFO data or set a flag/semaphore 
       to handle it in a thread instead */
    
}

void interrupt_init(void)
{
    if (!gpio_is_ready_dt(&int_gpio)) { 
        printk("INT GPIO not ready\n");
        return;
    }
    gpio_pin_configure_dt(&int_gpio, GPIO_INPUT);
    gpio_pin_interrupt_configure_dt(&int_gpio, GPIO_INT_EDGE_TO_ACTIVE);

    gpio_init_callback(&int_cb_data, max30009_int_handler, BIT(int_gpio.pin));
    gpio_add_callback(int_gpio.port, &int_cb_data);
}