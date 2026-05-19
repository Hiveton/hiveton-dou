# Unified Network Stack Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build one application-facing network API so Bluetooth PAN and 4G CAT1 stay mutually exclusive in `net_manager`, while UI, Xiaozhi, Weather, and OTA consume one unified network state and access path.

**Architecture:** Keep `net_manager` as the only runtime bearer state machine. Add `app_network.h/.c` as the application boundary over `net_manager` and `net_http_lock`, then migrate callers away from direct radio-specific or Xiaozhi-specific network truth. Move shared DNS/internet probing into the network layer after the boundary is in place.

**Tech Stack:** RT-Thread C, SCons via `app/src/SConscript`, existing `net_manager`, `net_http_lock`, lwIP DNS, CAT1 modem, Bluetooth PAN.

---

## File Structure

- Create `app/src/network/app_network.h`: application-level network types, snapshot, mode request, readiness, HTTP ownership, text helpers, network time API.
- Create `app/src/network/app_network.c`: maps `net_manager` snapshots into app-facing snapshots and wraps `net_http_lock`.
- Modify `app/src/SConscript`: add `network/app_network.c`.
- Modify `app/src/ui/ui_status_bar.c`: consume `app_network` snapshot/text instead of duplicating network status decisions.
- Modify `app/src/ui/ui_helpers.c`: consume `app_network` for the top dropdown network state and mode switching.
- Modify `app/src/ui/screens/ui_settings_screen.c`: use `app_network` mode/state helpers for Bluetooth/4G rows.
- Modify `app/src/ui/screens/ui_home_screen.c`: read AI network state through `app_network`.
- Modify `app/src/ui/screens/ui_ai_dou_screen.c`: read AI network state through `app_network` and remove 4G-specific wait copy.
- Modify `app/src/xiaozhi/xiaozhi_client_public.h`: mark `check_internet_access()` as compatibility-only and keep public declarations stable during migration.
- Modify `app/src/xiaozhi/xiaozhi_client_public.c`: route readiness and HTTP ownership through `app_network`.
- Modify `app/src/xiaozhi/weather/weather.c`: route readiness, HTTP ownership, and network time through `app_network`.
- Modify `app/src/network/net_manager.h`: expose any unified internet probe state needed by `app_network` after Task 5.
- Modify `app/src/network/net_manager.c`: own shared DNS/internet probe state in Task 5.

## Task 1: Add Application Network Boundary

**Files:**
- Create: `app/src/network/app_network.h`
- Create: `app/src/network/app_network.c`
- Modify: `app/src/SConscript`

- [ ] **Step 1: Add the public app network header**

Create `app/src/network/app_network.h` with:

```c
#ifndef APP_NETWORK_APP_NETWORK_H
#define APP_NETWORK_APP_NETWORK_H

#include <stdbool.h>
#include <time.h>
#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    APP_NETWORK_MODE_BT = 0,
    APP_NETWORK_MODE_4G,
    APP_NETWORK_MODE_NONE,
} app_network_mode_t;

typedef enum
{
    APP_NETWORK_LINK_NONE = 0,
    APP_NETWORK_LINK_BT_PAN,
    APP_NETWORK_LINK_4G_CAT1,
} app_network_link_t;

typedef enum
{
    APP_NETWORK_CLIENT_GENERIC = 0,
    APP_NETWORK_CLIENT_AI,
    APP_NETWORK_CLIENT_WEATHER,
    APP_NETWORK_CLIENT_OTA,
} app_network_client_t;

typedef enum
{
    APP_NETWORK_STATE_OFFLINE = 0,
    APP_NETWORK_STATE_RADIO_READY,
    APP_NETWORK_STATE_LINK_READY,
    APP_NETWORK_STATE_DNS_READY,
    APP_NETWORK_STATE_INTERNET_READY,
} app_network_state_t;

typedef struct
{
    app_network_mode_t desired_mode;
    app_network_mode_t runtime_mode;
    app_network_link_t active_link;
    app_network_state_t state;
    bool can_use_network;
    bool radios_suspended;
} app_network_snapshot_t;

void app_network_get_snapshot(app_network_snapshot_t *snapshot);
bool app_network_can_run(app_network_client_t client);
rt_err_t app_network_request_mode(app_network_mode_t mode);
rt_err_t app_network_http_begin(app_network_client_t client, rt_int32_t timeout_ms);
void app_network_http_end(app_network_client_t client);
const char *app_network_state_text(app_network_state_t state);
const char *app_network_link_text(app_network_link_t link);
bool app_network_get_network_time(time_t *utc_time);

#ifdef __cplusplus
}
#endif

#endif
```

