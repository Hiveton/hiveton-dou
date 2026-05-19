# Unified Network Stack Design

## Goal

Build one authoritative network stack for the device so Bluetooth PAN and 4G CAT1 are mutually exclusive, consistently represented, and consumed by every app through the same application-facing API.

The target behavior is:

- Only one bearer can run at a time: Bluetooth PAN or 4G CAT1.
- Boot reads the saved network mode from the config file.
- If no valid saved mode exists, the default is Bluetooth on and 4G off.
- All application code uses one network API for readiness, mode switching, status text, and HTTP/network ownership.
- UI, Xiaozhi, Weather, OTA, and future apps do not directly infer network truth from 4G, Bluetooth, PAN, lwIP, or DNS internals.

## Current State

`app/src/network/net_manager.c` is already the closest thing to a unified network state machine. It owns:

- `NET_MANAGER_MODE_BT` and `NET_MANAGER_MODE_4G`
- mutually exclusive `s_bt_enabled` and `s_4g_enabled`
- active link resolution through `NET_MANAGER_LINK_BT_PAN` and `NET_MANAGER_LINK_4G_CAT1`
- status refresh dispatch
- boot mode persistence through `app_config_get_boot_network_mode()` and `app_config_set_boot_network_mode()`

The current leaks are:

- `xiaozhi_client_public.c` owns `check_internet_access()` and performs DNS readiness checks as Xiaozhi-specific truth.
- `weather.c` uses `net_manager_can_run_weather()` for HTTP but still reaches into CAT1 for network time.
- UI files directly interpret `net_manager` details in multiple places, causing duplicated status text and mode logic.
- `net_http_lock` is a useful shared HTTP mutex, but it is not yet a complete application-level network access API.
- Low-level includes such as lwIP, CAT1, and Bluetooth headers are visible in application modules.

## Architecture

Use a three-layer model.

### Layer 1: Bearer Drivers

Bearer drivers are low-level adapters only.

- Bluetooth PAN: receives BT stack, ACL, HID, PAN events and exposes PAN readiness to the network manager.
- 4G CAT1: owns modem online/offline, readiness, network time, and modem-specific diagnostics.
- lwIP/DNS/HTTP primitives stay low-level and are not called directly by UI or business applications.

Application modules must not choose or coordinate bearers directly.

### Layer 2: Unified Network Manager

`net_manager` remains the single source of truth for runtime network state.

Responsibilities:

- Load boot network mode from config.
- Default to Bluetooth mode if config is missing or invalid.
- Switch between Bluetooth PAN and 4G CAT1.
- Enforce mutual exclusion during normal operation, transitions, sleep, and wake.
- Track desired mode, runtime mode, active link, radio readiness, link readiness, DNS readiness, and internet readiness.
- Publish one snapshot for UI and application logic.
- Trigger side effects only when the unified network state changes.

`net_manager` should not expose raw implementation details as application policy. It can expose them in diagnostics, but app decisions should use higher-level readiness and status APIs.

### Layer 3: Application Network API

Create a small application-facing module:

- `app/src/network/app_network.h`
- `app/src/network/app_network.c`

This module wraps `net_manager` and `net_http_lock` and becomes the only API used by UI, Xiaozhi, Weather, OTA, and future applications.

Required API shape:

```c
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
```

The exact implementation can keep using `net_manager` internally. The important boundary is that applications depend on `app_network`, not on radio-specific details.

## State Model

The unified state machine uses these meanings:

- `OFFLINE`: no usable radio or suspended during sleep.
- `RADIO_READY`: the selected bearer is enabled or starting but has no usable IP link.
- `LINK_READY`: the selected bearer has a usable link.
- `DNS_READY`: DNS check for the current active link has succeeded.
- `INTERNET_READY`: internet check for the current active link has succeeded.

Bluetooth PAN and 4G CAT1 should follow the same readiness pipeline. A bearer-specific ready flag is not enough to set `INTERNET_READY`.

Current behavior can initially map BT PAN ready and CAT1 ready to `LINK_READY`, then add a shared DNS/internet probe in a second task. The end state must make `app_network_can_run()` depend on unified internet readiness, not on a Xiaozhi-local check.

## Mode Persistence

`app_config` stores only the desired boot mode:

```text
boot.network_mode=bt
```

Valid values:

- `bt`
- `4g`

Rules:

- Missing file: default to `bt`.
- Invalid value: sanitize to `bt`.
- User switch to Bluetooth: save `bt`.
- User switch to 4G: save `4g`.
- Sleep mode does not overwrite the saved boot mode.
- Wake restores the pre-sleep desired mode. If that mode is invalid, restore Bluetooth.

