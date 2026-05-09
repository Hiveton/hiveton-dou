# hiveton-dou Global Incremental Optimization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reduce crash, data-loss, concurrency, and build-list drift risks through small, verifiable changes that avoid `sdk/` source edits.

**Architecture:** Work from a persistent backlog, pick one or two low-risk items per 5-minute automation round, and compile `sf32lb52-lcd_n16r8` after every code change. Treat `sdk/` as read-only reference unless the user explicitly approves SDK edits.

**Tech Stack:** RT-Thread, LVGL, SCons, C/C++, SiFli SDK, app-local modules under `app/src`.

---

## Operating Rules

- Do not use destructive git commands.
- Do not commit or push unless explicitly requested.
- Do not modify top navigation or bottom tabbar styles, dimensions, selected assets, or layout unless a task explicitly targets global navigation.
- Do not edit `sdk/` source files; read them only for API behavior.
- After any code change, run:

```bash
cd /Users/hiveton/HivetonCode/hiveton-dou/sdk
source export.sh
cd ../app/project
scons --board=sf32lb52-lcd_n16r8
```

- If compilation fails, stop further fixes and report the first actionable compiler or linker error.
- Keep changes small. Prefer one subsystem per round.
- Update `docs/optimization/global-backlog.md` after each round: mark done, add new risks, record compile result.

## Priority Lanes

### P0: Data Loss / Resource Leak / Use-After-Free

1. TXT reading detail FD leak on page destroy.
2. Pet game save file direct truncation and single write.
3. App config save fallback that can delete/truncate old config.
4. Xiaozhi audio close timeout followed by freeing still-used resources.

### P1: Cross-Thread State / Concurrency

1. EPUB path resolver uses `strtok()` and is not reentrant.
2. Reading cover worker reads shared list state while UI can refresh/destroy it.
3. Sleep manager state is read/written by UI, button, touch wake, and alarm paths without a single guard.
4. Network manager pending fields are multi-field volatile state without transactional protection.
5. `network_mem_ensure_ready()` can race during first allocation.
6. BQ27220 status text read/write is not synchronized.

### P2: UI Consistency / Runtime Lifecycle

1. Pet Rules standard-screen return target points to itself instead of Pet.
2. Language invalidation misses cached pages.
3. Runtime idle timer has no explicit deinit/reset path.
4. Some pages use `653` content height where standard content height is `650`.
5. Hardware-key fallback forcibly makes non-scroll pages scrollable.
6. Several business pages bypass standard shared navigation and need intentional classification or refactor.

### P3: Robustness / Long-Test Observability

1. HTTP global lock lacks owner validation and can starve generic clients if Xiaozhi active stays set.
2. Audio preemption changes owner but does not stop old audio stream.
3. Weather unknown content-length can occupy HTTP lock until webclient timeout.
4. Startup config mount can block up to 12 seconds.
5. BQ27220 stack may be tight with float formatting.
6. MCP TODO placeholder hardware tools should be marked unsupported or implemented.

## Task 1: Reading Detail TXT FD Cleanup

**Files:**
- Modify: `/Users/hiveton/HivetonCode/hiveton-dou/app/src/ui/screens/ui_reading_detail_screen.c`

- [ ] **Step 1: Inspect cleanup order**

Run:

```bash
rg -n "ui_Reading_Detail_screen_destroy|ui_reading_detail_reset_text_source|s_reading_detail_text_fd|load_thread|cancel" app/src/ui/screens/ui_reading_detail_screen.c
```

Expected: Identify whether the load thread is cancelled before page objects and text source are reset.

- [ ] **Step 2: Add destroy cleanup**

In `ui_Reading_Detail_screen_destroy()`, after cancelling background load and before clearing state, call the existing helper that owns the file descriptor:

```c
ui_reading_detail_reset_text_source();
```

Do not manually close `s_reading_detail_text_fd` if the helper already does it.

- [ ] **Step 3: Verify references**

Run:

```bash
rg -n "s_reading_detail_text_fd|ui_reading_detail_reset_text_source" app/src/ui/screens/ui_reading_detail_screen.c
```

Expected: all FD close paths route through the helper.

- [ ] **Step 4: Compile**

Run the standard `sf32lb52-lcd_n16r8` build.