- [ ] **Step 2: Add the implementation wrapper**

Create `app/src/network/app_network.c` with:

```c
#include "network/app_network.h"

#include "cat1_modem.h"
#include "network/net_http_lock.h"
#include "network/net_manager.h"

static app_network_mode_t app_network_mode_from_net(net_manager_mode_t mode)
{
    if (mode == NET_MANAGER_MODE_4G)
    {
        return APP_NETWORK_MODE_4G;
    }
    if (mode == NET_MANAGER_MODE_BT)
    {
        return APP_NETWORK_MODE_BT;
    }
    return APP_NETWORK_MODE_NONE;
}

static app_network_link_t app_network_link_from_net(net_manager_link_t link)
{
    if (link == NET_MANAGER_LINK_BT_PAN)
    {
        return APP_NETWORK_LINK_BT_PAN;
    }
    if (link == NET_MANAGER_LINK_4G_CAT1)
    {
        return APP_NETWORK_LINK_4G_CAT1;
    }
    return APP_NETWORK_LINK_NONE;
}

static app_network_state_t app_network_state_from_net(net_manager_service_state_t state)
{
    switch (state)
    {
    case NET_MANAGER_SERVICE_INTERNET_READY:
        return APP_NETWORK_STATE_INTERNET_READY;
    case NET_MANAGER_SERVICE_DNS_READY:
        return APP_NETWORK_STATE_DNS_READY;
    case NET_MANAGER_SERVICE_LINK_READY:
        return APP_NETWORK_STATE_LINK_READY;
    case NET_MANAGER_SERVICE_RADIO_READY:
        return APP_NETWORK_STATE_RADIO_READY;
    case NET_MANAGER_SERVICE_OFFLINE:
    default:
        return APP_NETWORK_STATE_OFFLINE;
    }
}

static net_http_client_t app_network_http_client_to_net(app_network_client_t client)
{
    return (client == APP_NETWORK_CLIENT_AI || client == APP_NETWORK_CLIENT_OTA) ?
           NET_HTTP_CLIENT_XIAOZHI :
           NET_HTTP_CLIENT_GENERIC;
}

void app_network_get_snapshot(app_network_snapshot_t *snapshot)
{
    net_manager_snapshot_t net_snapshot;
    net_manager_service_state_t state;

    if (snapshot == RT_NULL)
    {
        return;
    }

    net_manager_get_snapshot(&net_snapshot);
    state = net_manager_get_service_state();

    snapshot->desired_mode = app_network_mode_from_net(net_snapshot.desired_mode);
    snapshot->runtime_mode = app_network_mode_from_net(net_snapshot.runtime_mode);
    snapshot->active_link = app_network_link_from_net(net_snapshot.active_link);
    snapshot->state = app_network_state_from_net(state);
    snapshot->can_use_network = net_manager_internet_ready() && !net_snapshot.radios_suspended;
    snapshot->radios_suspended = net_snapshot.radios_suspended;
}

bool app_network_can_run(app_network_client_t client)
{
    (void)client;
    return net_manager_internet_ready();
}

rt_err_t app_network_request_mode(app_network_mode_t mode)
{
    if (mode == APP_NETWORK_MODE_BT)
    {
        net_manager_request_bt_mode();
        return RT_EOK;
    }
    if (mode == APP_NETWORK_MODE_4G)
    {
        net_manager_request_4g_mode();
        return RT_EOK;
    }
    if (mode == APP_NETWORK_MODE_NONE)
    {
        net_manager_request_none_mode();
        return RT_EOK;
    }
    return -RT_EINVAL;
}

rt_err_t app_network_http_begin(app_network_client_t client, rt_int32_t timeout_ms)
{
    return net_http_lock_take(app_network_http_client_to_net(client), timeout_ms);
}

void app_network_http_end(app_network_client_t client)
{
    net_http_lock_release(app_network_http_client_to_net(client));
}

const char *app_network_state_text(app_network_state_t state)
{
    switch (state)
    {
    case APP_NETWORK_STATE_INTERNET_READY:
        return "网络已连接";
    case APP_NETWORK_STATE_DNS_READY:
        return "DNS已就绪";
    case APP_NETWORK_STATE_LINK_READY:
        return "链路已连接";
    case APP_NETWORK_STATE_RADIO_READY:
        return "网络准备中";
    case APP_NETWORK_STATE_OFFLINE:
    default:
        return "网络未连接";
    }
}

const char *app_network_link_text(app_network_link_t link)
{
    switch (link)
    {
    case APP_NETWORK_LINK_BT_PAN:
        return "蓝牙";
    case APP_NETWORK_LINK_4G_CAT1:
        return "4G";
    case APP_NETWORK_LINK_NONE:
    default:
        return "无网络";
    }
}

bool app_network_get_network_time(time_t *utc_time)
{
    time_t cat1_time;
    app_network_snapshot_t snapshot;

    if (utc_time == RT_NULL)
    {
        return false;
    }

    app_network_get_snapshot(&snapshot);
    if (snapshot.active_link != APP_NETWORK_LINK_4G_CAT1)
    {
        return false;
    }

    if (!cat1_modem_get_network_time(&cat1_time))
    {
        return false;
    }

    *utc_time = cat1_time;
    return true;
}
```

