# TComm gateway

The ESP32-C6-GEEK that sits on the Raspberry Pi's USB port. The Pi's bridge (WeatherGate and
LucyGate, in Mike's WeatherGate project, not here) sends it one JSON line per message; the C6 turns
each into an encrypted ESP-NOW frame for the upstairs panel, retries until the panel acknowledges
it, and answers the Pi.

| Path | What it is |
|---|---|
| `gateway/gateway-c6/` | The C6's Arduino sketch, and `listener/`, a stand-in panel for testing |
| `gateway/tests/test_gateway.cpp` | Host tests for the gateway's logic, run by `bash firmware/tools/build_sim.sh` |
| `gateway/docs/gateway-c6.md` | Building, flashing and testing the C6, and its screen |
| `gateway/docs/weathergate-c6-brief.md` | What the WeatherGate project needs to know to drive it |
| `gateway/docs/gateway-brief.md` | The radio side: frames, retries, ACKs |
| `gateway/docs/weathergate.md` | The OUTSIDE message as the panels read it |

The gateway builds with the panels' toolchain and shares their wire-format code and `secrets.h`
(both under `firmware/tcomm/src`), so it's built from the firmware folder's script:

```
bash firmware/tools/fw.sh gateway COM7
```

Paths in this folder's files are from the repository's root. The Pi bridge's code belongs to the
WeatherGate project and isn't in this repository.
