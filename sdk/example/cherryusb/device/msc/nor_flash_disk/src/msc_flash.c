/*
 * SPDX-FileCopyrightText: 2019-2025 SiFli Technologies(Nanjing) Co, Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "usbd_core.h"
#include "usbd_msc.h"
#include <stdio.h>
#include "bf0_hal.h"
#include "drv_flash.h"
#include "drv_io.h"
#include "stdio.h"

/* Endpoint addresses */
#define MSC_IN_EP 0x85
#define MSC_OUT_EP 0x02

#define USBD_VID 0x38f4
#define USBD_PID 0xFFFF
#define USBD_MAX_POWER 100
#define USBD_LANGID_STRING 1033

/*!< config descriptor size */
#define USB_CONFIG_SIZE (9 + MSC_DESCRIPTOR_LEN)

#define MSC_MAX_MPS 64

static const uint8_t device_descriptor[] =
{
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0x00, 0x00, 0x00, USBD_VID, USBD_PID,
                               0x0200, 0x01),
};

static const uint8_t config_descriptor_fs[] =
{
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x01, 0x01,
                               USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    MSC_DESCRIPTOR_INIT(0x00, MSC_OUT_EP, MSC_IN_EP, MSC_MAX_MPS, 0x02),
};

static const uint8_t device_quality_descriptor[] =
{
    USB_DEVICE_QUALIFIER_DESCRIPTOR_INIT(USB_2_0, 0x00, 0x00, 0x00, 0x01),
};

static const char *string_descriptors[] =
{
    (const char[]){0x09, 0x04}, /* Langid */
    "SiFli",                    /* Manufacturer */
    "SiFli MSC DEMO",           /* Product */
    "2025090317",               /* Serial Number */
};

static const uint8_t *device_descriptor_callback(uint8_t speed)
{
    (void)speed;

    return device_descriptor;
}

static const uint8_t *config_descriptor_callback(uint8_t speed)
{
    (void)speed;

    return config_descriptor_fs;
}

static const uint8_t *device_quality_descriptor_callback(uint8_t speed)
{
    (void)speed;

    return device_quality_descriptor;
}

static const char *string_descriptor_callback(uint8_t speed, uint8_t index)
{
    (void)speed;

    if (index >= (sizeof(string_descriptors) / sizeof(char *)))
    {
        return NULL;
    }
    return string_descriptors[index];
}

const struct usb_descriptor msc_device_descriptor =
{
    .device_descriptor_callback = device_descriptor_callback,
    .config_descriptor_callback = config_descriptor_callback,
    .device_quality_descriptor_callback = device_quality_descriptor_callback,
    .string_descriptor_callback = string_descriptor_callback,
};

static void usbd_event_handler(uint8_t busid, uint8_t event)
{
    (void)busid;

    switch (event)
    {
    case USBD_EVENT_RESET:
        break;
    case USBD_EVENT_CONNECTED:
        break;
    case USBD_EVENT_DISCONNECTED:
        break;
    case USBD_EVENT_RESUME:
        break;
    case USBD_EVENT_SUSPEND:
        break;
    case USBD_EVENT_CONFIGURED:
        break;
    case USBD_EVENT_SET_REMOTE_WAKEUP:
        break;
    case USBD_EVENT_CLR_REMOTE_WAKEUP:
        break;

    default:
        break;
    }
}

#define MSC_BLOCK_SIZE 512
#define FLASH_MSC_START_ADDR (FS_REGION_START_ADDR)
#define FLASH_MSC_TOTAL_SIZE (FS_REGION_SIZE)

#ifndef SPI_NOR_SECT_SIZE
    #define SPI_NOR_SECT_SIZE 4096
#endif

/* Cached flash geometry */
static uint32_t g_flash_total_size = 0;
static uint32_t g_msc_block_num = 0;

/* Media ready flag */
static volatile int g_media_ready = 0;

/* Scratch buffer for one physical flash sector (read-modify-write) */
static uint8_t *g_flash_sector_buffer = RT_NULL;

