# hiveton-dou Global Optimization Backlog

This backlog is the source of truth for recurring 5-minute optimization rounds. Each round should take only a small, low-risk item, compile after code changes, then update this file.

## Status Legend

- `OPEN`: not started.
- `IN_PROGRESS`: currently being edited in this thread.
- `DONE`: fixed and compiled.
- `DEFERRED`: needs hardware, larger design, or explicit user approval.

## Automation

- Active heartbeat automation ID: `hiveton-dou`
- Schedule: every 5 minutes
- Required compile after code changes:

```bash
cd /Users/hiveton/HivetonCode/hiveton-dou/sdk
source export.sh
cd ../app/project
scons --board=sf32lb52-lcd_n16r8
```

## Completed Before This Planning Pass

- `DONE`: `app_buttons` wake-only event consumption scan/fixes verified.
- `DONE`: `reading_cover_cache` temp write + fsync + rename verified.
- `DONE`: `reading_state` safe save with temp/backup verified.
- `DONE`: `weather` HTTP timeout + lock verified.
- `DONE`: `weather` bounded dynamic response buffer added and compiled.
- `DONE`: `audio_manager` getter locking verified.
- `DONE`: `ui_runtime_adapter` delete hook dedupe verified.
- `DONE`: `ui_pet_rules_screen.c` basic build-list consistency verified.
- `DONE`: `ui_status_detail_screen.c` added to UI `filelist.txt` and `CMakeLists.txt`; compiled.
- `DONE`: Reading detail TXT stream FD reset added to `ui_Reading_Detail_screen_destroy()`; compiled.
- `DONE`: EPUB internal path resolver now uses `strtok_r()` instead of `strtok()`; compiled.
- `DONE`: Pet game save now writes a temp file, fsyncs, checks close, and renames into place; compiled.
- `DONE`: App config save now fsyncs temp file and no longer deletes/truncates the old config on rename failure; compiled.
- `DONE`: Pet Rules standard-screen back target now returns to `UI_SCREEN_PET`; compiled.
- `DONE`: BQ27220 status text get/set now uses the existing short critical-section style; compiled.
- `DONE`: Language change invalidation now also destroys Standby, Pet Rules, Wallpaper, and AI Weather Settings cached pages; compiled.
- `DONE`: Network PSRAM heap lazy init now uses a short critical section to avoid double initialization; compiled.
- `DONE`: Standard-screen content containers no longer extend to hard-coded height 653 on Language and Record List pages; compiled.
- `DONE`: Runtime hardkey fallback now only scrolls objects that are already marked scrollable; compiled.
- `DONE`: Runtime deinit now deletes the idle timer and resets runtime navigation state; compiled.
- `DONE`: Reading cover worker now uses worker-owned EPUB path snapshots instead of reading mutable global file indices; compiled.
- `DONE`: MQTT/WebSocket abort JSON messages now use bounded `rt_snprintf()` formatting instead of `strcat()`; compiled.
- `DONE`: EPUB deflate ZIP reads now validate that uzlib produced exactly the declared uncompressed length; compiled.
- `DONE`: HTTP lock release now validates that a lock owner exists and matches the releasing client; compiled.
- `DONE`: BQ27220 monitor thread stack raised from 2048 to 3072 bytes for float-formatting margin; compiled.
- `DONE`: UI sleep force-close path now sends a UI watchdog heartbeat before continuing; compiled.
- `DONE`: `audio_force_acquire()` now rejects `AUDIO_OWNER_NONE` to avoid accidental owner clear without semaphore release; compiled.
- `DONE`: Home/AI Dou pending-state UI mutexes are deleted on page destroy; compiled.
- `DONE`: Home/AI Dou only unregister Xiaozhi UI callbacks when destroying the active page; compiled.
- `DONE`: Wallpaper delayed render timer now guards missing screen/image-card refs; compiled.
- `DONE`: Recorder record-dir/path builders now reject truncated paths; compiled.
- `DONE`: Wallpaper and Reading List filesystem path builders now skip truncated paths; compiled.
- `DONE`: EPUB internal path resolver now rejects truncated resolved paths; compiled.
- `DONE`: EPUB XML tag/entity copies and attribute extraction now reject truncation; compiled.
- `DONE`: EPUB attribute extraction now matches whole XML attribute names; compiled.
- `DONE`: EPUB XHTML tag-name detection now requires a tag-name boundary; compiled.
- `DONE`: EPUB XHTML supported tag-name matching is now case-insensitive; compiled.
- `DONE`: EPUB OPF manifest/spine tag scans now use boundary-aware case-insensitive start-tag matching; compiled.
- `DONE`: EPUB XML attribute-name extraction is now case-insensitive; compiled.
- `DONE`: TF media directory creation now skips truncated paths instead of calling `mkdir()` on them; compiled.
- `DONE`: Reading cover cache read/write path builders now reject truncated cache paths; compiled.
- `DONE`: EPUB package/cover/image/spine internal path copies now reject truncation; compiled.
- `DONE`: Recorder listing/playback cached paths now reject truncation; compiled.
- `DONE`: Recorder active-record cached path/name validation now happens before opening the file; compiled.
- `DONE`: Config reading font and wallpaper path setters/load parsing now reject truncation; compiled.
- `DONE`: Config storage path formatting now rejects negative `snprintf()` results; compiled.
- `DONE`: Reading cover cache temp-path formatting now rejects negative `rt_snprintf()` results; compiled.
- `DONE`: Reading cover cache directory-tree path copy now rejects truncation/format failure; compiled.
- `DONE`: Pet game storage path helpers now reject truncation/format failure; compiled.
- `DONE`: Xiaozhi MQTT hello session_id copy now rejects empty/oversized values; compiled.
- `DONE`: Xiaozhi WebSocket OTA URL/token and activation code copies now reject oversized protocol fields; compiled.
- `DONE`: Xiaozhi WebSocket hello session_id copy now rejects empty/oversized values; compiled.
- `DONE`: Xiaozhi weather code parsing now rejects empty/oversized protocol values; compiled.
- `DONE`: Music service playable track paths now skip overlong path entries; compiled.
- `DONE`: UI font manager font directory and list path builders now reject truncated filesystem paths; compiled.
- `DONE`: UI font manager active font path copies now reject oversized paths; compiled.
- `DONE`: Reading Detail font path state copies now reject oversized filesystem paths; compiled.
- `DONE`: Reading Detail request/current book paths now reject truncation; compiled.
- `DONE`: UI helper file-font cache and LVGL path conversion now reject truncation; compiled.
- `DONE`: Reading List selected/current/cover filesystem path copies now reject truncation; compiled.
- `DONE`: Record List suppressed playback path cache now rejects truncation; compiled.
- `DONE`: UI font manager configured-font previous path cache now uses checked path copy; compiled.
- `DONE`: Xiaozhi WebSocket fixed endpoint host/path copies now reject truncation; compiled.
- `DONE`: Recorder active-record file creation no longer truncates an existing same-name recording; compiled.
- `DONE`: AW32001 debug status text read/write now uses a short critical section; compiled.
- `DONE`: Xiaozhi WebSocket activation last-code cache now uses checked copy; compiled.
- `DONE`: EPUB OPF cover-id cache now rejects truncation before manifest lookup; compiled.
- `DONE`: EPUB ZIP entry name cache now checks formatting before accepting metadata; compiled.
- `DONE`: App config storage path handoff now checks internal path copies; compiled.
- `DONE`: Petgame state parser now rejects truncated local parse copies; compiled.
- `DONE`: Reading state storage path handoff now checks internal path copies; compiled.
- `DONE`: UI screen build-list and runtime registration consistency rechecked; no code changes needed.
- `DONE`: UI navigation/layout compliance scan classified non-standard pages; no code changes needed.
- `DONE`: UI fixed 528x792 bounds scan found no clear helper-call overflow; no code changes needed.
- `DONE`: Weather dynamic short labels now use ellipsis instead of hard clipping; compiled.
- `DONE`: Datetime dynamic single-line labels now use ellipsis instead of hard clipping; compiled.
- `DONE`: Calendar dynamic single-line labels now use ellipsis instead of hard clipping; compiled.
- `DONE`: Pomodoro dynamic badge/subtitle labels now use ellipsis instead of hard clipping; compiled.
- `DONE`: Reading Detail page indicator label now uses ellipsis instead of hard clipping; compiled.
- `DONE`: Record List page indicator label now uses ellipsis instead of hard clipping; compiled.
- `DONE`: Music Player subtitle/state labels now use ellipsis instead of hard clipping; compiled.
- `DONE`: Reading List book-count label now uses ellipsis instead of hard clipping; compiled.
- `DONE`: Pet speech/notice labels now use ellipsis instead of hard clipping; compiled.
- `DONE`: AI Dou mouth status and Settings summary labels now use ellipsis instead of hard clipping; compiled.
- `DONE`: Music List title/subtitle/page labels now use ellipsis instead of hard clipping; compiled.
- `DONE`: Standby date/weather labels now use ellipsis instead of hard clipping; compiled.
- `DONE`: Home AI bubble copy label now preserves wrap instead of hard clipping; compiled.
- `DONE`: Reading List card title/meta labels now use ellipsis instead of hard clipping; compiled.
- `DONE`: Pomodoro status/hint labels are now created and refreshed visibly; compiled.
- `DONE`: AI Dou network status label is now created and refreshed visibly; compiled.

