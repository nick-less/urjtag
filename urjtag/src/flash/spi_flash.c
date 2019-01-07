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
    uint8_t protect_mask;             //  protection bit mask
    uint32_t protect_size;          // in bytes

    uint16_t page_size;             // in bytes
    uint8_t aai;                    // in bytes (0= not available)
    uint8_t aai_cmd;                    // in bytes (0= not available)
    char name[20];
} flash_device_list[] = {
  {
      0xbf, 0x25, 0x48, 65536, 4096, 0x0c, 32768, 1, 2, 0xAF, "SST25VF512(A)"
  },{
      0xbf, 0x25, 0x49, 131072, 4096, 0x0c, 32768, 1, 2, 0xAF, "SST25VF010A"
  },{
      0xbf, 0x25, 0x4B, 8388608, 4096, 0x0c, 32768, 256, 0, 0, "SST25VF064C"
  },{
      0xbf, 0x25, 0x01, 65536, 4096, 0x0c, 32768, 1, 2, 0xAD, "SST25WF512"
  },{
      0xbf, 0x25, 0x02, 131072, 4096, 0x0c, 32768, 1, 2 ,0xAD, "SST25WF010"
  },{
      0xbf, 0x25, 0x03, 262144, 4096, 0x0c, 65536, 1, 2, 0xAD, "SST25WF020" 
  },{
      0xbf, 0x25, 0x04, 524288, 4096, 0x0c, 65536, 1, 2, 0xAD, "SST25WF040"
  },{
      0xbf, 0x25, 0x05, 1048576, 4096, 0x0c, 65536, 1, 2, 0xAD, "SST25WF080"
  },{
      0xbf, 0x25, 0x41, 2097152, 4096, 0x0c, 65536, 1, 2, 0xAD, "SST25VF016B"
  },{
      0xbf, 0x25, 0x4A, 4194304, 4096, 0x0c, 65536, 1, 2, 0xAD, "SST25VF032B"
  },{
      0xbf, 0x25, 0x8C, 262144, 4096, 0x0c, 65536, 1, 2, 0xAD, "SST25VF020B"
  },{
      0xbf, 0x25, 0x8D, 524288, 4096, 0x0c, 65536, 1, 2, 0xAD, "SST25VF040B"
  },{
      0xbf, 0x25, 0x8E, 1048576, 4096, 0x0c, 65536, 1, 2, 0xAD, "SST25VF080B"
  },{
      0xef, 0x16, 0x40, 8388608, 4096, 0x0e, 65536, 256, 0, 0, "W25Q64FV"
  },{
      0x1f, 0x47, 0x01, 4194304, 4096, 0x0e, 65536, 256, 0, 0, "AT25DF321A"
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

static struct flash_device_t * find_flash_device(urj_flash_cfi_array_t *cfi_array) {
    uint8_t manu, type, id;
    urj_bus_t *bus = cfi_array->bus;

    spi_flash_set_csn ( cfi_array, 0);
    URJ_BUS_WRITE (bus, 0, 0x9f);
    manu = URJ_BUS_READ_NEXT (bus, 0);
    type = URJ_BUS_READ_NEXT (bus, 0);
    id = URJ_BUS_READ_NEXT (bus, 0);
    spi_flash_set_csn ( cfi_array, 1);

    return find_device(manu, type, id);
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

    struct flash_device_t *device = find_flash_device((*cfi_array));
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

        cfi->identification_string.pri_id_code = device->manufacturer;
        cfi->identification_string.pri_vendor_tbl = NULL;
        cfi->identification_string.alt_id_code =  device->device_type<<8 | device->device_id;;
        cfi->identification_string.alt_vendor_tbl = NULL;

        cfi->device_geometry.device_size = device->size;
        cfi->device_geometry.device_interface = 0;    // x 8
        cfi->device_geometry.max_bytes_write = device->page_size;
        cfi->device_geometry.number_of_erase_regions = 1;
        cfi->device_geometry.erase_block_regions =
        malloc (cfi->device_geometry.number_of_erase_regions * sizeof
            (urj_flash_cfi_erase_block_region_t));
        if (!cfi->device_geometry.erase_block_regions) {
            return -2;    /* out of memory */
            }
        cfi->device_geometry.erase_block_regions[0].erase_block_size = device->sector_size;
        cfi->device_geometry.erase_block_regions[0].number_of_erase_blocks = device->size / device->sector_size;
        //Add other details for info
    }
    return 0;
}

static void
spi_flash_print_info (urj_log_level_t ll, urj_flash_cfi_array_t *cfi_array)
{
    struct flash_device_t *device = find_flash_device(cfi_array);

    urj_log (ll, _("spi_flash_print_info\n"));
    if (device != NULL) {
        urj_log (ll, _("Chip: %s\n"), device->name);

    }
 
 

}

static void spi_write_addr (urj_bus_t *bus, uint32_t adr) {
    URJ_BUS_WRITE (bus, 0, (adr >> 16) & 0xff);
    URJ_BUS_WRITE (bus, 0, (adr >> 8) & 0xff);
    URJ_BUS_WRITE (bus, 0, adr & 0xff);
}

static int
spi_flash_erase_block (urj_flash_cfi_array_t *cfi_array, uint32_t adr)
{

//    printf("spi_flash_erase_block %ld\n", adr);
    urj_bus_t *bus = cfi_array->bus;

    spi_flash_wren (cfi_array);
    spi_flash_set_csn (cfi_array, 0);
    URJ_BUS_WRITE (bus, 0, 0x20);
    spi_write_addr(bus, adr);
    spi_flash_set_csn (cfi_array, 1);

    spi_flash_wait_wr_done (cfi_array);


    spi_flash_wrdi (cfi_array);


    return 0;
}

static int
spi_flash_lock_block (urj_flash_cfi_array_t *cfi_array, uint32_t adr)
{
 //   printf("spi_flash_lock_block %ld\n", adr);
    urj_bus_t *bus = cfi_array->bus;

    spi_flash_set_csn (cfi_array, 0);
    URJ_BUS_WRITE (bus, 0, 0x05);
    uint8_t status = URJ_BUS_READ_NEXT (bus, 0);
    spi_flash_set_csn (cfi_array, 1);

//    printf("status 05 %02x\n", status);

    spi_flash_wren (cfi_array);

    spi_flash_set_csn (cfi_array, 0);
    URJ_BUS_WRITE (bus, 0, 0x01);
    URJ_BUS_WRITE (bus, 0, status | 0x0c);
    spi_flash_set_csn (cfi_array, 1);

    spi_flash_wrdi (cfi_array);

    return 0;
}



static int
spi_flash_unlock_block (urj_flash_cfi_array_t *cfi_array, uint32_t adr)
{
 //   printf("spi_flash_unlock_block %ld\n", adr);
        urj_bus_t *bus = cfi_array->bus;

    spi_flash_set_csn (cfi_array, 0);
    URJ_BUS_WRITE (bus, 0, 0x05);
    uint8_t status = URJ_BUS_READ_NEXT (bus, 0);

    spi_flash_set_csn (cfi_array, 1);

 //    printf("status 05 %02x\n", status);

    spi_flash_wren (cfi_array);
    spi_flash_set_csn (cfi_array, 0);
    URJ_BUS_WRITE (bus, 0, 0x01);
    URJ_BUS_WRITE (bus, 0, status & !0x0c);
    spi_flash_set_csn (cfi_array, 1);
    spi_flash_wrdi (cfi_array);



    return 0;
}

inline void spi_flash_set_csn (urj_flash_cfi_array_t *cfi_array, uint8_t csn)
{
    if (csn) {
        URJ_BUS_DISABLE (cfi_array->bus);
    } else {
        URJ_BUS_ENABLE (cfi_array->bus);
    }
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
    while (reg & SPI_WIP) {
        reg = URJ_BUS_READ_NEXT (bus, 0);
    }
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
    uint32_t i=0;
    urj_bus_t *bus = cfi_array->bus;

//    printf("spi_flash_write_page %d %p %d %x\n", adr, buffer, count, buffer[0]);
    

     spi_flash_wren (cfi_array);
     for (i = 0; i < count; i++) {
            spi_flash_set_csn (cfi_array, 0);
            URJ_BUS_WRITE (bus, 0, CMD_SPI_WRITE);
            spi_write_addr(bus, adr+i);
            URJ_BUS_WRITE (bus, 0, buffer[i]);
            spi_flash_set_csn (cfi_array, 1);
     }
     spi_flash_wrdi (cfi_array);

    return count;
}

/**
 * some SST device have a special multi byte programming mode called auto address increment
 * we use this is possible to save some time (saves 24bit per byte)
 *
 */
static int spi_flash_write_aai (urj_flash_cfi_array_t *cfi_array, uint32_t adr, uint32_t *buffer, int count, uint8_t cmd) {
    uint32_t i=0;
    urj_bus_t *bus = cfi_array->bus;

//    printf("spi_flash_write_aai %d %p %d %x\n", adr, buffer, count, buffer[0]);
    
    spi_flash_wren (cfi_array);
    spi_flash_set_csn (cfi_array, 0);
    URJ_BUS_WRITE (bus, 0, cmd);
    spi_write_addr(bus, adr);
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
    uint32_t i = 0;
    uint32_t num_bytes=0;
//    printf("spi_flash_program %d %p %d pagesize %d \n", adr, buffer, count, cfi_array->cfi_chips[0]->cfi.device_geometry.max_bytes_write);

    struct flash_device_t *device = find_flash_device(cfi_array);
    if (device == NULL) {
        return -1;
    }
    if ((device ->aai != 0) && (device->aai>device->page_size)) {

        urj_log (URJ_LOG_LEVEL_NORMAL, _("using word mode programming\n"));
        for (i = 0; i < count; ) {
            num_bytes = spi_flash_write_aai (cfi_array, adr + i, &buffer[i], device->aai, device->aai_cmd);
            i += device->aai;
            spi_flash_wait_wr_done (cfi_array);
        }

    } else {
        urj_log (URJ_LOG_LEVEL_NORMAL, _("using %s mode programming\n"), device->page_size==1?"byte":"page");
        for (i = 0; i < count; ) {
            num_bytes = spi_flash_write_page (cfi_array, adr + i, &buffer[i], device->page_size);
            i += num_bytes;
            spi_flash_wait_wr_done (cfi_array);
        }

    }



    return 0;
}

static int
spi_flash_autodetect (urj_flash_cfi_array_t *cfi_array)
{
    urj_bus_area_t area;

    if (URJ_BUS_AREA (cfi_array->bus, cfi_array->address, &area) != URJ_STATUS_OK)
        return 0;
    return (area.width == 8);
}

static void
spi_flash_readarray (urj_flash_cfi_array_t *cfi_array)
{
    /* Read Array */
//  printf("spi_flash_readarray\n");

}


const urj_flash_driver_t urj_spi_flash_driver = {
    N_("SPI Flash"),
    N_("supported: Standard SPI flash, 1 x 8 bit"),
    1, /* buswidth */
    spi_flash_autodetect,
    spi_flash_print_info,
    spi_flash_erase_block,
    spi_flash_lock_block,
    spi_flash_unlock_block,
    spi_flash_program,
    spi_flash_readarray,
};
