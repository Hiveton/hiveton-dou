/*
 * SPDX-FileCopyrightText: 2019-2022 SiFli Technologies(Nanjing) Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "rtthread.h"
#include "swt6621s_wlan_port.h"

int rt_wifi_init(void)
{
    swt6621s_wlan_mgnt_init();
    return 0;
}
INIT_APP_EXPORT(rt_wifi_init);