# The C6 gateway, ready for the Pi

For the WeatherGate project (WeatherGate and LucyGate on the Raspberry Pi), from TComm,
2026-10-25. The earlier gateway brief covered the radio side. WeatherGate's own spec covered the
serial lines. This one says what the C6 actually does now, what was tested, and what the Pi has to
do to use it with the TComm panel.

## Where things stand

The ESP32-C6-GEEK has TComm's gateway firmware on it. It has been tested end to end from Mike's
Windows laptop, with Claude playing the Pi: JSON lines in over USB, ESP-NOW to the upstairs panel,
the panel's ACK back, and one final reply per request. Results, 2026-10-25:

- Every weather reading and Lucy press reached the panel and showed correctly on its screen,
  checked from screen captures of the panel itself. That covered all nine forecast icons, the
  heat and cold warnings, the temperature, humidity and phrase, and LUCY OUT and back in.
- After the last fixes, 43 timed requests got 43 final replies: 37 acked and 6 superseded, as
  intended. Each took 15 to 153 ms (median 46) from writing the line to reading the reply.
- Every malformed request got the expected rejection, and none of them sent anything over the
  radio.
- Two bugs in the C6's USB code turned up and are fixed (see Reply timing).

Nothing has run on the Pi yet: not Linux, not its USB port, not the bridge service. The C6's
`failed` reply hasn't happened on real hardware either. TComm's PC tests cover it, and they drive
the same code.

| | |
|---|---|
| Board | Waveshare ESP32-C6-GEEK |
| Firmware | `gateway-c6 0.1`, built and flashed from the TComm project (never from the Pi) |
| Wi-Fi MAC | `fc:01:2c:fd:d3:a8` |
| ESP-NOW | channel 1, encrypted; never joins Wi-Fi |
| Sends to | the upstairs panel, `14:c1:9f:54:be:20` |
| USB | the chip's own USB Serial/JTAG: Espressif `303a:1001`, serial number `FC:01:2C:FD:D3:A8` |

The keys are in the C6's firmware. The Pi never needs them, and they never go over the serial
link.

## Plugging it into the Pi

Any USB port, with a data cable (a charge-only cable shows nothing). The Pi's USB powers it, and
there's nothing to install. Linux's own `cdc_acm` driver picks it up as `/dev/ttyACM0`, or
`ttyACM1` if something else got there first.

That number can change between boots, so use the stable name:

```
/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_FC:01:2C:FD:D3:A8-if00
```

Linux builds that name from the USB strings, which Windows showed as the same serial number on
interface 0. UNVERIFIED on the Pi: check with `ls -l /dev/serial/by-id/` after plugging in.

The user the bridge runs as must be in the `dialout` group:

```
sudo usermod -aG dialout $USER
```

(Log out and back in for that to take.)

ModemManager, if it's installed (`systemctl status ModemManager`), probes every new ttyACM device
by sending it AT commands. The C6 answers each with `{"error":"bad_json"}`, which is harmless, but
ModemManager also holds the port for a few seconds. A udev rule keeps it away and gives the C6 a
fixed name at the same time. In `/etc/udev/rules.d/99-tcomm-gateway.rules`:

```
SUBSYSTEM=="tty", ATTRS{idVendor}=="303a", ATTRS{idProduct}=="1001", ATTRS{serial}=="FC:01:2C:FD:D3:A8", SYMLINK+="tcomm-gateway", ENV{ID_MM_DEVICE_IGNORE}="1"
```

then

```
sudo udevadm control --reload && sudo udevadm trigger
```

and the port is `/dev/tcomm-gateway`. UNVERIFIED on the Pi.

## Opening the port

It's USB, so the baud rate is ignored (115200 is fine). 8N1, no flow control. pyserial's defaults
are right:

```python
ser = serial.Serial(PORT, 115200, timeout=0.2, exclusive=True)
```

`exclusive=True` stops a second program opening the port by mistake.

