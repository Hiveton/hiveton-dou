/*
 * SPDX-FileCopyrightText: 2019-2025 SiFli Technologies(Nanjing) Co, Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "dfs_file.h"
#include "drv_io.h"
#include "spi_msd.h"
#include "stdio.h"
#include "usbd_core.h"
#include "usbd_msc.h"


/*!< endpoint address */
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

/* Global device handle for SD card */
static rt_device_t g_sd_dev = RT_NULL;

/* Cached SD card geometry */
static uint32_t g_block_size = 0;
static uint32_t g_block_num = 0;
/* Media ready flag */
static volatile int g_sd_ready = 0;

/* Try a single, non-blocking probe/open. Return 0 on success, -1 otherwise */
static int sdcard_try_open_once(void)
{
    struct msd_device *msd;
    if (g_sd_ready)
    {
        return 0;
    }

    if (g_sd_dev == RT_NULL)
    {
        g_sd_dev = rt_device_find("sd0");
        if (g_sd_dev == RT_NULL)
        {
            return -1;
        }
    }

    if (rt_device_open(g_sd_dev, RT_DEVICE_OFLAG_RDWR) != RT_EOK)
    {
        return -1;
    }

    /* Get and cache geometry information */
    msd = (struct msd_device *)g_sd_dev->user_data;
    if (msd == NULL)
    {
        /* If underlying driver hasn't populated yet, close and retry later */
        rt_device_close(g_sd_dev);
        g_sd_dev = RT_NULL;
        return -1;
    }

    g_block_size = msd->geometry.bytes_per_sector;
    g_block_num = msd->geometry.sector_count;
    if (g_block_size == 0 || g_block_num == 0)
    {
        rt_device_close(g_sd_dev);
        g_sd_dev = RT_NULL;
        return -1;
    }

    g_sd_ready = 1;
    rt_kprintf("SD card ready: block %d bytes, total %d blocks, %d MB\n",
               g_block_size, g_block_num,
               (g_block_num / 2048) * (g_block_size / 512));
    return 0;
}

/* Background worker: wait for sd0 to be registered then open once. */
static void sdcard_open_worker(void *parameter)
{
    (void)parameter;
    uint16_t time_out = 100;
    rt_kprintf("MSC: waiting for sd0 ...\n");
    while (time_out-- && !g_sd_ready)
    {
        if (sdcard_try_open_once() == 0)
        {
            break;
        }
        rt_thread_mdelay(10);
    }
    if (!g_sd_ready)
    {
        rt_kprintf("MSC: sd0 not ready, continue without media.\n");
    }
}

/* Start background worker; non-blocking */
static int sdcard_backend_init(void)
{
    rt_thread_t th = rt_thread_create("sd_open", sdcard_open_worker, RT_NULL,
                                      1024, RT_THREAD_PRIORITY_MAX - 3, 10);
    if (th)
    {
        rt_thread_startup(th);
        return 0;
    }
    rt_kprintf("MSC: failed to start sd_open thread.\n");
    return -1;
}

void usbd_msc_get_cap(uint8_t busid, uint8_t lun, uint32_t *block_num,
                      uint32_t *block_size)
{
    (void)busid;
    (void)lun;

    if (!g_sd_ready)
    {
        /* Try a quick lazy probe (non-blocking) */
        (void)sdcard_try_open_once();
    }

    if (!g_sd_ready)
    {
        *block_num = 0;  /* Report no media to host */
        *block_size = 0; /* Host will poll periodically */
    }
    else
    {
        *block_num = g_block_num;
        *block_size = g_block_size;
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
        /* Attempt lazy open; still fail fast if not ready */
        if (sdcard_try_open_once() != 0)
        {
            return -1; /* Not ready */
        }
    }

    num_sectors_to_read = length / g_block_size;

    /* rt_device_read's `pos` is the sector number and `size` is the number of
     * sectors */
    sectors_read = rt_device_read(g_sd_dev, sector, buffer, num_sectors_to_read);

    if (sectors_read == num_sectors_to_read)
    {
        return 0; /* Success */
    }

    rt_kprintf("E: sector_read failed, expected %d, got %d\n",
               num_sectors_to_read, sectors_read);
    return -1; /* Error */
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
        if (sdcard_try_open_once() != 0)
        {
            return -1; /* Not ready */
        }
    }

    num_sectors_to_write = length / g_block_size;

    /* rt_device_write's `pos` is the sector number and `size` is the number of
     * sectors */
    sectors_written =
        rt_device_write(g_sd_dev, sector, buffer, num_sectors_to_write);

    if (sectors_written == num_sectors_to_write)
    {
        return 0; /* Success */
    }

    rt_kprintf("E: sector_write failed, expected %d, got %d\n",
               num_sectors_to_write, sectors_written);
    return -1; /* Error */
}

static struct usbd_interface intf0;

void msc_device_init(uint8_t busid, uint32_t reg_base)
{

    /* Start SD card backend in background (non-blocking) */
    (void)sdcard_backend_init();

    usbd_desc_register(busid, &msc_device_descriptor);
    usbd_add_interface(busid,
                       usbd_msc_init_intf(busid, &intf0, MSC_OUT_EP, MSC_IN_EP));

    usbd_initialize(busid, reg_base, usbd_event_handler);
}