## P0 Backlog

- `DONE`: Reading detail TXT stream FD leak.
  - Files: `app/src/ui/screens/ui_reading_detail_screen.c`
  - Fix: ensure `ui_Reading_Detail_screen_destroy()` calls the existing text-source reset helper after cancelling load activity.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-06; repeated TXT open/close still needs hardware validation.

- `DONE`: Pet game save is not atomic.
  - Files: `app/src/petgame_storage.c`
  - Fix: write-all loop, temp file, `fsync`, checked close, `rename`, preserve old file on failure.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07; power-fail style hardware validation remains.

- `DONE`: App config save has destructive fallback.
  - Files: `app/src/config/app_config_storage.c`
  - Fix: add `fsync`; remove old-file unlink and direct `O_TRUNC` fallback.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07; settings save/load hardware validation remains.

- `DEFERRED`: Xiaozhi audio close timeout can free resources while thread is alive.
  - Files: `app/src/xiaozhi/xiaozhi_audio.c`
  - Reason: needs careful lifecycle testing; avoid quick patch without hardware stress.

## P1 Backlog

- `DONE`: EPUB path resolver uses non-reentrant `strtok()`.
  - Files: `app/src/reading/reading_epub.c`
  - Fix: replaced with `strtok_r()` using local parser state.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-06.