Leave DTR and RTS alone. The chip's USB port resets the C6 when RTS is set while DTR is clear;
that's how esptool resets it. Opening the port raises both together, which doesn't. So never set
`ser.dtr = False` on its own. From Windows, opening and reopening the port didn't reset the C6.
The same check on the Pi is still to do: `sudo systemctl restart tcomm-bridge` should produce no
`{"boot":true,...}`.

After opening, send `{"cmd":"hello"}`. The `{"boot":false,...}` reply comes within a few ms and
says the C6 is there. While the port is closed, the C6 keeps what it writes (up to 2 KB), and it
all comes out when the port opens. So ignore any reply whose id this run of the bridge didn't
send.

## The serial protocol, as built

It's one JSON object per line, ending in `\n`; a CR before it is ignored. Lines over 512 bytes
are rejected, and blank lines are ignored.

### Pi to C6

```
{"cmd":"hello"}
{"type":"outside","id":12,"temp_tenths_f":485,"humidity":71,"forecast":1,"flags":1,"time":1793000000,"phrase":"Settled Fine"}
{"type":"lucy","id":13,"out":1,"since":1793000400}
```

| Field | Allowed | Notes |
|---|---|---|
| `id` | 1 to 2^53 - 1 | Required on `outside` and `lucy`. Use a new one for every request, retries included. |
| `temp_tenths_f` | -32768 to 32767 | 485 is 48.5 °F; the panel shows it rounded, 49°F |
| `humidity` | 0 to 100 | |
| `forecast` | 0 to 9, not 6 | Table below |
| `flags` | 0 to 7 | 1 valid, 2 danger heat, 4 danger cold. The panel ACKs a reading without the valid bit but doesn't show it, so set 1 on every real reading. |
| `time` | 0 to 4294967295 | UTC seconds when the reading was taken; 0 if the Pi's clock isn't set |
| `phrase` | any string, even "" | Required. Cut to 48 characters; anything outside printable ASCII becomes `?` |
| `out` | 0 or 1 | 1 Lucy is out, 0 she's back in |
| `since` | 0 to 4294967295 | UTC seconds of the press; 0 if the Pi's clock isn't set |

Whole numbers must be JSON integers: `1.0` and `true` are rejected. Python writes a `bool` as
`true`, so always send `int`. Fields the C6 doesn't use are ignored. It reads up to 12 fields a
line, and whitespace between the parts is fine.

| `forecast` | The panel shows |
|---|---|
| 0 | a dash (no forecast; use with "No Pressure Data") |
| 1 | sun (the moon at night) |
| 2 | sun behind a cloud (the moon behind a cloud at night) |
| 3 | cloud |
| 4 | rain |
| 5 | snow |
| 6 | never send: the panels' own night sky |
| 7 | thunder |
| 8 | clearing |
| 9 | heavy rain |

The panel drops a reading more than 15 minutes old by its own clock, so `time` has to be right
(NTP), or 0. With `time` 0 the panel counts the reading's age from when it arrived.

### C6 to Pi

A JSON line always starts with `{`. Anything else is a note for a person reading the log, so
ignore it in the bridge (but logging it helps). The C6's notes start `gw: `, the chip's start-up
text starts `ESP-ROM`, and any system error starts `E (`.

The first line after every restart, power-up included:

```
{"boot":true,"mac":"fc:01:2c:fd:d3:a8","channel":1,"fw":"gateway-c6 0.1"}
```

The reply to hello:

```
{"boot":false,"mac":"fc:01:2c:fd:d3:a8","channel":1,"fw":"gateway-c6 0.1"}
```

Exactly one final reply per request with a valid id. These are real ones from the tests:

```
{"type":"lucy","id":202,"acked":"upstairs","seq":53399,"ms":40}
{"type":"lucy","id":204,"superseded":true,"seq":53401}
{"type":"outside","id":301,"rejected":"forecast must be 0 to 9, not 6"}
{"type":"lucy","id":13,"failed":"upstairs","seq":4662}
```