## Task 2: EPUB Path Resolver Reentrancy

**Files:**
- Modify: `/Users/hiveton/HivetonCode/hiveton-dou/app/src/reading/reading_epub.c`

- [ ] **Step 1: Inspect current parser**

Run:

```bash
sed -n '1310,1385p' app/src/reading/reading_epub.c
```

Expected: `reading_epub_resolve_path()` currently uses `strtok()`.

- [ ] **Step 2: Replace `strtok()` with `strtok_r()`**

Keep the existing algorithm and buffers. Use a local `char *save_ptr = NULL;` and replace both tokenizer calls with `strtok_r(..., &save_ptr)`.

- [ ] **Step 3: Scan for remaining nonreentrant tokenizers**

Run:

```bash
rg -n "\bstrtok\s*\(" app/src/reading app/src/ui/screens
```

Expected: no remaining reading-path `strtok()` use unless proven single-threaded and documented.

- [ ] **Step 4: Compile**

Run the standard `sf32lb52-lcd_n16r8` build.

## Task 3: Pet Game Atomic Save

**Files:**
- Modify: `/Users/hiveton/HivetonCode/hiveton-dou/app/src/petgame_storage.c`

- [ ] **Step 1: Add write-all helper**

Add a helper near `petgame_storage_read_file()`:

```c
static bool petgame_storage_write_all(int fd, const char *data, size_t size)
{
    size_t total = 0U;

    while (total < size)
    {
        ssize_t written = write(fd, data + total, size - total);
        if (written <= 0)
        {
            return false;
        }
        total += (size_t)written;
    }

    return true;
}
```

- [ ] **Step 2: Save via temp file**

Change `petgame_storage_save()` to write `petgame.dat.tmp`, call `fsync(fd)`, check `close(fd)`, then `rename(tmp, data_file)`. On any failure, `unlink(tmp)` and leave the old `petgame.dat` intact.

- [ ] **Step 3: Check directory creation**

Stop saving if `petgame_storage_make_dir(games_dir)` or `petgame_storage_make_dir(data_dir)` fails.

- [ ] **Step 4: Compile**

Run the standard `sf32lb52-lcd_n16r8` build.

## Task 4: App Config Safe Save

**Files:**
- Modify: `/Users/hiveton/HivetonCode/hiveton-dou/app/src/config/app_config_storage.c`

- [ ] **Step 1: Inspect fallback**

Run:

```bash
sed -n '130,205p' app/src/config/app_config_storage.c
```

Expected: temp write, `rename`, then destructive fallback.

- [ ] **Step 2: Add `fsync()` before close**

After the temp-file write succeeds, call `fsync(fd)`. If it fails, close and unlink temp.

- [ ] **Step 3: Remove destructive fallback**

Remove the `unlink(path)` plus direct `O_TRUNC` write fallback. If temp rename fails, unlink only the temp file and return failure.

- [ ] **Step 4: Compile**

Run the standard `sf32lb52-lcd_n16r8` build.

## Task 5: Pet Rules Return Target

**Files:**
- Modify: `/Users/hiveton/HivetonCode/hiveton-dou/app/src/ui/screens/ui_pet_rules_screen.c`

- [ ] **Step 1: Inspect standard screen call**

Run:

```bash
sed -n '1,45p' app/src/ui/screens/ui_pet_rules_screen.c
```

- [ ] **Step 2: Change return target**

Change the `ui_build_standard_screen()` current/return target argument from `UI_SCREEN_PET_RULES` to `UI_SCREEN_PET` if the helper parameter is the return target. Confirm against the helper signature before editing.

- [ ] **Step 3: Compile**

Run the standard `sf32lb52-lcd_n16r8` build.

## Task 6: Language Invalidation Completeness

**Files:**
- Modify: `/Users/hiveton/HivetonCode/hiveton-dou/app/src/ui/screens/ui_language_screen.c`

- [ ] **Step 1: Inspect invalidation list**

Run:

```bash
sed -n '1,70p' app/src/ui/screens/ui_language_screen.c
```

- [ ] **Step 2: Add missing cached page destroys**

Add the missing destroy calls only if the pages contain translatable text:

```c
ui_Standby_screen_destroy();
ui_Pet_Rules_screen_destroy();
ui_Wallpaper_screen_destroy();
ui_AI_Weather_Settings_screen_destroy();
```