## Application Rules

Application modules must follow these rules:

- Use `app_network_can_run(APP_NETWORK_CLIENT_AI)` before Xiaozhi connects, registers, or sends audio.
- Use `app_network_can_run(APP_NETWORK_CLIENT_WEATHER)` before weather HTTP.
- Use `app_network_can_run(APP_NETWORK_CLIENT_OTA)` before OTA HTTP.
- Use `app_network_http_begin()` and `app_network_http_end()` around HTTP work.
- Use `app_network_get_snapshot()` for UI and status labels.
- Do not call `cat1_modem_is_ready()`, `cat1_modem_get_network_time()`, `g_pan_connected`, Bluetooth stack internals, or `dns_gethostbyname()` from UI or business apps.

Exceptions:

- `net_manager` can call bearer-specific APIs.
- a CAT1 adapter file can call CAT1 APIs.
- a Bluetooth adapter file can call Bluetooth APIs.
- a shared internet probe implementation can call lwIP/DNS internally.

## Migration Plan

### Phase 1: Boundary and Types

Add `app_network.h/.c`, map `net_manager_snapshot_t` into `app_network_snapshot_t`, and route HTTP lock operations through `app_network_http_begin/end`.

No behavior changes beyond the new API.

### Phase 2: UI Consumers

Move status text and mode text into `app_network`.

Update:

- `ui_status_bar.c`
- `ui_helpers.c`
- `ui_settings_screen.c`
- `ui_home_screen.c`
- `ui_ai_dou_screen.c`

These files should stop duplicating Bluetooth/4G readiness text and should read one unified app network snapshot.

### Phase 3: Xiaozhi Consumers

Replace Xiaozhi-local network truth with `app_network`.

`check_internet_access()` should either be removed or become a thin compatibility wrapper over `app_network_can_run(APP_NETWORK_CLIENT_AI)`.

Device registration and OTA queries should use `app_network_http_begin(APP_NETWORK_CLIENT_AI, ...)`.

### Phase 4: Weather and Time

Weather HTTP should use `app_network`.

Network time should be exposed through the network layer instead of direct CAT1 access. The API can be:

```c
bool app_network_get_network_time(time_t *utc_time);
```

The implementation can use CAT1 time only when the active link is 4G. If the active link is Bluetooth, it should return false or use a future Bluetooth-compatible time provider.

### Phase 5: Shared Internet Probe

Move DNS/internet probing out of Xiaozhi into the network stack.

The network stack owns:

- probe hostname selection
- DNS result cache
- failure cooldown
- state transition from `LINK_READY` to `DNS_READY` and `INTERNET_READY`

Applications only observe readiness.

## Testing Strategy

### Static Checks

After each phase, run targeted searches:

```bash
rg -n "cat1_modem_|g_pan_connected|dns_gethostbyname|check_internet_access|net_http_lock_" app/src/ui app/src/xiaozhi app/src/reading app/src/petgame_reducer.c
```

Allowed direct references should shrink to:

- `net_manager.c`
- CAT1 adapter files
- Bluetooth adapter files
- the shared network probe
- compatibility wrappers marked for removal

### Build Check

Run after every code phase:

```bash
source sdk/export.sh
cd app/project
scons --board=sf32lb52-lcd_n16r8
```

### Runtime Checks

Manual board checks:

- Fresh config boot: Bluetooth mode selected, 4G off.
- Saved `boot.network_mode=4g`: boots into 4G mode, Bluetooth off.
- Switch Bluetooth to 4G: Bluetooth closes before 4G is considered active.
- Switch 4G to Bluetooth: CAT1 goes offline before Bluetooth PAN is considered active.
- UI top dropdown, settings page, home AI status, and AI detail page show the same state for the same network condition.
- Weather and Xiaozhi do not run while unified network state is below `INTERNET_READY`.

## Non-Goals

- Do not redesign the Bluetooth stack.
- Do not redesign the CAT1 modem driver.
- Do not introduce automatic fallback between Bluetooth and 4G unless explicitly requested later.
- Do not make both radios active at the same time.
- Do not change UI layout in this refactor except status text source and behavior consistency.

## Risks

- Some legacy modules may rely on `check_internet_access()` side effects and caching. Keep a compatibility wrapper during migration.
- CAT1 time is bearer-specific. Moving it behind `app_network_get_network_time()` keeps the API unified while preserving current functionality.
- The existing network manager is large. Changes should be phased and verified after each phase instead of rewritten in one pass.
