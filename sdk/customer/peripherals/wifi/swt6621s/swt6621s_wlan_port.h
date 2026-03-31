/*
 * SPDX-FileCopyrightText: 2019-2022 SiFli Technologies(Nanjing) Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __SWT6621S_WLAN_PORT_H__
#define __SWT6621S_WLAN_PORT_H__
/**
 * @brief Default device name for SWT6621S WiFi module.
 *
 * Used to find/open the WiFi device in the system. Can be overridden
 * by defining `SKW_DEV_NAME` before including this header.
 */
#ifndef SKW_DEV_NAME
    #define SKW_DEV_NAME "wifi_swt6621"
#endif
/**
 * @brief Wakeup output GPIO pin number.
 *
 * This pin outputs a short pulse to signal the WiFi module during
 * sleep/wakeup handshakes.
 */
#ifndef WIFI_WAKEUP_OUT_PIN
    #error "Please define WIFI_WAKEUP_OUT_PIN in your board config"
#endif
/**
 * @brief Wakeup input GPIO pin number.
 *
 * This pin is read to detect the WiFi module's wakeup state. Rising-edge
 * interrupts may be attached to this pin.
 */
#ifndef WIFI_WAKEUP_IN_PIN
    #error "Please define WIFI_WAKEUP_IN_PIN in your board config"
#endif
/**
 * @brief WiFi power control GPIO pin number.
 *
 * This pin controls power to the WiFi module (enable/disable) during
 * suspend/resume sequences.
 */
#ifndef WIFI_POWER_PIN
    #error "Please define WIFI_POWER_PIN in your board config"
#endif
/* Suspend/Resume callback definitions */
/**
 * @brief Suspend callback signature.
 * @param arg User-provided argument passed to the callback (optional).
 */
typedef void (*wifi_suspend_cb_t)(void *arg);
/**
 * @brief Resume callback signature.
 * @param arg User-provided argument passed to the callback (optional).
 */
typedef void (*wifi_resume_cb_t)(void *arg);
/**
 * @brief Detect-sleep callback signature.
 * @param arg User-provided argument passed to the callback (optional).
 */
typedef void (*wifi_detect_slp_cb_t)(void *arg);

/**
 * @brief Container for a suspend callback and its argument.
 */
struct wifi_suspend_cb
{
    /** Callback to execute on suspend. */
    wifi_suspend_cb_t cb;
    /** Opaque user argument passed to the callback. */
    void *arg;
};

/**
 * @brief Container for a resume callback and its argument.
 */
struct wifi_resume_cb
{
    /** Callback to execute on resume. */
    wifi_resume_cb_t cb;
    /** Opaque user argument passed to the callback. */
    void *arg;
};
/**
 * @brief Container for a detect-sleep callback and its argument.
 */
struct wifi_detect_slp_cb
{
    /** Callback to execute when detecting sleep/wakeup handshake. */
    wifi_detect_slp_cb_t cb;
    /** Opaque user argument passed to the callback. */
    void *arg;
};
/**
 * @brief Aggregated WiFi control callbacks.
 *
 * Holds suspend, resume, and detect-sleep callbacks that manage the
 * power and handshake flow of the WiFi module.
 */
struct wifi_ctl
{
    struct wifi_suspend_cb suspend;
    struct wifi_resume_cb resume;
    struct wifi_detect_slp_cb detect_slp;
};
/**
 * @brief Initialize the SWT6621S WLAN management layer.
 *
 * Sets up control structures and any required resources for WiFi
 * management. Should be called during application initialization.
 *
 * @return 0 on success; non-zero on failure.
 */
int swt6621s_wlan_mgnt_init(void);

#endif