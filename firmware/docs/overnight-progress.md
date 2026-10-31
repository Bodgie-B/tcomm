# Overnight work log (2026-10-21 into 10-22)

Mike approved at about 18:40: the overnight plan, downloads (WinLibs GCC 16.2 via winget, Jost and Atkinson Hyperlegible fonts from google/fonts, lv_font_conv 1.5.3 via npx, fonttools via pip), local git commits, then an adversarial review with fixes, ready by morning. No flashing, no pushing, no purchases, no security-setting changes. If the hourly usage limit stops work, resume when it passes (an hourly CronCreate job in this session does that).

Git: initialise the repository and commit only after the review fixes and the morning report, so no file edits happen inside a repository overnight.

## Steps

- [x] 1. Resume schedule (cron job 8ead9416, hourly at :17), downloads
- [x] 2. Fonts converted to LVGL (Jost 800: 64, 54, 44, 30; Atkinson Bold: 14, 16, 20, 24, 26; Atkinson Regular: 16, 20)
- [x] 3. Core logic (plain C++) and protocol, with host unit tests (191 checks pass; WAVs in docs/sounds)
- [x] 4. Board layer (I2C lock, expander, backlight, RTC, audio with synthesized chimes; ESP-NOW written, off)
- [x] 5. LVGL interface and demo mode (ui/*.cpp, app/demo.*, app/panel_app.h)
- [x] 6. Host screenshots of every state (26 in docs/screens via `bash tools/build_sim.sh`); ESP32 compile clean, 914 KB, no warnings (22:47)
- [x] 7. Adversarial review (three independent agents) and fixes. Core 22 findings, interface 18, board 16. Fixed except the items listed as open in docs/morning-report.md. 234 checks pass; ESP32 compile clean (919 KB).
- [x] 8. Morning report (docs/morning-report.md), git init and first commit (local only, not pushed), resume cron job deleted

## Notes as I go

- Host GCC: `%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin`. Host builds go to `%LOCALAPPDATA%\tcomm\host`. A test exe built and ran without Bitdefender interfering.
- Python (the install-manager build) can't see `%LOCALAPPDATA%\nodejs-portable`, so font conversion is a Bash script (`tools/make_fonts.sh`). Run Python with `-P`, not `-I`, so the user-site fonttools is visible.
- Static Jost ExtraBold made with fonttools instancer at wght=800.
