# AGENTS.md

## UI Screen Workflow

- This repo's UI screen coordinates are fixed design pixels. In `app/src/ui/ui_helpers.c`, `ui_px_x()`, `ui_px_y()`, `ui_px_w()`, and `ui_px_h()` currently return the input value unchanged.
- For standard pages built with `ui_build_standard_screen()`, place content using fixed coordinates inside `page.content`; do not mix in a second layout system.
- When adding a new screen, wire it through all of:
  - `app/src/ui/ui_types.h` for the `UI_SCREEN_*` enum
  - `app/src/ui/ui.h` for globals and init/destroy declarations
  - `app/src/ui/ui_runtime_adapter.c` for runtime screen registration
  - `app/src/ui/ui.c` for destroy ordering
- UI screen source files are also tracked in both `app/src/ui/filelist.txt` and `app/src/ui/CMakeLists.txt`; add new screen `.c` files to both lists.
- Persistent UI/runtime config in this checkout commonly lives under a discovered `config` directory such as `/config`, `/tf/config`, `/sd/config`, or `/sd0/config`.

## Useful Commands

- Find screen files:
  - `rg --files app/src/ui/screens`
- Find `UI_SCREEN_*` references and routing:
  - `rg "UI_SCREEN_" app/src/ui`
- Inspect standard screen layout helpers before placing controls:
  - `sed -n '2827,2955p' app/src/ui/ui_helpers.c`
- Inspect current config-directory discovery behavior:
  - `sed -n '20,120p' app/src/ui/ui_font_manager.c`

## TODO

- If the build/download flow should live here, document the exact repo-local command sequence after confirming the current preferred path in this checkout.