static int flash_backend_init(void)
{
    /* Validate flash base address */
    FLASH_HandleTypeDef *handle =
        rt_flash_get_handle_by_addr(FLASH_MSC_START_ADDR);
    if (handle == NULL)
    {
        rt_kprintf("MSC: Address 0x%x is not valid NOR FLASH\n",
                   FLASH_MSC_START_ADDR);
        return -1;
    }

    /* Obtain and cache geometry */
    g_flash_total_size = rt_flash_get_total_size(FLASH_MSC_START_ADDR);
    /* Use configured region size */
    if (FLASH_MSC_TOTAL_SIZE > g_flash_total_size)
    {
        rt_kprintf(
            "MSC: Error! MSC region size is larger than flash total size.\n");
        g_flash_total_size = 0;
        return -1;
    }

    g_flash_total_size = FLASH_MSC_TOTAL_SIZE;
    g_msc_block_num = g_flash_total_size / MSC_BLOCK_SIZE;

    /* Allocate sector buffer */
    g_flash_sector_buffer = rt_malloc(SPI_NOR_SECT_SIZE);
    if (g_flash_sector_buffer == NULL)
    {
        rt_kprintf("MSC: Failed to allocate flash sector buffer.\n");
        return -1;
    }

    g_media_ready = 1;
    rt_kprintf("MSC: NOR FLASH ready. Total Size: %d KB, Block Size: %d, Block "
               "Count: %d\n",
               g_flash_total_size / 1024, MSC_BLOCK_SIZE, g_msc_block_num);

    return 0;
}

void usbd_msc_get_cap(uint8_t busid, uint8_t lun, uint32_t *block_num,
                      uint32_t *block_size)
{
    (void)busid;
    (void)lun;

    if (!g_media_ready)
    {
        *block_num = 0;
        *block_size = 0;
    }
    else
    {
        *block_num = g_msc_block_num;
        *block_size = MSC_BLOCK_SIZE;
    }
}

int usbd_msc_sector_read(uint8_t busid, uint8_t lun, uint32_t sector,
                         uint8_t *buffer, uint32_t length)
{
    (void)busid;
    (void)lun;

    if (!g_media_ready)
    {
        return -1; /* Not ready */
    }

    uint32_t read_addr = FLASH_MSC_START_ADDR + sector * MSC_BLOCK_SIZE;

    // rt_flash_read 的 size 是字节数
    int result = rt_flash_read(read_addr, buffer, length);

    if (result == length)
    {
        return 0; /* Success */
    }

    rt_kprintf("E: flash_read failed, expected %d, got %d\n", length, result);
    return -1; /* Error */
}

int usbd_msc_sector_write(uint8_t busid, uint8_t lun, uint32_t sector,
                          uint8_t *buffer, uint32_t length)
{
    if (!g_media_ready)
    {
        return -1; /* Not ready */
    }

    uint32_t bytes_written = 0;
    while (bytes_written < length)
    {
        /* Compute absolute write address */
        uint32_t write_addr =
            FLASH_MSC_START_ADDR + (sector * MSC_BLOCK_SIZE) + bytes_written;

        /* Sector base address */
        uint32_t flash_sector_addr = write_addr & ~(SPI_NOR_SECT_SIZE - 1);

        /* Offset inside sector */
        uint32_t offset_in_sector = write_addr - flash_sector_addr;

        /* Bytes we can write without crossing sector boundary */
        uint32_t bytes_to_write = SPI_NOR_SECT_SIZE - offset_in_sector;
        if (bytes_to_write > (length - bytes_written))
        {
            bytes_to_write = length - bytes_written;
        }

        /* Read-modify-erase-write sequence */
        if (rt_flash_read(flash_sector_addr, g_flash_sector_buffer,
                          SPI_NOR_SECT_SIZE) != SPI_NOR_SECT_SIZE)
        {
            rt_kprintf("E: sector_write - pre-read failed at addr 0x%x\n",
                       flash_sector_addr);
            return -1;
        }

        /* Patch buffer region */
        memcpy(g_flash_sector_buffer + offset_in_sector, buffer + bytes_written,
               bytes_to_write);

        /* Erase physical sector */
        if (rt_flash_erase(flash_sector_addr, SPI_NOR_SECT_SIZE) != 0)
        {
            rt_kprintf("E: sector_write - erase failed at addr 0x%x\n",
                       flash_sector_addr);
            return -1;
        }

        /* Program modified sector */
        if (rt_flash_write(flash_sector_addr, g_flash_sector_buffer,
                           SPI_NOR_SECT_SIZE) != SPI_NOR_SECT_SIZE)
        {
            rt_kprintf("E: sector_write - write-back failed at addr 0x%x\n",
                       flash_sector_addr);
            return -1;
        }

        bytes_written += bytes_to_write;
    }

    return 0; /* Success */
}

static struct usbd_interface intf0;

void msc_device_init(uint8_t busid, uint32_t reg_base)
{

    /* Start FLASH backend (blocking is fine here) */
    if (flash_backend_init() != 0)
    {
        rt_kprintf("MSC backend init failed. USB MSC will not work.\n");
    }

    usbd_desc_register(busid, &msc_device_descriptor);
    usbd_add_interface(busid,
                       usbd_msc_init_intf(busid, &intf0, MSC_OUT_EP, MSC_IN_EP));

    usbd_initialize(busid, reg_base, usbd_event_handler);
}