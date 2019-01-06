/*
 * $Id: spi_flash.c 1729 2010-01-24 11:31:51Z jgstroud $
 *
 * Copyright (C) 2002, 2003 ETC s.r.o.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Written by Jon Stroud <jstroud@breakingpoint.com>, 2010.
 */

#include <sysdep.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>     /* usleep */

#include <urjtag/log.h>
#include <urjtag/error.h>
#include <urjtag/flash.h>
#include <urjtag/bus.h>

#include "flash.h"
#include "spi_flash.h"
#include "cfi.h"
#include "amd.h"


inline void spi_flash_set_csn (urj_flash_cfi_array_t *cfi_array, uint8_t csn);
inline uint8_t spi_flash_rdsr (urj_flash_cfi_array_t *cfi_array);
inline void spi_flash_wait_wr_done (urj_flash_cfi_array_t *cfi_array);
inline void spi_flash_wren (urj_flash_cfi_array_t *cfi_array);
inline void spi_flash_wrdi (urj_flash_cfi_array_t *cfi_array);


struct flash_device_t {
    uint8_t manufacturer;             // manufacturer
    uint8_t device_type;             // type of device
    uint8_t device_id;             // id of device
    uint32_t size;                  // in bytes
    uint16_t sector_size;           // in bytes
    uint8_t protect_bits;             // number of protection bits
    uint32_t protect_size;          // in bytes

    uint16_t page_size;             // in bytes
    uint8_t aai;                    // in bytes (0= not available)
    char name[20];
} flash_device_list[] = {
  {
      0xbf, 0x25, 0x48, 65536, 4096, 2, 32768, 1, 1, "SST25VF512(A)"
  },{
      0xbf, 0x25, 0x49, 131072, 4096, 2, 32768, 1, 1, "SST25VF010A"
  },{
      0xbf, 0x25, 0x4B, 8388608, 4096, 2, 32768, 256, 0, "SST25VF064C"
  },{
      0xbf, 0x25, 0x01, 65536, 4096, 2, 32768, 1, 2, "SST25WF512"
  },{
      0xbf, 0x25, 0x02, 131072, 4096, 2, 32768, 1, 2, "SST25WF010"
  },{
      0xbf, 0x25, 0x03, 262144, 4096, 2, 65536, 1, 2, "SST25WF020"
  },{
      0xbf, 0x25, 0x04, 524288, 4096, 2, 65536, 1, 2, "SST25WF040"
  },{
      0xbf, 0x25, 0x05, 1048576, 4096, 2, 65536, 1, 2, "SST25WF080"
  },{
      0xbf, 0x25, 0x41, 2097152, 4096, 2, 65536, 1, 2, "SST25VF016B"
  },{
      0xbf, 0x25, 0x4A, 4194304, 4096, 2, 65536, 1, 2, "SST25VF032B"
  },{
      0xbf, 0x25, 0x8C, 262144, 4096, 2, 65536, 1, 2, "SST25VF020B"
  },{
      0xbf, 0x25, 0x8D, 524288, 4096, 2, 65536, 1, 2, "SST25VF040B"
  },{
      0xbf, 0x25, 0x8E, 1048576, 4096, 2, 65536, 1, 2, "SST25VF080B"
  },{
      0xef, 0x16, 0x40, 8388608, 4096, 2, 65536, 1, 2, "W25Q64FV"
  },
};


static struct flash_device_t * find_device(uint8_t manu, uint8_t type, uint8_t id) {
    
    for (int i=0;i<sizeof(flash_device_list) / sizeof(struct flash_device_t);i++) {
        if ((flash_device_list[i].manufacturer == manu) && 
            (flash_device_list[i].device_type == type) &&
            (flash_device_list[i].device_id == id)) {
                return &flash_device_list[i];
            }
    }
    return NULL;
}

