# TComm

A low-power tenant communication system for one house: two wall-mounted touchscreen panels, one at the top of the basement stairs and one at the bottom.

Each panel:
- shows the other floor's status in color (Open, Busy, Private, Sleeping, Away, or No signal)
- lets you set your own status with one tap
- rings a doorbell chime on the other panel from an on-screen tile
- shows both floors' temperature and humidity
- shows which dog (Lucy or Copper) is in the yard
- shows outside weather sent from a Raspberry Pi weather station

The panels talk to each other directly over ESP-NOW, so the system works with the router off.

## Hardware

- Two [Waveshare ESP32-S3-Touch-LCD-4.3C](https://docs.waveshare.com/ESP32-S3-Touch-LCD-4.3C) boards (800x480 IPS, capacitive touch, 16 MB flash, 8 MB PSRAM). One board is in hand. The second is bought after the interface and radio phases pass.
- One external SHT40 temperature and humidity sensor per panel, planned.
- One spare ESP32 on a Raspberry Pi USB port as a gateway for weather and dog signals, planned.

## Status

Early. The first board runs Waveshare's factory firmware and has been flashed and restored over USB. No project firmware exists yet.

## Where to start

1. Read [`docs/CLAUDE_CODE_HANDOFF.md`](docs/CLAUDE_CODE_HANDOFF.md). It is written for a coding assistant but works as a plain project brief for people too.
2. Check [`docs/design/`](docs/design/) for the GUI design brief.
3. Build order: bring-up and demos, interface alone, radio, sound and rules, sensor, install. The done-when checks are in the handoff brief.

## Working in this repo

This repo is public. Do not commit Wi-Fi passwords, ESP-NOW keys or tokens. Keep them in an untracked config file; `.gitignore` already covers common names.