- [ ] **Step 3: Add `app_network.c` to the build**

Modify `app/src/SConscript` and insert the new source after `net_http_lock.c`:

```python
    os.path.join(cwd, 'network', 'net_manager.c'),
    os.path.join(cwd, 'network', 'net_http_lock.c'),
    os.path.join(cwd, 'network', 'app_network.c'),
    os.path.join(cwd, 'network', 'network_mem.c'),
```

- [ ] **Step 4: Run the build**

Run:

```bash
source sdk/export.sh
cd app/project
scons --board=sf32lb52-lcd_n16r8
```

Expected: build succeeds. If it fails with an include path error, verify `app/src/SConscript` still includes `os.path.join(cwd, 'network')` in `cpppath`.

## Task 2: Migrate UI Status Consumers

**Files:**
- Modify: `app/src/ui/ui_status_bar.c`
- Modify: `app/src/ui/ui_helpers.c`
- Modify: `app/src/ui/screens/ui_settings_screen.c`
- Modify: `app/src/ui/screens/ui_home_screen.c`
- Modify: `app/src/ui/screens/ui_ai_dou_screen.c`

- [ ] **Step 1: Replace direct status text in home AI screen**

In `app/src/ui/screens/ui_home_screen.c`, add:

```c
#include "network/app_network.h"
```

Update `home_ai_network_state_text()` to take `app_network_state_t`:

```c
static const char *home_ai_network_state_text(app_network_state_t state)
{
    return ui_i18n_pick(app_network_state_text(state), app_network_state_text(state));
}
```

Update `home_ai_update_network_status()` to read the app snapshot:

```c
static void home_ai_update_network_status(void)
{
    app_network_snapshot_t snapshot;
    xz_service_state_t service_state;
    const char *text;

    if (s_home_ai_network_label == NULL)
    {
        return;
    }

    app_network_get_snapshot(&snapshot);
    service_state = xiaozhi_service_get_state();
    text = home_ai_network_state_text(snapshot.state);

    if (s_home_ai_last_network_state == (net_manager_service_state_t)snapshot.state &&
        s_home_ai_last_service_state == service_state &&
        strncmp(s_home_ai_network_cache, text, sizeof(s_home_ai_network_cache)) == 0)
    {
        return;
    }

    s_home_ai_last_network_state = (net_manager_service_state_t)snapshot.state;
    s_home_ai_last_service_state = service_state;
    home_ai_set_label_if_changed(s_home_ai_network_label,
                                 s_home_ai_network_cache,
                                 sizeof(s_home_ai_network_cache),
                                 text);
}
```

