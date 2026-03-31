#include "rtthread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bf0_hal.h"
#include "drv_io.h"
#include "drv_flash.h"
#ifdef RT_USING_DFS_ELMFAT
    #include "dfs_file.h"
#endif
#include "drv_flash.h"
#ifndef FS_REGION_START_ADDR
    #error "Need to define file system start address!"
#else
    #define FS_ROOT "root"
    #define FS_ROOT_PATH "/"
#endif

#ifdef RT_USING_DFS_ELMFAT
int mnt_init(void)
{
    rt_kprintf("0x%x %d\n", FS_REGION_START_ADDR, FS_REGION_SIZE);
    register_mtd_device(FS_REGION_START_ADDR, 0x200000, FS_ROOT);
    if (dfs_mount(FS_ROOT, FS_ROOT_PATH, "elm", 0, 0) == 0) // fs exist
    {
        rt_kprintf("mount fs on flash to root success\n");
    }
    else
    {
        rt_kprintf("mount fs on flash to root fail\n");
        if (dfs_mkfs("elm", FS_ROOT) == 0)
        {
            rt_kprintf("make elm fs on flash sucess, mount again\n");
            if (dfs_mount(FS_ROOT, "/", "elm", 0, 0) == 0)
                rt_kprintf("mount fs on flash success\n");
            else
            {
                rt_kprintf("mount to fs on flash fail\n");
                return RT_ERROR;
            }
        }
        else
        {
            rt_kprintf("dfs_mkfs elm flash fail\n");
            return RT_ERROR;
        }
    }
    return RT_EOK;
}
INIT_ENV_EXPORT(mnt_init);
#endif

/**
  * @brief  Main program
  * @param  None
  * @retval 0 if success, otherwise failure number
  */