- `DONE`: Reading cover worker reads mutable global file list after start.
  - Files: `app/src/ui/screens/ui_reading_list_screen.c`
  - Fix: copy missing EPUB paths into worker-owned path snapshots before thread startup.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: EPUB deflate read accepts success without validating output length.
  - Files: `app/src/reading/reading_epub.c`
  - Fix: validate `decomp.dest` reaches `output_buffer + uncompressed_size` before accepting success.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: EPUB internal path resolution truncates long paths silently.
  - Files: `app/src/reading/reading_epub.c`
  - Fix: make `reading_epub_resolve_path()` return success/failure, check every internal `rt_snprintf()` append, and have cover/spine/image callers skip truncated paths.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: EPUB XML attribute/tag extraction truncates values silently.
  - Files: `app/src/reading/reading_epub.c`
  - Fix: reject oversized attribute values, add checked XML span copying, and skip oversized tag/entity spans instead of parsing truncated XML fragments.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: EPUB attribute extraction can match attribute-name substrings.
  - Files: `app/src/reading/reading_epub.c`
  - Fix: replace raw `strstr(attribute_name)` matching with XML attribute boundary checks before accepting `=`, avoiding false positives such as `href` inside `xlink:href`.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: BQ27220 status text read/write not synchronized.
  - Files: `app/src/bq27220_monitor.c`
  - Fix: use existing critical-section style around status text copies.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: `network_mem_ensure_ready()` one-time init race.
  - Files: `app/src/network/network_mem.c`
  - Fix: double-check `s_network_psram_heap_ready` inside a short interrupt-disabled critical section before `rt_memheap_init()`.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DEFERRED`: Sleep manager transition state lacks a single guard.
  - Files: `app/src/sleep_manager.c`, callers in buttons/touch/alarm paths.
  - Reason: transition side effects need sequencing tests.

- `DEFERRED`: Network manager pending fields are multi-field volatile state.
  - Files: `app/src/network/net_manager.c`
  - Reason: larger event queue/transaction model.

## P2 Backlog

- `DONE`: Pet Rules return target likely points to itself.
  - Files: `app/src/ui/screens/ui_pet_rules_screen.c`
  - Fix: confirmed helper parameter is `back_target` and changed it to `UI_SCREEN_PET`.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Language invalidation misses cached pages.
  - Files: `app/src/ui/screens/ui_language_screen.c`
  - Fix: destroy Standby, Pet Rules, Wallpaper, AI Weather Settings on language changes.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Standard content height hard-coded as 653 on some pages.
  - Files: `app/src/ui/screens/ui_language_screen.c`, `app/src/ui/screens/ui_record_list_screen.c`
  - Fix: use page-local standard content height constants set to 650.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Runtime hardkey fallback forces non-scroll pages scrollable.
  - Files: `app/src/ui/ui_runtime_adapter.c`
  - Fix: return fallback targets only when they already have `LV_OBJ_FLAG_SCROLLABLE`, and remove forced `lv_obj_add_flag()`.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Runtime idle timer has no explicit deinit/reset.
  - Files: `app/src/ui/ui_runtime_adapter.c`, `app/src/ui/ui.c`
  - Fix: add `ui_runtime_deinit()` and call it from `ui_destroy()` before destroying screens.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Home/AI Dou pending-state UI mutexes are not deleted on page destroy.
  - Files: `app/src/ui/screens/ui_home_screen.c`, `app/src/ui/screens/ui_ai_dou_screen.c`
  - Fix: delete the lazily-created pending mutex after unregistering Xiaozhi UI callbacks and clearing pending state.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Home/AI Dou destroy can clear another page's Xiaozhi UI callbacks.
  - Files: `app/src/ui/screens/ui_home_screen.c`, `app/src/ui/screens/ui_ai_dou_screen.c`
  - Fix: guard `xiaozhi_service_register_ui_callbacks(NULL)` with `ui_runtime_get_active_screen_id()` so inactive cached-page destroy does not clear the active AI UI callback owner.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Reading List card title/meta labels hard-clip dynamic book text.
  - Files: `app/src/ui/screens/ui_reading_list_screen.c`
  - Fix: change ordinary book-card and continue-card title/meta labels from `LV_LABEL_LONG_CLIP` to `LV_LABEL_LONG_DOT`, preserving fixed card dimensions while showing truncation explicitly.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Wallpaper delayed render can run without valid screen refs.
  - Files: `app/src/ui/screens/ui_wallpaper_screen.c`
  - Fix: guard the one-shot render timer callback and render entry with `ui_Wallpaper`/`image_card` checks before creating image objects.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Wallpaper/Reading List filesystem path builders accept truncated paths.
  - Files: `app/src/ui/screens/ui_wallpaper_screen.c`, `app/src/ui/screens/ui_reading_list_screen.c`
  - Fix: check `rt_snprintf()` return values when building `/pic`, wallpaper image paths, and reading list `stat()` paths; skip or fail on truncation.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DEFERRED`: Some business pages bypass standard shared navigation.
  - Files: `ui_about_screen.c`, `ui_brightness_screen.c`, `ui_datetime_screen.c`.
  - Reason: visual/UX behavior needs owner decision because AGENTS.md forbids casual nav edits. Review 2026-05-07 08:06: fullscreen Reading Detail, Wallpaper, and Standby are treated as intentional immersive pages; About/Brightness/Datetime remain candidates for a planned settings-page navigation refactor.