In `home_ai_start_listening()`, replace:

```c
if (!net_manager_can_run_ai())
```

with:

```c
if (!app_network_can_run(APP_NETWORK_CLIENT_AI))
```

- [ ] **Step 2: Replace direct status text in AI Dou screen**

In `app/src/ui/screens/ui_ai_dou_screen.c`, add:

```c
#include "network/app_network.h"
```

Update `ai_ui_network_state_text()` to take `app_network_state_t` and call `app_network_state_text()`.

Replace 4G-specific wait strings in `ai_ui_network_wait_text()` with link-neutral text:

```c
static const char *ai_ui_network_wait_text(void)
{
    app_network_snapshot_t snapshot;

    app_network_get_snapshot(&snapshot);

    switch (snapshot.state)
    {
    case APP_NETWORK_STATE_OFFLINE:
        return ui_i18n_pick("网络未连接，网络恢复后会自动连接小智。",
                            "Network offline. Xiaozhi will connect automatically when the network recovers.");
    case APP_NETWORK_STATE_RADIO_READY:
        return ui_i18n_pick("网络硬件已就绪，正在等待链路完成。",
                            "Network radio is ready. Waiting for link.");
    case APP_NETWORK_STATE_LINK_READY:
        return ui_i18n_pick("网络链路已连接，正在等待DNS和互联网检测。",
                            "Network link is connected. Waiting for DNS and internet check.");
    case APP_NETWORK_STATE_DNS_READY:
        return ui_i18n_pick("DNS已就绪，正在完成小智网络连接。",
                            "DNS is ready. Finishing Xiaozhi network connection.");
    case APP_NETWORK_STATE_INTERNET_READY:
    default:
        return ui_i18n_pick("网络正在准备，小智会在网络恢复后自动连接。",
                            "Network is preparing. Xiaozhi will connect when it recovers.");
    }
}
```

Replace `net_manager_can_run_ai()` calls with `app_network_can_run(APP_NETWORK_CLIENT_AI)`.

- [ ] **Step 3: Migrate settings network actions**

In `app/src/ui/screens/ui_settings_screen.c`, add:

```c
#include "network/app_network.h"
```

Replace calls to `net_manager_request_bt_mode()` and `net_manager_request_4g_mode()` with:

```c
(void)app_network_request_mode(APP_NETWORK_MODE_BT);
```

and:

```c
(void)app_network_request_mode(APP_NETWORK_MODE_4G);
```

Use `app_network_get_snapshot()` for 4G summary decisions while preserving existing user-facing row labels.

- [ ] **Step 4: Migrate dropdown and status bar snapshots**

In `app/src/ui/ui_status_bar.c` and `app/src/ui/ui_helpers.c`, keep existing icon rendering but replace status text and app-level readiness decisions with `app_network_get_snapshot()`.

Do not change image positions, dropdown layout, or bottom/top navigation.

- [ ] **Step 5: Static scan UI layer**

Run:

```bash
rg -n "net_manager_can_run|net_manager_get_service_state|net_manager_internet_ready|cat1_modem_|g_pan_connected|dns_gethostbyname" app/src/ui
```

Expected after this task: UI may still call `net_manager_get_snapshot()` only in components that need low-level icon diagnostics, but no UI file should call `cat1_modem_`, `g_pan_connected`, `dns_gethostbyname`, or client readiness helpers directly.

- [ ] **Step 6: Build**

Run:

```bash
source sdk/export.sh
cd app/project
scons --board=sf32lb52-lcd_n16r8
```

Expected: build succeeds.

## Task 3: Migrate Xiaozhi Network Access