`ms` counts from the first try to the panel's ACK. `seq` is the radio frame's sequence number,
useful only for matching the panel's own log. A request whose id is missing or not a whole number
is rejected without an id, since there's nothing to answer to:

```
{"type":"lucy","rejected":"id must be a positive whole number"}
```

The rejection reasons, word for word:

```
id must be a positive whole number
temp_tenths_f must fit an int16
humidity must be 0 to 100
forecast must be 0 to 9, not 6
flags must be 0 to 7
time must fit a u32
phrase must be a string
out must be 0 or 1
since must fit a u32
```

A line that isn't one JSON object (bad JSON, over 512 bytes) gets `{"error":"bad_json"}`. JSON
with neither `"cmd":"hello"` nor a known `type` gets `{"error":"unknown_type"}`, and so does any
other `cmd`. Neither has an id.

### Reply timing

The C6 sends at once and retries at 150, 400, 750 and 1150 ms, stopping at the panel's ACK.
Measured from the laptop:

- An `acked` reply came 15 to 153 ms after the request was written (median 46).
- A `failed` reply comes about 1.5 s after the first try, with no ACK by then.
- A `superseded` reply comes the moment a newer request of the same type arrives while the older
  one is still waiting for its ACK. A newer reading supersedes an older reading, and a newer Lucy
  press supersedes an older press. A reading never supersedes a press, or the other way round:
  the two are retried independently. Sending out and in back to back gives `superseded` for the
  out and `acked` for the in.

A restart of the C6 loses whatever it was doing, with no final reply for it. The boot line is the
sign of that.

Two USB problems turned up in testing, both in the Arduino core's USB serial driver, and the C6
now works around both. A reply exactly 64 bytes long (one full USB packet, which is most Lucy
acks) was held back by the computer until the next line came, sometimes seconds later. Linux does
the same as Windows here, so the Pi would have seen it. The fixed firmware ends every write
properly. If replies ever seem to arrive only when the bridge sends something, that's this
problem back; tell TComm.

## What the bridge should do

- One process owns the port (the bridge, as already planned). Two writers would interleave lines
  and steal each other's replies.
- Open the port, send hello, and wait for the reply. If the open fails or the port goes away (C6
  unplugged), close it, wait a few seconds, and try again.
- Match replies by `id`, and ignore replies to ids this run didn't send.
- Allow 10 s for a final reply. The real worst case is about 1.6 s, so 10 s means something is
  wrong (the C6 gone, or a restart).
- On `{"boot":true,...}` the C6 has restarted. Treat every request still waiting as failed, and
  send the current Lucy state again.
- LUCY: on `failed`, send the same state again with a new id every 30 s until it's acked or a
  newer press replaces it. Repeats change nothing on the panel. A `superseded` press needs
  nothing: the newer one replaced it.
- OUTSIDE: when there's a new reading, no more than once a minute and at least every 5 minutes.
  On `failed`, just wait for the next one.
- Log the `gw:` lines. They say what went wrong (next section).

## When something fails

After a `failed` reply, the C6 adds one note saying whether any radio heard the tries:

```
gw: no ACK, and no radio took any of 5 tries: panel off, too far, or on another channel?
gw: no ACK, though a radio took 5 of 5 tries: panel radio off in its build, or other keys?
```

The first means nothing heard it: the panel is off, too far away, or not listening on channel 1.
The second means the panel's radio heard the frames but the panel didn't take them: it's running
a build without its radio, or one built with different keys. Neither is something the Pi can fix.
Both notes are UNVERIFIED on hardware so far, since nothing failed in the tests.

