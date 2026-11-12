# Updating both panels with no touch (Mike, 2026-11-07)

One command on the laptop updates both panels over the home Wi-Fi without anyone tapping either
screen. The chain, step by step:

1. `bash tools/fw.sh ota-remote` on the laptop (Git Bash, project folder) runs
   `tools/remote_trigger.local.sh`. That file is Mike's own and stays out of git (`*.local.sh` is
   in `.gitignore`). It holds the one command that asks the Pi to start an update, for example an
   `ssh` line, and prints the answer the Pi gives back.
2. The Pi's bridge writes one line to the gateway's serial port, the port it already holds open:

       {"type":"update","id":N}

   with N a positive whole number it hasn't used in this run of the bridge (the same `id` rule as
   the OUTSIDE and LUCY lines).
3. The gateway (ESP32-C6) sends an UPDATE radio message (type 15, one reserved byte 0) to the
   upstairs panel, with its usual retries over 1.5 s, and answers on its serial line:
   `"acked":"upstairs"` when the panel took it, `"failed":"upstairs"` when nobody answered,
   or `"busy":true` when an earlier update line is still waiting. The exact lines are in
   `../gateway/docs/gateway-c6.md`, "The update line".
4. The upstairs panel, on an UPDATE from the gateway, does what Settings > Update over Wi-Fi > BOTH
   PANELS does: it forwards UPDATE to downstairs, then restarts into its own updater 700 ms after
   downstairs answers, or after 2 s if it doesn't.
5. Both panels join the home Wi-Fi and wait in their updaters (up to ten minutes).
6. `ota-remote` compiles first, then runs the trigger (30 s at most), then polls `tools/find_panels.py`
   every 5 s, for up to 90 s, until it sees both updaters, and uploads to downstairs, then upstairs, exactly as `ota-both` does. The
   panels restart into the new firmware.

Without `tools/remote_trigger.local.sh`, `ota-remote` prints how to set it up and stops. If the
trigger command fails, or both updaters don't appear within 90 s, nothing is uploaded. If the
gateway's answer was `failed`, the upstairs panel never got the message (off, too far, or another
channel), so no updater will appear. The panels fall back to their old firmware by themselves if the
updaters aren't used within ten minutes (docs/wifi-updates.md).

Nothing in this repository runs on the Pi, and none of its addresses, names or logins belong here.

## What to ask the Pi project for

Give the WeatherGate session `docs/weathergate-remote-brief.md`. Since the gateway now stays on the
Pi (Mike, 2026-11-07), it asks for two commands run over ssh: this update command, and a flash
command that writes a gateway image the laptop sends with scp (`bash tools/fw.sh gateway-remote`).
It also says what goes in the two untracked laptop files, `tools/remote_trigger.local.sh` and
`tools/gateway_flash.local.sh`.
