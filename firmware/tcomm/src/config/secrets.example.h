// Copy to secrets.h (same folder) and fill in. secrets.h is in .gitignore: never commit it.
// The C6 gateway (firmware/gateway-c6) and its listener are built from this same file; see
// docs/gateway-c6.md.
// HANDOFF: the first version hard-codes both MAC addresses and the keys. Each panel learns its
// floor from its own MAC: the one listed as UPSTAIRS is upstairs. Both panels and the gateway are
// built from the same file.
#pragma once

// Wi-Fi station MACs (bash tools/fw.sh prints a board's MAC after each upload; for the C6, its
// BASE MAC). A board left at all zeros is skipped.
#define TCOMM_MAC_UPSTAIRS   {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
#define TCOMM_MAC_DOWNSTAIRS {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
#define TCOMM_MAC_GATEWAY    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}

// ESP-NOW CCMP keys: a 16-byte master key and a 16-byte key per peer link.
#define TCOMM_PMK         "0123456789abcdef"
#define TCOMM_LMK_PANELS  "0123456789abcdef"
#define TCOMM_LMK_GATEWAY "0123456789abcdef"

// The panels' location, for sunrise, sunset and the sky (decimal degrees, west negative). Rounded
// to a degree is close enough (a few minutes). Leave both out to use settings.h's placeholder.
#define TCOMM_LATITUDE  38.9f
#define TCOMM_LONGITUDE -77.0f

// The home Wi-Fi (docs/wifi-updates.md): the panel joins it for a few seconds to get the time after
// a power cut, and the updater for new firmware. Its name and password, and a password of your own
// for uploads to the updater.
#define TCOMM_WIFI_SSID ""
#define TCOMM_WIFI_PASS ""
#define TCOMM_OTA_PASS  ""

// The two people of each floor, for Away's "<name> out" options (Mike, 2026-11-26): short, written
// as they should show ("Name out"). Leave them out to get Person 1 to Person 4.
#define TCOMM_AWAY_UP_1   "Person 1"
#define TCOMM_AWAY_UP_2   "Person 2"
#define TCOMM_AWAY_DOWN_1 "Person 3"
#define TCOMM_AWAY_DOWN_2 "Person 4"