int
urj_flash_spi_detect (urj_bus_t *bus, uint32_t adr, urj_flash_cfi_array_t **cfi_array)
{
    urj_bus_area_t area;
    urj_flash_cfi_query_structure_t *cfi;


    if (!cfi_array || !bus)
        return -1;        /* invalid parameters */

    *cfi_array = calloc ( 1, sizeof (urj_flash_cfi_array_t) );
    if (!*cfi_array)
        return -2;        /* out of memory */

    (*cfi_array)->bus = bus;
    (*cfi_array)->address = 0;
    if (URJ_BUS_AREA(bus, adr + 0, &area) != URJ_STATUS_OK)
        return -8;              /* bus width detection failed */
    if (URJ_BUS_TYPE (bus) != URJ_BUS_TYPE_SPI)
        return URJ_STATUS_FAIL;

    urj_log (URJ_LOG_LEVEL_NORMAL, "SPI Flash\n");

    unsigned int bw = area.width;
    int ba,i;
    if (bw != 8)
        return -3;              /* invalid bus width */
    (*cfi_array)->bus_width = ba = bw / 8;
    (*cfi_array)->cfi_chips = calloc (ba, sizeof (urj_flash_cfi_chip_t *));
    if (!(*cfi_array)->cfi_chips)
        return -2;

    printf("urj_flash_spi_detect %d\n ", ba);
    uint8_t manu, type, id;
    spi_flash_set_csn (  (*cfi_array), 0);
    URJ_BUS_WRITE (bus, 0, 0x9f);
    manu = URJ_BUS_READ_NEXT (bus, 0);
    type = URJ_BUS_READ_NEXT (bus, 0);
    id = URJ_BUS_READ_NEXT (bus, 0);
    spi_flash_set_csn (  (*cfi_array), 1);

    struct flash_device_t *device = find_device(manu, type, id);
    if (device == NULL) {
       return -1; // no device we know
    }

    for ( i=0; i<ba; i++ )
    {
        (*cfi_array)->cfi_chips[i] = calloc (1, sizeof (urj_flash_cfi_chip_t));
        if (!(*cfi_array)->cfi_chips[i])
            return -2;    /* out of memory */
        (*cfi_array)->cfi_chips[i]->width = 1;        //ba;
        cfi = &(*cfi_array)->cfi_chips[i]->cfi;

        cfi->identification_string.pri_id_code = CFI_VENDOR_NULL;
        cfi->identification_string.pri_vendor_tbl = NULL;
        cfi->identification_string.alt_id_code = 0;
        cfi->identification_string.alt_vendor_tbl = NULL;

        cfi->device_geometry.device_size = device->size;
        cfi->device_geometry.device_interface = 0;    // x 8
        cfi->device_geometry.max_bytes_write = device->page_size;
        cfi->device_geometry.number_of_erase_regions = device->size / device->protect_size;
        cfi->device_geometry.erase_block_regions =
        malloc (cfi->device_geometry.number_of_erase_regions * sizeof
            (urj_flash_cfi_erase_block_region_t));
        if (!cfi->device_geometry.erase_block_regions)
            return -2;    /* out of memory */
        for (int r=0;r<cfi->device_geometry.number_of_erase_regions;r++) {
            cfi->device_geometry.erase_block_regions[r].erase_block_size = device->protect_size;
            cfi->device_geometry.erase_block_regions[r].number_of_erase_blocks = 1;
        }
        //Add other details for info
    }
    return 0;
}

static void
spi_flash_print_info (urj_log_level_t ll, urj_flash_cfi_array_t *cfi_array)
{
    urj_log (ll, _("spi_flash_print_info\n"));

    urj_log (ll, _("Chip: SPI Flash\n"));
 

}

static int
spi_flash_erase_block (urj_flash_cfi_array_t *cfi_array, uint32_t adr)
{

    printf("erase %d\n", adr);

    // we dont need to erase blocks with the spi flash
    return 0;
}

static int
spi_flash_unlock_block (urj_flash_cfi_array_t *cfi_array, uint32_t adr)
{
    printf("unlock %d\n", adr);
    // we dont need to unlock blocks with the spi flash
    return 0;
}

inline void spi_flash_set_csn (urj_flash_cfi_array_t *cfi_array, uint8_t csn)
{
    if (csn)
        URJ_BUS_DISABLE (cfi_array->bus);
    else
        URJ_BUS_ENABLE (cfi_array->bus);
}