## P3 Backlog

- `DONE`: MQTT/WebSocket JSON abort messages use `strcat()`.
  - Files: `app/src/xiaozhi/xiaozhi_mqtt.c`, `app/src/xiaozhi/xiaozhi_websocket.c`
  - Fix: replace tail `strcat()` with full-message `rt_snprintf()` branches.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: UI sleep path may skip watchdog heartbeat.
  - Files: `app/src/main.c`
  - Fix: add UI watchdog heartbeat before the `gui_is_force_close()` branch continues.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: BQ27220 thread stack may be tight with float formatting.
  - Files: `app/src/bq27220_monitor.c`
  - Fix: raise monitor thread stack to 3072 bytes.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: HTTP lock release lacks owner validation.
  - Files: `app/src/network/net_http_lock.c`
  - Fix: track owner validity, ignore mismatched/no-owner release attempts, and only clear owner state after successful mutex release.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Recorder path builders accept truncated paths.
  - Files: `app/src/xiaozhi/recorder_service.c`
  - Fix: check `rt_snprintf()` return values in record directory and file path builders; clear output and fail on truncation.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DEFERRED`: Audio preemption only changes owner and does not stop old stream.
  - Files: `app/src/audio_manager.c`, `app/src/music_service.c`, `app/src/xiaozhi/xiaozhi_service.c`
  - Reason: requires preemption contract and hardware audio validation. Review 2026-05-07 02:17: `audio_register_state_callback()` has no current registrants, `audio_try_preempt()` only changes owner and notifies, and old stream shutdown is not centralized; changing this mechanically risks cross-thread `mp3ctrl`/mic/speaker lifecycle issues.

- `DONE`: `audio_force_acquire()` accepted `AUDIO_OWNER_NONE`.
  - Files: `app/src/audio_manager.c`
  - Fix: reject uninitialized manager, null mutex, and `AUDIO_OWNER_NONE` before changing owner/semaphore state.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DEFERRED`: Startup config mount can block up to 12 seconds.
  - Files: `app/src/main.c`
  - Reason: `tf_try_boot_mount_for_config()` uses `TF_MOUNT_RETRY_COUNT` 120 * `TF_MOUNT_RETRY_DELAY_MS` 100ms before `app_config_load()`. Shortening this can boot faster but may miss TF-backed config on slow card init; needs product decision. The same retry constants are also used by the background TF mount path.