**Files:**
- Modify: `app/src/xiaozhi/xiaozhi_client_public.h`
- Modify: `app/src/xiaozhi/xiaozhi_client_public.c`
- Modify: `app/src/xiaozhi/xiaozhi_service.c`
- Modify: `app/src/xiaozhi/xiaozhi_websocket.c`
- Modify: `app/src/xiaozhi/xiaozhi_mqtt.c`

- [ ] **Step 1: Add app network include**

In Xiaozhi source files that gate network work, add:

```c
#include "network/app_network.h"
```

- [ ] **Step 2: Convert `check_internet_access()` into a compatibility wrapper**

In `app/src/xiaozhi/xiaozhi_client_public.c`, replace the body of `check_internet_access()` with:

```c
int check_internet_access(void)
{
    return app_network_can_run(APP_NETWORK_CLIENT_AI) ? 1 : 0;
}
```

Keep the function declaration in `xiaozhi_client_public.h` during this migration so older callers still compile.

- [ ] **Step 3: Route Xiaozhi HTTP ownership through app network**

In `xz_xiaozhi_http_begin()`, replace direct `net_http_set_xiaozhi_active()` and `net_http_lock_take()` use with:

```c
lock_result = app_network_http_begin(APP_NETWORK_CLIENT_AI, RT_WAITING_FOREVER);
```

In `xz_xiaozhi_http_end()`, replace direct release with:

```c
if (lock_taken)
{
    app_network_http_end(APP_NETWORK_CLIENT_AI);
}
```

Remove direct `net_http_set_xiaozhi_active()` calls from this path.

- [ ] **Step 4: Replace Xiaozhi readiness checks**

Replace:

```c
net_manager_can_run_ai()
```

with:

```c
app_network_can_run(APP_NETWORK_CLIENT_AI)
```

in Xiaozhi files.

- [ ] **Step 5: Static scan Xiaozhi layer**

Run:

```bash
rg -n "net_manager_can_run_ai|net_http_lock_|net_http_set_xiaozhi_active|dns_gethostbyname|check_internet_access\\(" app/src/xiaozhi
```

Expected: `check_internet_access()` remains only as a wrapper definition/declaration or legacy call sites scheduled for removal. Direct `net_http_lock_` and `dns_gethostbyname` should not remain in Xiaozhi application paths after Task 5 is complete.

- [ ] **Step 6: Build**

Run:

```bash
source sdk/export.sh
cd app/project
scons --board=sf32lb52-lcd_n16r8
```

Expected: build succeeds.

## Task 4: Migrate Weather and Network Time

**Files:**
- Modify: `app/src/xiaozhi/weather/weather.c`
- Modify: `app/src/network/app_network.h`
- Modify: `app/src/network/app_network.c`

- [ ] **Step 1: Use app network readiness in weather**

In `weather.c`, include:

```c
#include "network/app_network.h"
```

Replace `weather_network_ready()` with:

```c
static bool weather_network_ready(void)
{
    return app_network_can_run(APP_NETWORK_CLIENT_WEATHER);
}
```

- [ ] **Step 2: Route weather HTTP ownership through app network**

Replace:

```c
net_http_lock_take(NET_HTTP_CLIENT_GENERIC, WEATHER_HTTP_LOCK_TIMEOUT_MS)
```

with:

```c
app_network_http_begin(APP_NETWORK_CLIENT_WEATHER, WEATHER_HTTP_LOCK_TIMEOUT_MS)
```

Replace:

```c
net_http_lock_release(NET_HTTP_CLIENT_GENERIC);
```

with:

```c
app_network_http_end(APP_NETWORK_CLIENT_WEATHER);
```

- [ ] **Step 3: Move CAT1 time behind app network**

Replace direct CAT1 time usage:

```c
if (cat1_modem_get_network_time(&cur_time))
```

with:

```c
if (app_network_get_network_time(&cur_time))
```

Keep the existing China-wall-to-UTC conversion exactly where it is unless testing shows `cat1_modem_get_network_time()` already returns UTC.

- [ ] **Step 4: Static scan weather layer**