inline uint8_t spi_flash_rdsr (urj_flash_cfi_array_t *cfi_array)
{
    uint8_t reg;
    urj_bus_t *bus = cfi_array->bus;
    spi_flash_set_csn (cfi_array, 0);
    URJ_BUS_WRITE (bus, 0, CMD_SPI_RDSR);
    reg = URJ_BUS_READ_NEXT (bus, 0);
    spi_flash_set_csn (cfi_array, 1);
    return reg;
}

inline void spi_flash_wait_wr_done (urj_flash_cfi_array_t *cfi_array)
{
    uint8_t reg;
    urj_bus_t *bus = cfi_array->bus;
    spi_flash_set_csn (cfi_array, 0);
    URJ_BUS_WRITE (bus, 0, CMD_SPI_RDSR);
    reg = URJ_BUS_READ_NEXT (bus, 0);
    while (reg & SPI_WIP)
        reg = URJ_BUS_READ_NEXT (bus, 0);
    spi_flash_set_csn (cfi_array, 1);
}

inline void spi_flash_wren (urj_flash_cfi_array_t *cfi_array)
{
    urj_bus_t *bus = cfi_array->bus;
    spi_flash_set_csn (cfi_array, 0);
    URJ_BUS_WRITE (bus, 0, CMD_SPI_WREN);
    spi_flash_set_csn (cfi_array, 1);
}

inline void spi_flash_wrdi (urj_flash_cfi_array_t *cfi_array)
{
    urj_bus_t *bus = cfi_array->bus;
    spi_flash_set_csn (cfi_array, 0);
    URJ_BUS_WRITE (bus, 0, CMD_SPI_WRDI);
    spi_flash_set_csn (cfi_array, 1);
}

static int
spi_flash_write_page (urj_flash_cfi_array_t *cfi_array, uint32_t adr, uint32_t *buffer, int count)
{
    uint32_t i;
    urj_bus_t *bus = cfi_array->bus;

    printf("spi_flash_write_page %d %p %d\n", adr, buffer, count);


    if (count > 128) count = 128;
    spi_flash_wren (cfi_array);
    spi_flash_set_csn (cfi_array, 0);
    URJ_BUS_WRITE_START (bus, adr);
    for (i = 0; i < count; i++) {
        URJ_BUS_WRITE (bus, 0, buffer[i]);
    }
    spi_flash_set_csn (cfi_array, 1);
    spi_flash_wrdi (cfi_array);
    return count;
}

static int
spi_flash_program (urj_flash_cfi_array_t *cfi_array, uint32_t adr, uint32_t *buffer, int count)
{
    uint32_t i;
    uint32_t num_bytes;
    printf("spi_flash_program %d %p %d\n", adr, buffer, count);

    for (i = 0; i < count; )
    {
        num_bytes = spi_flash_write_page (cfi_array, adr + i, &buffer[i], count - i);
        i += num_bytes;
        spi_flash_wait_wr_done (cfi_array);
    }
    return 0;
}

static int
spi_flash_autodetect (urj_flash_cfi_array_t *cfi_array)
{
    urj_bus_area_t area;

    if (URJ_BUS_AREA (cfi_array->bus, cfi_array->address, &area) != URJ_STATUS_OK)
        return 0;

       printf("spi_flash_autodetect\n");

    urj_bus_t *bus = cfi_array->bus;

    uint8_t reg;
    spi_flash_set_csn (cfi_array, 0);
    URJ_BUS_WRITE (bus, 0, 0x9f);
    reg = URJ_BUS_READ_NEXT (bus, 0);
    printf("%x\n", reg);
    reg = URJ_BUS_READ_NEXT (bus, 0);
    printf("%x\n", reg);
    reg = URJ_BUS_READ_NEXT (bus, 0);
    printf("%x\n", reg);
    spi_flash_set_csn (cfi_array, 1);

    return (area.width == 8);
}

static void
spi_flash_readarray (urj_flash_cfi_array_t *cfi_array)
{
    /* Read Array */
}


const urj_flash_driver_t urj_spi_flash_driver = {
    N_("SPI Flash"),
    N_("supported: Standard SPI flash, 1 x 8 bit"),
    1, /* buswidth */
    spi_flash_autodetect,
    spi_flash_print_info,
    spi_flash_erase_block,
    spi_flash_unlock_block,
    spi_flash_program,
    spi_flash_readarray,
};

