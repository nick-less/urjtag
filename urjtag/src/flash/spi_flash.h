#ifndef URJ_SRC_SPI_FLASH_H
#define URJ_SRC_SPI_FLASH_H

#include <urjtag/types.h>
#include <urjtag/flash.h>


#define CMD_SPI_WREN    0x06
#define CMD_SPI_WRDI    0x04
#define CMD_SPI_RDSR    0x05
#define CMD_SPI_WRSR    0x01
#define CMD_SPI_READ    0x03
#define CMD_SPI_WRITE   0x02
#define SPI_WIP         0x01

int urj_flash_spi_detect (urj_bus_t *bus, uint32_t adr, urj_flash_cfi_array_t **cfi_array);

extern const urj_flash_driver_t urj_spi_flash_driver;

#endif