Keep `ui_Language` reload behavior unchanged.

- [ ] **Step 3: Compile**

Run the standard `sf32lb52-lcd_n16r8` build.

## Task 7: UI Content Height Corrections

**Files:**
- Modify: `/Users/hiveton/HivetonCode/hiveton-dou/app/src/ui/screens/ui_language_screen.c`
- Modify: `/Users/hiveton/HivetonCode/hiveton-dou/app/src/ui/screens/ui_record_list_screen.c`

- [ ] **Step 1: Find hard-coded 653 heights**

Run:

```bash
rg -n "\b653\b" app/src/ui/screens
```

- [ ] **Step 2: Change standard-page content heights to 650**

Only change values that are children of standard `page.content`. Do not alter intentional full-screen fixed layouts.

- [ ] **Step 3: Compile**

Run the standard `sf32lb52-lcd_n16r8` build.

## Task 8: Runtime Hardkey Fallback Respect Non-scroll Pages

**Files:**
- Modify: `/Users/hiveton/HivetonCode/hiveton-dou/app/src/ui/ui_runtime_adapter.c`

- [ ] **Step 1: Inspect fallback**

Run:

```bash
sed -n '740,810p' app/src/ui/ui_runtime_adapter.c
```

- [ ] **Step 2: Avoid adding scrollability**

Change fallback so it scrolls only if the target is already scrollable. Do not call `lv_obj_add_flag(..., LV_OBJ_FLAG_SCROLLABLE)` for pages that deliberately disabled scrolling.

- [ ] **Step 3: Compile**

Run the standard `sf32lb52-lcd_n16r8` build.

## Task 9: BQ27220 Shared Text Synchronization

**Files:**
- Modify: `/Users/hiveton/HivetonCode/hiveton-dou/app/src/bq27220_monitor.c`

- [ ] **Step 1: Inspect status text read/write**

Run:

```bash
rg -n "s_bq27220_status_text|set_status_text|get_status_text|rt_enter_critical" app/src/bq27220_monitor.c
```

- [ ] **Step 2: Guard text copy**

Use the same locking style as the snapshot state. Protect both writer and getter while copying `s_bq27220_status_text`.

- [ ] **Step 3: Compile**

Run the standard `sf32lb52-lcd_n16r8` build.

## Task 10: Network Memory Init Guard

**Files:**
- Modify: `/Users/hiveton/HivetonCode/hiveton-dou/app/src/network/network_mem.c`

- [ ] **Step 1: Inspect init**

Run:

```bash
sed -n '1,120p' app/src/network/network_mem.c
```

- [ ] **Step 2: Add critical section around one-time init**

Guard `s_network_psram_heap_ready` with `rt_enter_critical()` / `rt_exit_critical()` or an RT-Thread mutex. Keep the critical section short.

- [ ] **Step 3: Compile**

Run the standard `sf32lb52-lcd_n16r8` build.

## Task 11: Reading Cover Worker Snapshot

**Files:**
- Modify: `/Users/hiveton/HivetonCode/hiveton-dou/app/src/ui/screens/ui_reading_list_screen.c`

- [ ] **Step 1: Inspect worker inputs**

Run:

```bash
rg -n "s_reading_cover_worker|cover_worker|s_reading_files|worker_indices" app/src/ui/screens/ui_reading_list_screen.c
```

- [ ] **Step 2: Replace index-only handoff with path snapshot**

When scheduling work, copy the selected book paths into a worker-owned array. The worker must not dereference `s_reading_files` after it starts.

- [ ] **Step 3: Compile**

Run the standard `sf32lb52-lcd_n16r8` build.

## Task 12: EPUB Deflate Length Validation

**Files:**
- Modify: `/Users/hiveton/HivetonCode/hiveton-dou/app/src/reading/reading_epub.c`

- [ ] **Step 1: Inspect inflate call**

Run:

```bash
sed -n '1190,1255p' app/src/reading/reading_epub.c
```

- [ ] **Step 2: Validate output length**

After inflate returns success, verify the produced byte count equals `entry->uncompressed_size`. If the miniz/tinf API does not expose produced length, initialize the destination to a sentinel and add a narrow helper if possible; otherwise mark as hardware/file-fixture validation task.

