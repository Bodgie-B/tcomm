# TComm

Read `docs/CLAUDE_CODE_HANDOFF.md` before doing anything. It holds the goal, the decisions already made, the radio protocol, the hardware facts and the build order.

The GUI design brief lives in `docs/design/`. Where it disagrees with the handoff brief on layout, colors, type or icons, the design brief wins.

The campsite screensaver is a separate sub-project with its own brief: `docs/screensaver/CAMPSITE_SCREENSAVER_BRIEF.md`. Read it when working on the screensaver. It runs as an idle mode inside the panel firmware, and its visual rules (Firewatch palettes, full animation) apply only to the screensaver, not to the main interface.

Screensaver work continues from `docs/screensaver/STAGE2_START_HERE.md` (on the `screensaver` branch). Read it first in any screensaver session: it records what is done and verified, the git rules for working with Mike, and the recommended path through Stage 2.

Rules for this repo:
- This repo is public. Never commit Wi-Fi passwords, ESP-NOW keys, tokens or anything from Mike's other projects. Use an untracked config file (see `.gitignore`).
- Confirm the COM port with the board owner before every flash. Never erase flash without asking.
- Mark anything you could not verify as unverified, in code comments and in reports.
