/*
 * SPDX-FileCopyrightText: 2019-2025 SiFli Technologies(Nanjing) Co, Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "bf0_hal_aon.h"
#include "dfs_file.h"
#include "dfs_posix.h"
#include "drivers/mmcsd_core.h"
#include "drv_io.h"
#include "stdio.h"
#include "usbd_core.h"
#include "usbd_msc.h"


/* Endpoint address */
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

// static rt_device_t g_sd_dev = RT_NULL;
static uint32_t g_block_size = 512;
static uint32_t g_block_num = 0;
static volatile int g_sd_ready = 0;
static rt_device_t msc_dev = RT_NULL;

/*
 * Whole-disk pass-through: expose the underlying mmcsd device ("sd0") directly
 * as a USB MSC device. Previous sub‑partition/chunk logic has been removed.
 * IMPORTANT: Do NOT mount or access this block device locally while it is
 * exported over USB to avoid data corruption.
 */

int mnt_init(void)
{
    uint16_t sdhci_time = 100;
    while (sdhci_time--)
    {
        rt_thread_mdelay(30);
        uint8_t mmcsd_get_stat(void);
        if (mmcsd_get_stat())
            break;
    }
    /* Locate whole block device */
    msc_dev = rt_device_find(CHERRYUSB_DEVICE_MSC_DEVNAME);
    if (msc_dev == RT_NULL)
    {
        rt_kprintf("E: Failed to find block device '%s'.\n", CHERRYUSB_DEVICE_MSC_DEVNAME);
        return -RT_ERROR;
    }
    /* Open device */
    if (rt_device_open(msc_dev, RT_DEVICE_OFLAG_RDWR) != RT_EOK)
    {
        rt_kprintf("E: Failed to open device '%s'.\n", CHERRYUSB_DEVICE_MSC_DEVNAME);
        return -RT_ERROR;
    }
    /* Query geometry */
    struct rt_device_blk_geometry geometry;
    if (rt_device_control(msc_dev, RT_DEVICE_CTRL_BLK_GETGEOME, &geometry) == RT_EOK)
    {
        g_block_num = geometry.sector_count;
        g_block_size = geometry.bytes_per_sector;
        /* Validate block size */
        if (g_block_size == 0)
        {
            rt_kprintf("E: Block size is 0!\n");
            rt_device_close(msc_dev);
            return -RT_ERROR;
        }
        g_sd_ready = 1; /* Set ready flag */
        uint64_t capacity_bytes = (uint64_t)g_block_size * (uint64_t)g_block_num;
        rt_kprintf("MSC: disk '%s' ready (%u blocks * %u bytes = %u MB)\n",
                   CHERRYUSB_DEVICE_MSC_DEVNAME,
                   (unsigned)g_block_num,
                   (unsigned)g_block_size,
                   (unsigned)(capacity_bytes / (1024U * 1024U)));
    }
    else
    {
        rt_kprintf("E: Failed to get geometry for '%s'.\n", CHERRYUSB_DEVICE_MSC_DEVNAME);
        rt_device_close(msc_dev);
        return -RT_ERROR;
    }

    return RT_EOK;
}
INIT_ENV_EXPORT(mnt_init);

void usbd_msc_get_cap(uint8_t busid, uint8_t lun, uint32_t *block_num,
                      uint32_t *block_size)
{
    (void)busid;
    (void)lun;

    if (g_sd_ready)
    {
        *block_num = g_block_num;
        *block_size = g_block_size;
    }
    else
    {
        *block_num = 0;
        *block_size = 0;
    }

}

int usbd_msc_sector_read(uint8_t busid, uint8_t lun, uint32_t sector,
                         uint8_t *buffer, uint32_t length)
{
    (void)busid;
    (void)lun;

    rt_size_t sectors_read;
    uint32_t num_sectors_to_read;

    if (!g_sd_ready)
    {
        return -1; /* Device not ready */
    }

    /* Convert byte length to sector count (rt_device_read works in sectors) */
    num_sectors_to_read = length / g_block_size;

    sectors_read = rt_device_read(msc_dev, sector, buffer, num_sectors_to_read);

    if (sectors_read == num_sectors_to_read)
    {
        return 0; /* OK */
    }
    else
    {
        rt_kprintf("E: sector_read failed, expected %d, got %d\n", num_sectors_to_read, sectors_read);
        return -1; /* Fail */
    }
}

int usbd_msc_sector_write(uint8_t busid, uint8_t lun, uint32_t sector,
                          uint8_t *buffer, uint32_t length)
{
    (void)busid;
    (void)lun;

    rt_size_t sectors_written;
    uint32_t num_sectors_to_write;

    if (!g_sd_ready)
    {
        return -1; /* Device not ready */
    }

    /* Convert byte length to sector count */
    num_sectors_to_write = length / g_block_size;

    sectors_written = rt_device_write(msc_dev, sector, buffer, num_sectors_to_write);

    if (sectors_written == num_sectors_to_write)
    {
        return 0; /* OK */
    }
    else
    {
        rt_kprintf("E: sector_write failed, expected %d, got %d\n", num_sectors_to_write, sectors_written);
        return -1; /* Fail */
    }
}

static struct usbd_interface intf0;

void msc_device_init(uint8_t busid, uint32_t reg_base)
{

    /* Register descriptors and interfaces */

    usbd_desc_register(busid, &msc_device_descriptor);
    usbd_add_interface(busid,
                       usbd_msc_init_intf(busid, &intf0, MSC_OUT_EP, MSC_IN_EP));

    usbd_initialize(busid, reg_base, usbd_event_handler);
}