- `DONE`: TF media directory creation ignores path truncation.
  - Files: `app/src/main.c`
  - Fix: check `rt_snprintf()` results in `tf_ensure_media_dirs()` and skip overlong media directory paths instead of passing truncated strings to `mkdir()`.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Recorder file creation uses direct `O_TRUNC` for active recording output.
  - Files: `app/src/xiaozhi/recorder_service.c`
  - Fix: replace direct `O_TRUNC` creation with `O_EXCL` non-overwrite creation and up to 8 timestamp suffix attempts, preventing same-second filename collisions from truncating an existing recording while preserving live streaming writes.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Recorder listing/playback cached paths can truncate.
  - Files: `app/src/xiaozhi/recorder_service.c`
  - Fix: check `rt_snprintf()` when copying record dir, scanned file name/path, and requested playback path/name; skip too-long list entries and reject too-long playback requests before event dispatch.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Recorder active-record cached file path/name copies still assume fit after opening the file.
  - Files: `app/src/xiaozhi/recorder_service.c`
  - Fix: validate same-sized cached record path/name buffers before `open()`, then copy the verified values into recorder state after the fd is opened and the lock is taken.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Config path fields can be silently truncated by setters/load parsing.
  - Files: `app/src/config/app_config.c`
  - Fix: add a checked string-copy helper and use it for `reading.font_path` and `wallpaper.path` during config parsing and public setters; overlong setter values now return `-RT_EFULL`.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Config storage temp-path formatting checks do not reject negative `snprintf()`.
  - Files: `app/src/config/app_config_storage.c`
  - Fix: store `snprintf()` return values for temp path, load out path, and save path copies, and reject `< 0 || >= size` before using the destination buffer.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Reading cover cache temp-path formatting checks do not reject negative `rt_snprintf()`.
  - Files: `app/src/reading/reading_cover_cache.c`
  - Fix: store `rt_snprintf()` return values and reject `< 0 || >= size` for state and image temp path construction before opening temp files.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Reading state save temp/backup path formatting should be rechecked.
  - Files: `app/src/reading/reading_state.c`
  - Review: `reading_state_save_to_path_locked()` already rejects negative and truncated temp/backup path formatting and unlinks temp files on write/fsync/close/rename failures.
  - Verify: no code change needed in this file on 2026-05-07.

- `DONE`: Reading cover cache directory tree creation copies path without checking truncation.
  - Files: `app/src/reading/reading_cover_cache.c`
  - Fix: check `rt_snprintf()` in `reading_cover_ensure_dir_tree()` and return on negative/truncated path before walking and creating directories.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Pet game storage path helpers accept truncated paths.
  - Files: `app/src/petgame_storage.c`
  - Fix: check `rt_snprintf()` return values for mounted root copy, join-path branches, and final games/data directory copies; clear outputs and fail before `mkdir()`/open on negative or truncated formatting.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Xiaozhi MQTT hello session_id can truncate or miss termination.
  - Files: `app/src/xiaozhi/xiaozhi_mqtt.c`
  - Fix: add a checked session_id copy helper that rejects empty or oversized values before MQTT listen start and clears the destination on failure.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Xiaozhi WebSocket OTA/activation string copies should reject oversized protocol fields.
  - Files: `app/src/xiaozhi/xiaozhi_websocket.c`
  - Fix: add bounded OTA string duplication for URL/token, cap URL/token lengths, and validate activation code length before copying so overlong protocol fields are rejected instead of truncated or over-allocated.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Xiaozhi WebSocket hello session_id copy should reject truncation.
  - Files: `app/src/xiaozhi/xiaozhi_websocket.c`
  - Fix: add a checked fixed-buffer session_id copy helper and reject empty/oversized hello session IDs before moving WebSocket state to ready.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Xiaozhi weather JSON string copies intentionally truncate display fields.
  - Files: `app/src/xiaozhi/weather/weather.c`
  - Fix: keep location/text/wind fields as display-only truncating copies, but add a checked string helper for the weather `code` protocol field so icon selection never uses a truncated code.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Music service title copies intentionally truncate display text.
  - Files: `app/src/music_service.c`
  - Fix: keep title truncation as display-only behavior, but check playable path formatting in `music_service_refresh()` and skip overlong MP3 entries instead of storing truncated paths.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: UI font manager path builders should reject filesystem path truncation.
  - Files: `app/src/ui/ui_font_manager.c`
  - Fix: make mount-root/path copy helpers fail on overflow, add checked path joining for font subdir discovery and list scanning, and skip overlong font entries instead of probing truncated paths.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: UI font manager active font path copies should reject oversized paths.
  - Files: `app/src/ui/ui_font_manager.c`
  - Fix: make selected active path copies exact and failure-aware, have active-path getters fail when the caller buffer is too small, and reject oversized selected font paths before updating runtime/config state. Display font names remain truncating UI text.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Reading detail font path copies should reject oversized filesystem paths.
  - Files: `app/src/ui/screens/ui_reading_detail_screen.c`
  - Fix: add a checked font-path copy helper and use it for configured font loading, selected font path getters, rejected-font path cache, loaded HDFont path state, selected font item paths, and previous configured path matching. Font names remain display-only truncating text.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Reading Detail request/current book paths should reject truncation.
  - Files: `app/src/ui/screens/ui_reading_detail_screen.c`
  - Fix: add a checked book-path copy path, use it for current book state, async/sync request paths, and worker-local load path copies; failed copies clear the request path and use the existing empty-selection fallback.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: UI helper file-font cache path should reject truncation.
  - Files: `app/src/ui/ui_helpers.c`
  - Fix: add checked file-font cache path copying and check `A:`/`A:/` LVGL path conversion formatting before using converted font filesystem paths. Failure-log path text remains debug/display-only.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Reading List selected/current path copies should reject truncation.
  - Files: `app/src/ui/screens/ui_reading_list_screen.c`
  - Fix: make Reading List path copy/join helpers failure-aware, reject oversized selected/current paths, mount snapshots, file entries, cover worker snapshots, cover-card cache paths, and selected-path getters while keeping display text truncation unchanged.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Record List suppressed playback path cache can truncate.
  - Files: `app/src/ui/screens/ui_record_list_screen.c`
  - Fix: replace the `rt_strncpy()` path cache copy with a checked exact copy helper; on overflow the suppressed path cache is cleared instead of comparing against a truncated filesystem path.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: UI font manager configured-font previous path cache still used `rt_strncpy()`.
  - Files: `app/src/ui/ui_font_manager.c`
  - Fix: use the existing checked `ui_font_manager_copy_path()` helper when snapshotting `active_path` before applying configured font changes.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Xiaozhi WebSocket fixed endpoint host/path copies used unchecked `%s` formatting.
  - Files: `app/src/xiaozhi/xiaozhi_websocket.c`
  - Fix: add a fixed-buffer copy helper and reject oversized static host/path values with `-RT_EFULL` before calling `wsock_connect()`.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: AW32001 debug status text read/write was unsynchronized.
  - Files: `app/src/aw32001_debug.c`
  - Fix: wrap status text update/get copies with a short `rt_enter_critical()`/`rt_exit_critical()` section, matching the low-overhead style used by BQ27220 status text.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Xiaozhi WebSocket activation last-code cache used unchecked `%s` formatting.
  - Files: `app/src/xiaozhi/xiaozhi_websocket.c`
  - Fix: reuse the fixed-buffer copy helper for `last_code`, which participates in activation-code change detection.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: EPUB OPF cover-id cache used unchecked `%s` formatting.
  - Files: `app/src/reading/reading_epub.c`
  - Fix: check `rt_snprintf()` when caching EPUB2 `<meta name="cover" content="...">` IDs, clear and skip on truncation before later manifest lookup.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: EPUB ZIP entry name cache used unchecked `%s` formatting.
  - Files: `app/src/reading/reading_epub.c`
  - Fix: check the `entry_out->name` formatting result before accepting central-directory metadata and clear the output on failure.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: App config storage path handoff used unchecked internal copy.
  - Files: `app/src/config/app_config.c`
  - Fix: replace `s_storage_temp_path` to `s_storage_path` handoff after load/save with checked copies and return `-RT_EFULL` on impossible truncation.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Petgame state parser local copy used unchecked `%s` formatting.
  - Files: `app/src/petgame_storage.c`
  - Fix: check the `rt_snprintf()` result before tokenizing the local parse buffer, rejecting oversized state text instead of parsing a truncated copy.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Reading state storage path handoff ignored checked copy results.
  - Files: `app/src/reading/reading_state.c`
  - Fix: check `reading_state_copy_path()` when persisting discovered/saved storage paths and return `-RT_EFULL` if the internal handoff ever fails.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: UI build-list/page-registration consistency pass.
  - Files: `app/src/ui/screens`, `app/src/ui/filelist.txt`, `app/src/ui/CMakeLists.txt`, `app/src/ui/ui_types.h`, `app/src/ui/ui.h`, `app/src/ui/ui_runtime_adapter.c`, `app/src/ui/ui.c`
  - Review: screen source files, `filelist.txt`, and `CMakeLists.txt` are in sync; runtime registration covers current screen enums. `UI_SCREEN_STANDBY` remains runtime-managed and intentionally absent from the main page rotation list.
  - Verify: no code changes in this round, so `sf32lb52-lcd_n16r8` build was not rerun.