- [ ] **Step 3: Compile**

Run the standard `sf32lb52-lcd_n16r8` build.

## Task 13: HTTP Lock Fairness and Owner Validation

**Files:**
- Modify: `/Users/hiveton/HivetonCode/hiveton-dou/app/src/network/net_http_lock.c`
- Modify: `/Users/hiveton/HivetonCode/hiveton-dou/app/src/xiaozhi/xiaozhi_client_public.c`

- [ ] **Step 1: Inspect lock owner state**

Run:

```bash
sed -n '1,130p' app/src/network/net_http_lock.c
sed -n '240,310p' app/src/xiaozhi/xiaozhi_client_public.c
```

- [ ] **Step 2: Add release-owner validation**

Only release if the caller matches the recorded owner. Log mismatches and avoid corrupting lock state.

- [ ] **Step 3: Bound Xiaozhi lock wait**

Replace `RT_WAITING_FOREVER` with a finite timeout if the call path can safely fail and retry. Keep behavior unchanged if the device registration protocol requires blocking.

- [ ] **Step 4: Compile**

Run the standard `sf32lb52-lcd_n16r8` build.

## Task 14: Audio Preemption Contract

**Files:**
- Modify: `/Users/hiveton/HivetonCode/hiveton-dou/app/src/audio_manager.c`
- Modify: `/Users/hiveton/HivetonCode/hiveton-dou/app/src/music_service.c`
- Modify: `/Users/hiveton/HivetonCode/hiveton-dou/app/src/xiaozhi/xiaozhi_service.c`

- [ ] **Step 1: Define preemption callback**

Extend audio manager with a callback or event so the previous owner can stop its stream before the new owner opens audio devices.

- [ ] **Step 2: Music service handles preempt**

Make MUSIC stop `mp3ctrl` when preempted by XIAOZHI.

- [ ] **Step 3: Compile**

Run the standard `sf32lb52-lcd_n16r8` build.

## Task 15: Sleep Manager State Guard

**Files:**
- Modify: `/Users/hiveton/HivetonCode/hiveton-dou/app/src/sleep_manager.c`
- Modify callers only if required.

- [ ] **Step 1: Inspect shared state**

Run:

```bash
rg -n "s_sleeping|s_standby_pending|sleep_manager_request_wakeup|sleep_manager_request_sleep|net_manager_suspend_for_sleep|net_manager_resume_after_wake" app/src/sleep_manager.c app/src/app_buttons.c app/src/touch_wakeup.c
```

- [ ] **Step 2: Add single guard**

Use critical sections or a mutex to guard state transitions. Ensure network suspend/resume is triggered once per transition, not once per duplicate event.

- [ ] **Step 3: Compile**

Run the standard `sf32lb52-lcd_n16r8` build.

## Long-Test Matrix

Run on hardware after the first P0/P1 batch:

1. Open and close large TXT reading detail 50 times. Watch file descriptors and heap.
2. Open EPUB list while cover worker is active, switch tabs, destroy/recreate reading list repeatedly.
3. Save pet game state, power-cycle during save attempts, verify old state survives.
4. Change app settings during TF card stress, verify config survives.
5. Start music, launch Xiaozhi, interrupt answer, exit Xiaozhi, verify audio ownership and playback.
6. Switch 4G/BT repeatedly for 30 minutes; filter logs for `net_http`, `Weather sync`, `Release mismatch`, `wait thread`.
7. Enter sleep, trigger touch wake plus PWR/B/T rapidly, confirm one wake transition and no repeated network resume.
8. Change language after visiting every UI screen, confirm no stale text on cached pages.

## Automation Loop

Every 5-minute run should:

1. Read `docs/optimization/global-backlog.md`.
2. Pick the highest-priority open item with a small blast radius.
3. Re-read the exact files before editing.
4. Apply one focused patch.
5. Run `sf32lb52-lcd_n16r8` build.
6. If build passes, update backlog status and add any new findings.
7. If build fails, stop and report the failing error.

## Current Automation

Created heartbeat automation:

- ID: `hiveton-dou`
- Schedule: every 5 minutes
- Scope: global incremental scan, low-risk fixes, backlog maintenance, compile after code changes.