Run:

```bash
rg -n "cat1_modem_|net_http_lock_|NET_HTTP_CLIENT|net_manager_can_run_weather" app/src/xiaozhi/weather/weather.c
```

Expected: no direct CAT1 modem or net HTTP lock calls remain in weather.

- [ ] **Step 5: Build**

Run:

```bash
source sdk/export.sh
cd app/project
scons --board=sf32lb52-lcd_n16r8
```

Expected: build succeeds.

## Task 5: Move DNS and Internet Probe Into Network Stack

**Files:**
- Modify: `app/src/network/net_manager.h`
- Modify: `app/src/network/net_manager.c`
- Modify: `app/src/network/app_network.c`
- Modify: `app/src/xiaozhi/xiaozhi_client_public.c`

- [ ] **Step 1: Add probe configuration to `net_manager.c`**

Add near the existing network manager constants:

```c
#define NET_MANAGER_PROBE_HOST              "api.tenclass.net"
#define NET_MANAGER_PROBE_SUCCESS_CACHE_MS  30000U
#define NET_MANAGER_PROBE_FAIL_CACHE_MS      5000U
#define NET_MANAGER_PROBE_WAIT_MS            3000U
```

- [ ] **Step 2: Move DNS cache state from Xiaozhi into `net_manager.c`**

Add state:

```c
static volatile int s_net_probe_done = 0;
static ip_addr_t s_net_probe_addr = {0};
static int s_net_probe_last_result = -1;
static rt_tick_t s_net_probe_last_tick = 0;
```

Include lwIP DNS types in `net_manager.c`:

```c
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
```

- [ ] **Step 3: Implement a shared DNS probe**

Add a private function:

```c
static bool net_manager_probe_internet_locked(void)
{
    rt_tick_t now_tick = rt_tick_get();
    rt_tick_t ttl;
    err_t err;

    if (s_active_link == NET_MANAGER_LINK_NONE || s_radios_suspended)
    {
        s_net_probe_last_result = 0;
        s_net_probe_last_tick = now_tick;
        return false;
    }

    if (s_net_probe_last_result >= 0)
    {
        ttl = rt_tick_from_millisecond(s_net_probe_last_result ?
                                       NET_MANAGER_PROBE_SUCCESS_CACHE_MS :
                                       NET_MANAGER_PROBE_FAIL_CACHE_MS);
        if ((rt_tick_t)(now_tick - s_net_probe_last_tick) < ttl)
        {
            return s_net_probe_last_result != 0;
        }
    }

    s_net_probe_done = 0;
    memset(&s_net_probe_addr, 0, sizeof(s_net_probe_addr));
    err = dns_gethostbyname(NET_MANAGER_PROBE_HOST, &s_net_probe_addr, net_manager_probe_dns_cb, RT_NULL);
    if (err == ERR_OK)
    {
        s_net_probe_last_result = 1;
        s_net_probe_last_tick = now_tick;
        return true;
    }

    s_net_probe_last_result = 0;
    s_net_probe_last_tick = now_tick;
    return false;
}
```

Add the callback:

```c
static void net_manager_probe_dns_cb(const char *name, const ip_addr_t *ipaddr, void *arg)
{
    (void)name;
    (void)arg;

    rt_enter_critical();
    if (ipaddr != RT_NULL)
    {
        s_net_probe_addr = *ipaddr;
        s_net_probe_done = 1;
    }
    else
    {
        s_net_probe_done = -1;
    }
    rt_exit_critical();
}
```

If asynchronous waiting is needed, copy Xiaozhi's existing wait loop into a private `net_manager_probe_wait_done()` helper and use `NET_MANAGER_PROBE_WAIT_MS`.

- [ ] **Step 4: Update service state resolution**

Change `net_manager_update_runtime_state()` so:

```c
network_ready = (link != NET_MANAGER_LINK_NONE) ? 1U : 0U;
dns_ready = network_ready && net_manager_probe_internet_locked() ? 1U : 0U;
internet_ready = dns_ready;
service_state = internet_ready ? NET_MANAGER_SERVICE_INTERNET_READY :
                (network_ready ? NET_MANAGER_SERVICE_LINK_READY :
                 net_manager_resolve_service_state(link));
```