- `DONE`: UI navigation/layout compliance scan.
  - Files: `app/src/ui/screens/ui_about_screen.c`, `ui_brightness_screen.c`, `ui_datetime_screen.c`, `ui_wallpaper_screen.c`, `ui_reading_detail_screen.c`, `ui_standby_screen.c`
  - Review: Reading Detail, Wallpaper, and Standby are intentionally fullscreen/immersive pages. About, Brightness, and Datetime are settings-style subpages that currently bypass standard scaffold/shared nav; this is a UX/navigation decision, not a safe automatic patch.
  - Verify: no code changes in this round, so `sf32lb52-lcd_n16r8` build was not rerun.

- `DONE`: UI fixed 528x792 bounds scan.
  - Files: `app/src/ui/screens/*.c`
  - Review: scanned common `ui_create_label`, `ui_create_button`, `ui_create_card`, `ui_create_image_slot`, and obvious fixed `lv_obj_set_size()` dimensions. No clear content overflow found. Fullscreen/immersive objects such as Wallpaper image/card `528x792`, Standby background, Reading List overlays, and Calendar content `528x647` are within design bounds or intentional.
  - Verify: no code changes in this round, so `sf32lb52-lcd_n16r8` build was not rerun.

- `DONE`: Weather dynamic short labels hard-clip overflow.
  - Files: `app/src/ui/screens/ui_weather_screen.c`
  - Fix: set `LV_LABEL_LONG_DOT` on the last-update label and metric labels so longer English/status/weather fields ellipsize within their fixed-width containers instead of hard clipping.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Datetime dynamic single-line labels hard-clip overflow.
  - Files: `app/src/ui/screens/ui_datetime_screen.c`
  - Fix: set `LV_LABEL_LONG_DOT` on the current-time, status, and hint labels so future longer English/status text ellipsizes within the fixed-width settings layout.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Calendar dynamic single-line labels hard-clip overflow.
  - Files: `app/src/ui/screens/ui_calendar_screen.c`
  - Fix: set `LV_LABEL_LONG_DOT` on the month title, today metadata, and selected-date summary labels; kept selected-date detail as wrapping text.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Pomodoro dynamic single-line labels hard-clip overflow.
  - Files: `app/src/ui/screens/ui_pomodoro_screen.c`
  - Fix: set `LV_LABEL_LONG_DOT` on the badge and subtitle labels so longer future state/subtitle text ellipsizes within the full-width fixed layout.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Reading Detail page indicator label hard-clips overflow.
  - Files: `app/src/ui/screens/ui_reading_detail_screen.c`
  - Fix: set `LV_LABEL_LONG_DOT` on the bottom page indicator label so long chapter/page strings ellipsize in the fixed 160px footer slot. The filename label already used dot mode.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Record List page indicator label hard-clips overflow.
  - Files: `app/src/ui/screens/ui_record_list_screen.c`
  - Fix: set `LV_LABEL_LONG_DOT` on the bottom page indicator label so large page counts ellipsize in the fixed 120px footer slot.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Music Player dynamic single-line labels hard-clip overflow.
  - Files: `app/src/ui/screens/ui_music_player_screen.c`
  - Fix: set `LV_LABEL_LONG_DOT` on the subtitle and playback-state labels. The title label remains wrapping because it can represent long track names.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Reading List book-count label hard-clips overflow.
  - Files: `app/src/ui/screens/ui_reading_list_screen.c`
  - Fix: set `LV_LABEL_LONG_DOT` on the first-page book-count label so longer localized/count text ellipsizes in the fixed 260px title slot. Empty-state status text remains unchanged because it is a larger explanatory area.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Pet dynamic speech/notice labels hard-clip overflow.
  - Files: `app/src/ui/screens/ui_pet_screen.c`
  - Fix: change the single-line speech bubble and notice labels from `LV_LABEL_LONG_CLIP` to `LV_LABEL_LONG_DOT`, so longer localized or quota/status messages ellipsize within the fixed cards.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: AI Dou mouth status and Settings summary labels hard-clip overflow.
  - Files: `app/src/ui/screens/ui_ai_dou_screen.c`, `app/src/ui/screens/ui_settings_screen.c`
  - Fix: set `LV_LABEL_LONG_DOT` on the AI mouth/status label and Settings row summary labels. AI copy/help labels remain wrapping text.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Music List dynamic single-line labels hard-clip overflow.
  - Files: `app/src/ui/screens/ui_music_list_screen.c`
  - Fix: set `LV_LABEL_LONG_DOT` on list item title, list item subtitle, and page indicator labels so long track names/counts ellipsize within fixed list slots.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Standby dynamic date/weather labels hard-clip overflow.
  - Files: `app/src/ui/screens/ui_standby_screen.c`
  - Fix: set `LV_LABEL_LONG_DOT` on the date and weather labels so longer weekday, city, or weather strings ellipsize within the fixed standby layout.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Home AI bubble copy label hard-clips despite wrap layout.
  - Files: `app/src/ui/screens/ui_home_screen.c`
  - Fix: restore `LV_LABEL_LONG_WRAP` on the Home AI bubble copy label. The label is created with `wrap=true`, and overriding it to `LV_LABEL_LONG_CLIP` caused multi-line AI prompt/status text to hard-clip inside the speech bubble.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: AI Dou network status label is refreshed but not created.
  - Files: `app/src/ui/screens/ui_ai_dou_screen.c`
  - Fix: create a compact single-line network status label between the avatar and dialog card, set ellipsis mode, and reuse the existing `update_network_status()` refresh path.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: Pomodoro status/hint refs are refreshed but never created.
  - Files: `app/src/ui/screens/ui_pomodoro_screen.c`
  - Fix: create a compact status label under the page title and a wrapped hint label above the bottom nav, then reuse the existing `pomodoro_refresh_ui()` assignments.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DEFERRED`: Pet game activity event mutex is module-lifetime and has no deinit.
  - Files: `app/src/petgame_reducer.c`
  - Reason: `petgame_init()` is effectively lazy module initialization and there is no matching petgame deinit contract; deleting it from UI page destroy would risk background/state users.

- `DEFERRED`: Reading cover worker can continue briefly after Reading List page destroy.
  - Files: `app/src/ui/screens/ui_reading_list_screen.c`
  - Reason: destroy sets `s_reading_cover_worker_cancel` and removes UI timers, while the worker owns path snapshots and does not touch LVGL objects; joining the worker would need lifecycle/hardware latency validation.

- `DONE`: Reading cover cache path builders accept truncated cache paths.
  - Files: `app/src/reading/reading_cover_cache.c`
  - Fix: check `rt_snprintf()` in `reading_cover_find_existing_path()` and `reading_cover_make_write_path()` so truncated cache paths are skipped or rejected before `stat()`/write.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: EPUB internal path copies can silently truncate after resolution.
  - Files: `app/src/reading/reading_epub.c`
  - Fix: check `rt_snprintf()` copies for `package->opf_path`, `package->cover_path`, exported cover `internal_path`, image block paths, and collected spine item paths; reject or skip overlong internal paths before later ZIP reads.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DEFERRED`: Remaining UI path copies mostly truncate display/status text intentionally.
  - Files: `ui_reading_list_screen.c`, `ui_wallpaper_screen.c`, `ui_ai_dou_screen.c`, `ui_home_screen.c`
  - Reason: many `rt_snprintf("%s")` sites feed UI labels or cached status strings; changing all to hard failures would alter UX. Future rounds should keep targeting file-system paths and persisted identifiers first.