| What the bridge sees | Likely cause | What to do |
|---|---|---|
| No `/dev/ttyACM*` at all | charge-only cable, or a bad port | another cable or port |
| Permission denied | not in `dialout` | `usermod`, then log in again |
| Port busy | ModemManager, or another program | the udev rule above; `exclusive=True` |
| `bad_json` lines nobody sent | ModemManager probing | the udev rule above |
| `boot:true` whenever the bridge restarts | DTR/RTS being changed on open | leave them alone |
| Every request `failed`, "no radio took any" | panel off, far, or not on channel 1 | check the panel; tell TComm |
| Every request `failed`, "a radio took" | panel built without its radio, or with other keys | tell TComm |
| A `gw:` line at start-up (no secrets, MAC not set, radio error) | the C6's firmware | tell TComm |
| A reading acked but the panel shows dashes | `flags` without 1, or `time` over 15 minutes old | set flags 1; check the clock |
| Replies arrive only when the bridge sends the next line | the USB problem above has come back | tell TComm |

## The C6's screen

From the top: TCOMM GATEWAY; its MAC; the channel and firmware; PI: WAITING (yellow) until the
first line from the Pi, then PI: TALKING (green); how many requests were ACKed and how many failed
(plus BAD with the number rejected, once there are any); and the last result (green when acked,
red when failed, yellow when rejected). A red or yellow line along the bottom means the firmware
has a problem; tell TComm. (The colours are as designed; nobody has checked them by eye since the
last change to the layout.)

## The panel's side

The panel only ACKs when it's built with its radio on, which TComm does with
`bash firmware/tools/fw.sh radio`. That build is on the panel now. TComm's demo build has no radio, so with
it every request fails with the "no radio took any" note. Until the second panel arrives, the
panel's other-floor tile says NO SIGNAL, which is expected. Once there's a downstairs panel, the
upstairs one passes readings and Lucy presses on to it. Nothing changes on the Pi's side.

## A smoke test from the Pi

Before the bridge, this checks the C6 on its own. It sends hello, a reading, and Lucy out then
back in, printing every line that comes back. UNVERIFIED on the Pi; written for pyserial
(`sudo apt install python3-serial`).

```python
import json, time, serial

PORT = '/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_FC:01:2C:FD:D3:A8-if00'

with serial.Serial(PORT, 115200, timeout=0.2, exclusive=True) as s:
    def send(obj):
        s.write((json.dumps(obj, separators=(',', ':')) + '\n').encode('ascii'))

    def show(seconds):
        end = time.time() + seconds
        while time.time() < end:
            line = s.readline().decode('ascii', 'replace').strip()
            if line:
                print(line)

    show(0.5)  # anything the C6 kept while the port was closed
    send({'cmd': 'hello'}); show(1)
    now = int(time.time())
    send({'type': 'outside', 'id': 1, 'temp_tenths_f': 485, 'humidity': 71, 'forecast': 1,
          'flags': 1, 'time': now, 'phrase': 'Settled Fine'}); show(2)
    send({'type': 'lucy', 'id': 2, 'out': 1, 'since': now}); show(2)
    send({'type': 'lucy', 'id': 3, 'out': 0, 'since': now}); show(2)
```

With the panel on and its radio build running, expect the hello reply and then three `acked`
lines. The panel should show 49°F, 71%, the sun (or the moon at night) and "Settled Fine", then
LUCY OUT for a moment.

## Who owns what

The C6's firmware, its keys, and the panels' MACs belong to TComm. The C6 is rebuilt and
reflashed from Mike's laptop with TComm's tools, never from the Pi, so don't run esptool against
it on the Pi. If the protocol needs to change, or the C6 misbehaves, write it up for the TComm
session, and Mike carries it across.

Still to check on the Pi, and worth reporting back to TComm:

- `ls -l /dev/serial/by-id/` shows the name above.
- `sudo systemctl restart tcomm-bridge` produces no `{"boot":true,...}`.
- A `failed` reply and its `gw:` note. The simplest test is to unplug the panel and send a Lucy
  press.
- A day of real use: readings every few minutes, a few Lucy presses, and no stuck replies.