Preserve existing `RADIO_READY` when a bearer is enabled but no active link exists.

- [ ] **Step 5: Remove Xiaozhi DNS ownership**

After the shared probe is working, remove or stop using Xiaozhi-local DNS cache variables and helper functions:

```c
s_dns_lookup_done
s_dns_lookup_addr
xz_dns_lookup_set_done
xz_dns_lookup_get_done
xz_dns_lookup_reset
xz_wait_dns_lookup_done
svr_found_callback
```

Keep `check_internet_access()` as:

```c
int check_internet_access(void)
{
    return app_network_can_run(APP_NETWORK_CLIENT_AI) ? 1 : 0;
}
```

- [ ] **Step 6: Static scan for DNS truth**

Run:

```bash
rg -n "dns_gethostbyname|s_dns_lookup|xz_wait_dns|svr_found_callback|check_internet_access\\(" app/src
```

Expected: `dns_gethostbyname` appears only in `net_manager.c` or other explicitly approved low-level probe code. `check_internet_access()` appears only as a compatibility wrapper and any remaining call sites that simply use the wrapper.

- [ ] **Step 7: Build**

Run:

```bash
source sdk/export.sh
cd app/project
scons --board=sf32lb52-lcd_n16r8
```

Expected: build succeeds.

## Task 6: Final Global Enforcement and Board Checks

**Files:**
- Modify only files required by failures found in scans.

- [ ] **Step 1: Run global direct-access scan**

Run:

```bash
rg -n "cat1_modem_|g_pan_connected|first_pan_connected|dns_gethostbyname|net_http_lock_|net_http_set_xiaozhi_active|net_manager_can_run_ai|net_manager_can_run_weather" app/src
```

Expected allowed direct references:

- `app/src/network/net_manager.c`
- `app/src/network/app_network.c`
- CAT1 modem implementation files outside `app/src` if any
- Bluetooth event plumbing in `app/src/main.c` and `app/src/network/net_manager.c`
- compatibility declaration/wrapper for `check_internet_access()`

- [ ] **Step 2: Run full build**

Run:

```bash
source sdk/export.sh
cd app/project
scons --board=sf32lb52-lcd_n16r8
```

Expected: build succeeds.

- [ ] **Step 3: Manual board verification**

On the target board, verify:

```text
1. Delete or ignore device_config.cfg.
2. Boot device.
3. Confirm Bluetooth is selected, 4G is off, and UI shows one consistent network state.
4. Switch to 4G in settings or top dropdown.
5. Confirm Bluetooth closes before 4G becomes active.
6. Reboot.
7. Confirm saved 4G mode is restored.
8. Switch back to Bluetooth.
9. Confirm CAT1 goes offline before Bluetooth PAN becomes active.
10. Confirm top dropdown, settings, home AI, and AI Dou page show the same state.
11. Confirm Xiaozhi and Weather do not run while app network state is below INTERNET_READY.
```

- [ ] **Step 4: Clean generated status dropdown files**

Run:

```bash
rm -f app/asset/status_dropdown/*.tmp.c app/asset/status_dropdown/*.tmp.o
```

- [ ] **Step 5: Final status**

Run:

```bash
git status --short
```

Expected: only intended source, asset, spec, and plan files are modified or untracked.

## Self-Review

- Spec coverage: Tasks cover boundary creation, UI migration, Xiaozhi migration, Weather/time migration, shared DNS/internet probing, build/static/runtime verification.
- Placeholder scan: no task uses TBD/TODO or defers implementation details without commands or code shape.
- Type consistency: `app_network_mode_t`, `app_network_link_t`, `app_network_client_t`, `app_network_state_t`, and `app_network_snapshot_t` are defined in Task 1 and reused consistently in later tasks.
- Scope control: no automatic fallback between Bluetooth and 4G is introduced; both-radio active mode remains out of scope.