- `DEFERRED`: EPUB parser remains a lightweight string scanner.
  - Files: `app/src/reading/reading_epub.c`
  - Reason: current parser still depends on simple string matching and fixed limits. Deeper compatibility work should use a focused EPUB corpus before broad parser changes.

- `DONE`: EPUB tag detection still uses prefix substring checks.
  - Files: `app/src/reading/reading_epub.c`
  - Fix: add `reading_epub_tag_name_matches()` and use it for paragraph, break, image, and closing block tags so names such as `<pagenum>` or `<h10>` are not treated as supported XHTML tags.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: EPUB XHTML tag detection remains case-sensitive.
  - Files: `app/src/reading/reading_epub.c`
  - Fix: make `reading_epub_tag_name_matches()` use `strncasecmp()` while preserving the existing boundary checks.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: EPUB OPF tag scans are still literal and case-sensitive.
  - Files: `app/src/reading/reading_epub.c`
  - Fix: add `reading_epub_find_xml_start_tag()` with boundary-aware `strncasecmp()` matching, then use it for OPF `<meta>`, `<item>`, `<spine>`, and `<itemref>` scans.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DONE`: EPUB attribute extraction is still case-sensitive.
  - Files: `app/src/reading/reading_epub.c`
  - Fix: make `reading_epub_extract_attribute()` scan with `strncasecmp()` while preserving full-name boundary, `=`, quoting, and oversized-value checks.
  - Verify: `sf32lb52-lcd_n16r8` build passed on 2026-05-07.

- `DEFERRED`: Remaining `RT_WAITING_FOREVER` waits are mostly service-thread/event-loop waits.
  - Files: `audio_manager.c`, `touch_wakeup.c`, `net_manager.c`, `ui_dispatch.c`, `recorder_service.c`, `xiaozhi_service.c`
  - Reason: not a low-risk mechanical timeout change; each wait must be reviewed with thread ownership and shutdown semantics.
  - Review 2026-05-07 02:09: `audio_manager` waits are short mutex-protected state accesses; `touch_wakeup`, TF mount, UI dispatch, network manager, recorder, Xiaozhi, reading, and weather waits are dedicated service/event-loop waits. No low-risk mechanical timeout selected.

## Scan Commands For Future Rounds

```bash
rg -n "TODO|FIXME|XXX|HACK|BUG|strtok\\(|strcat\\(|O_TRUNC|RT_WAITING_FOREVER|rt_realloc|rt_mutex_create|lv_timer_create" app/src app/project -g '!sdk/**'
```

```bash
comm -23 <(rg --files app/src/ui/screens | sed 's|app/src/ui/||' | sort) <(rg '^screens/.*\\.c$' app/src/ui/filelist.txt | sort)
comm -23 <(rg --files app/src/ui/screens | sed 's|app/src/ui/||' | sort) <(rg 'screens/.*\\.c' app/src/ui/CMakeLists.txt -o | sort)
```

```bash
rg -n "open\\(|write\\(|rename\\(|unlink\\(|fsync\\(|mkdir" app/src/config app/src/reading app/src/petgame* app/src/xiaozhi app/src/ui/screens -g '!sdk/**'
```

## Last Planning Scan

- Date: 2026-05-06
- Method: 4 read-only explorer subagents plus main-thread static scan.
- Compile in this specific planning step: not run, because only documentation/backlog files were added.

## Last Automation Round

- Time: 2026-05-07 10:48 Asia/Shanghai
- Completed: Recorder active-record file creation now uses non-overwrite creation with short suffix retries instead of direct truncation.
- Compile: `sf32lb52-lcd_n16r8` build passed.
- Existing warnings: linker `RWX` segment and executable stack warnings remain non-blocking.
- Next suggested item: continue scanning storage/network path truncation and service lifecycle waits; deeper recorder temp/rename semantics remain a hardware-contract item.