int main(void)
{
    /* Infinite loop */
    rt_kprintf("wifi \n");
    while (1)
    {
        rt_thread_mdelay(10000);    // Let system breath.
    }
    return 0;
}
/*
这工程里面wifi驱动与RTT的wlan组件结合在一起了，驱动代码在 customer/peripherals/swt6621s/  目录下
customer/peripherals/swt6621s/port/lwip/wifi_netif.c  里面有驱动与lwip结合的代码
customer/peripherals/swt6621s/sdio/swt6621s_wlan_port.c  里面有驱动与RTT wlan组件结合的代码
customer/peripherals/swt6621s/port/lwip/net.c  里面有驱动与RTT lwip协议栈结合的代码
wifi驱动里面单独创建了m0的网卡，名称可以通过 PKG_USING_WPA_NET_NAME 宏定义来修改，默认是m0 文件在net.c的 net_wlan_init 函数里面创建的
直接使用wlan上的API进行wifi的连接等操作即可，如下：
struct rt_wlan_device *wlan = rt_wlan_dev_get_by_name("wlan0");
rt_wlan_scan(wlan, RT_NULL);    //扫描
rt_wlan_connect(wlan, &sta_info); //连接AP
rt_wlan_disconnect(wlan); //断开连接
开机log：
11-06 14:03:13:434    SDIO: enabling function 1
11-06 14:03:13:436    SDIO: enabled function successfull
11-06 14:03:13:438    SDIO: enabling IRQ for function 1
11-06 14:03:13:439    start to send DRAM boot code to wifi module
11-06 14:03:13:463    start to send IRAM boot code to wifi module
11-06 14:03:13:517    BOOT CP BY SDIO
11-06 14:03:13:795    BOOT CP BY SDIO DONE
11-06 14:03:13:799    skw_calib_download 371 SEEKWAVE_SIZE=2372
11-06 14:03:13:806    MAC Address: 00:00:00:00:00:00
11-06 14:03:13:816    wifi_skw_wpa_supp_dev_init 535
11-06 14:03:13:817    wifi_skw_wpa_supp_dev_init 546
11-06 14:03:13:818    wifi_skw_wpa_supp_dev_init 573
11-06 14:03:13:819    wifi_skw_wpa_supp_dev_init 575 20047834
11-06 14:03:13:820    wpas_init_driver 6531 wpa_s->ifname=m01 drv_priv=2004c4f8
11-06 14:03:14:818    l2_packet_init: iface m01 ifindex 2
11-06 14:03:14:830    get_wpa_s_handle 122 ifname: m01,dev->num=2
11-06 14:03:14:831    wlan_cmd_get_hw_spec 863
11-06 14:03:14:832    wlan_set_regiontable 1641
11-06 14:03:14:833    l2_packet_init: iface m01 ifindex 2
11-06 14:03:14:856    wifi

wlan层的finsh指令：
wifi scan         //扫描
11-06 14:05:18:620    [32m[4137731] I/NO_TAG: scan quiet window: fill tail results 1->15
11-06 14:05:18:625    [0m[32m[4137768] I/NO_TAG: scan quiet window elapsed, emit SCAN_DONE (total=15)
11-06 14:05:18:626    [0m             SSID                      MAC            security    rssi chn Mbps
11-06 14:05:18:626    ------------------------------- -----------------  -------------- ---- --- ----
11-06 14:05:18:627    CMCC-7XL4                       90:98:38:a1:c6:bc  WPA2_AES_PSK   0      1    0
11-06 14:05:18:628    CQHY                            06:05:88:a5:ac:92  WPA2_AES_PSK   0      1    0
11-06 14:05:18:629    CQHY-OFFICE                     0a:05:88:a5:ac:92  WPA2_AES_PSK   0      1    0
11-06 14:05:18:629    sifli-employee                  74:ac:b9:c4:10:fa  WPA2_AES_PSK   0      1    0
11-06 14:05:18:630    sifli-guest                     7a:ac:b9:c4:10:fa  WPA2_AES_PSK   0      1    0
11-06 14:05:18:631    MAXUB                           70:c6:dd:69:ed:14  WPA2_AES_PSK   0      1    0
11-06 14:05:18:632    HEUVAN-I905                     b0:df:c1:b5:a4:39  WPA2_AES_PSK   0      2    0
11-06 14:05:18:632    yunshigaoke                     c8:cd:55:f4:f7:e5  WPA2_AES_PSK   0      3    0
11-06 14:05:18:633    HEUVAN-I906                     b0:df:c1:b5:a4:49  WPA2_AES_PSK   0      3    0
11-06 14:05:18:634    DIRECT-CA-HP M329dw LJ          fa:0d:ac:d7:9b:ca  WPA2_AES_PSK   0      6    0
11-06 14:05:18:634    sifli-employee                  e0:63:da:ba:71:95  WPA2_AES_PSK   0      6    0
11-06 14:05:18:635    sifli-guest                     e6:63:da:ba:71:95  WPA2_AES_PSK   0      6    0
11-06 14:05:18:636    360WiFi-C3089B                  1c:68:7e:c3:08:9b  WPA2_AES_PSK   0      6    0
11-06 14:05:18:637    Tenda_BEC400                    b8:3a:08:be:c4:04  WPA2_AES_PSK   0      6    0
11-06 14:05:18:637    yunshigaoke                     54:16:51:69:ed:ce  WPA2_AES_PSK   0      8    0



wifi join sifli-guest sifli123456  //连接AP

11-06 14:05:40:963    get_wpa_s_handle 122 ifname: m01,dev->num=2
11-06 14:05:40:964    found ssid=360WiFi-C3089B i=13
11-06 14:05:40:965    wlan_cmd_tx_frame 2382 ssid=360WiFi-C3089B sizeof(struct skw_join_param)=25,params=20051494 size=284
11-06 14:05:40:965    wlan_cmd_tx_frame 2400 len=258
11-06 14:05:40:966    wlan_skw_htinfo 2332 ht_oper->field1=5
11-06 14:05:40:968    ctx: 0, bind
11-06 14:05:40:968    invalid lmac id: 0
11-06 14:05:40:979    sme_event_auth 1660 auth_type=0
11-06 14:05:40:980    sme_associate 2040 params=20051494
11-06 14:05:40:981    wlan_set_regiontable 1641
11-06 14:05:40:990    wlan_ops_sta_ioctl: 0x20000
11-06 14:05:40:990    wlan_ops_sta_ioctl: MLAN_IOCTL_BSS
11-06 14:05:40:991    wlan_bss_ioctl 347: sub_command = 131073
11-06 14:05:40:992    wlan_bss_ioctl_start 248
11-06 14:05:40:993    wlan_bss_ioctl_start 258 pmpriv->bss_mode=1
11-06 14:05:40:994    wlan_bss_ioctl_start 296 i=13
11-06 14:05:40:997    wlan_ret_802_11_associate 1280 host_mlme=1
11-06 14:05:40:998    wlan_ret_802_11_associate 1284 hdr->BssId=1c:68:7e:c3:08:9b
11-06 14:05:40:999    wlan_ret_802_11_associate 1285 pmpriv->pattempted_bss_desc->mac_address=1c:68:7e:c3:08:9b
11-06 14:05:41:014    sm_SUPP_PAE_Step 396 startWhen=2,startCount=0,maxStart=3,portValid=0,sm->eapSuccess=0,sm->eapFail=0
11-06 14:05:41:095    sm_SUPP_PAE_Step 396 startWhen=2,startCount=0,maxStart=3,portValid=0,sm->eapSuccess=0,sm->eapFail=0
11-06 14:05:41:106    sm_SUPP_PAE_Step 396 startWhen=2,startCount=0,maxStart=3,portValid=0,sm->eapSuccess=0,sm->eapFail=0
11-06 14:05:41:119    sm_SUPP_PAE_Step 396 startWhen=2,startCount=0,maxStart=3,portValid=1,sm->eapSuccess=0,sm->eapFail=0
11-06 14:05:41:120    sm_SUPP_PAE_Step 396 startWhen=2,startCount=0,maxStart=3,portValid=1,sm->eapSuccess=0,sm->eapFail=0
11-06 14:05:41:122    sm_SUPP_PAE_Step 396 startWhen=2,startCount=0,maxStart=3,portValid=1,sm->eapSuccess=1,sm->eapFail=0
11-06 14:05:41:423    app_cb: WLAN: authenticated to network
11-06 14:05:41:424    get_wpa_s_handle 122 ifname: m01,dev->num=2
11-06 14:05:43:392    [32m[4949420] I/WLAN.mgnt: wifi connect success ssid:360WiFi-C3089B
11-06 14:05:43:396    [0mmsh />msh />app_cb: WLAN: connected to network
11-06 14:05:43:398    Connected to following BSS:
11-06 14:05:43:399    SSID = [360WiFi-C3089B]
11-06 14:05:43:400    IPv4 Address: [192.168.0.3]  //连接成功

ping www.baidu.com
11-06 14:07:06:817 TX:ping www.baidu.com
11-06 14:07:06:895    60 bytes from 153.3.238.127 icmp_seq=0 ttl=53 time=36 ms
11-06 14:07:07:922    60 bytes from 153.3.238.127 icmp_seq=1 ttl=53 time=26 ms
11-06 14:07:08:951    60 bytes from 153.3.238.127 icmp_seq=2 ttl=53 time=27 ms
11-06 14:07:09:978    60 bytes from 153.3.238.127 icmp_seq=3 ttl=53 time=27 ms
11-06 14:07:10:208    msh />msh />

获取天气信息：
11-06 14:07:03:088 TX:weather
11-06 14:07:03:142    DNS lookup succeeded, IP: 116.62.81.138
11-06 14:07:03:227    id:"WM7B0X53DZW2"
11-06 14:07:03:227    name:"??"
11-06 14:07:03:228    country:"CN"
11-06 14:07:03:229    path:"??,??,??"
11-06 14:07:03:230    timezone:"Asia/Shanghai"
11-06 14:07:03:230    timezone_offset:"+08:00"
11-06 14:07:03:231    txt:"?"
11-06 14:07:03:231    code:"30"
11-06 14:07:03:232    temperature:"16"
11-06 14:07:03:233    last_update:"2025-11-06T13:57:41+08:00"
11-06 14:07:03:436    msh />msh />


wifi disc   //断开连接
11-06 14:07:36:977 TX:wifi disc
11-06 14:07:36:980    get_wpa_s_handle 122 ifname: m01,dev->num=2
11-06 14:07:36:980    get_wpa_s_handle 122 ifname: m01,dev->num=2
11-06 14:07:36:991    [32m[1716690] I/WLAN.mgnt: disconnect success!
11-06 14:07:36:992    [0mmsh />msh />app_cb: disconnected


相关的实现可以在 **\rtos\rtthread\components\drivers\wlan\wlan_cmd.c 中找到
每次scan的个数为15个，可以通过修改RT_WLAN_SCAN_RESULT_MAX来调整
*/

