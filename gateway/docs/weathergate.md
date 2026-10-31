# What WeatherGate sends to TComm

WeatherGate (the Raspberry Pi weather station in the carport) will send its readings and its
Zambretti forecast to the panels over ESP-NOW. This is what the panels are ready to receive.
The WeatherGate side isn't built yet, so nothing here has been tested against the real unit.

## The message

One OUTSIDE message per reading, sent to the **upstairs** panel, which passes it on to the
downstairs one. Layout as in `firmware/tcomm/src/core/protocol.h`, little-endian:

| Field | Size | Value |
|---|---|---|
| version | u8 | 1 |
| type | u8 | 8 (OUTSIDE) |
| sender | u8 | 2 (the gateway) |
| seq | u16 | counts up per message; start at a random number each boot |
| flags | u8 | bit 0 valid (always set), bit 1 danger heat, bit 2 danger cold |
| temp_tenths_f | i16 | outside temperature, tenths of a °F (`485` is 48.5 °F) |
| humidity | u8 | percent, 0–100 |
| forecast | u8 | the symbol code, below |
| time | u32 | UTC seconds when the reading was taken, or 0 if unknown |
| phrase_len | u8 | 0–48 |
| phrase | bytes | the forecast text, printable ASCII, no terminator |

The panel ACKs each message back to the gateway; resend up to 5 times over about 1.5 s
without one. A reading older than 15 minutes (by `time`, or since it arrived) shows as dashes.

## Forecast codes

WeatherGate's eight symbol keys (ZAMBRETTI_REFERENCE.md section 7), as codes:

| Code | Symbol key | Icon on the panel |
|---|---|---|
| 1 | `sunny` | sun; the moon at night |
| 2 | `partly_cloudy` | sun behind a cloud; the moon behind it at night |
| 3 | `cloudy` | a cloud |
| 4 | `rain` | cloud, three drops |
| 5 | `snow` | cloud, five flakes |
| 7 | `storm` | cloud, lightning bolt |
| 8 | `clearing` | sun and a small cloud breaking away; the moon at night |
| 9 | `heavy_rain` | cloud, five longer drops |
| 0 | (none) | a dash |

Send the symbol **after** WeatherGate's rain-to-snow swap, so a `heavy_rain` turned `snow` goes
as 5. Code 6 is the design boards' clear night: don't send it, the panel picks the moon itself
from the time of day. A code the panel doesn't know shows as the dash; the reading still shows.

## The text

Send WeatherGate's forecast text as it shows it, after the swap ("Snow at Times, Worse Later"),
or "No Pressure Data" when there's no pressure reading. The panel wraps it in the 124 px column
beside the icon, at most four lines, centred on the icon. Most texts fit at the design boards'
23 px. The few that don't step down to 20 px (texts with "Changeable,", too wide for the column
at 23) or 18 px ("Rain at Times, Becoming Very Unsettled", "Snow Showers, Becoming More
Unsettled"). Words are never broken. The longest Zambretti text is 38 characters; the limit is
48. A text longer than those may run past four lines.

## The warnings

Set the flag bits from WeatherGate's own `danger_heat` (heat index 100 °F or more) and
`danger_cold` (20 °F or less), independently; both can be set.

- **Danger heat:** the forecast icon is replaced by a bigger sun whose rays pulse once a second.
  The text stays beside it.
- **Danger cold:** icicles hang from the top of the screen, over the dog button, the date strip
  and the gear, some down to the upstairs tile, at 50 percent. The forecast icon stays. Touch
  goes through them.

## For a gateway that only knows the letter

`firmware/tcomm/src/core/zambretti.h` has WeatherGate's table: `zambretti_symbol(letter)` and
`zambretti_text(letter)` for A to Z (S included, though WeatherGate can't produce it yet), and
`forecast_from_key("heavy_rain")` for the symbol keys.

## Trying it without WeatherGate

Settings › Zambretti demo (Mike, 2026-10-25) lists all 26 Zambretti forecasts (A to Z), snow
(the rain-to-snow swap), danger heat and danger cold, each with a reading to suit it, where the
floor tiles are. The outside tile stays where it is and shows the one tapped, by day or by night
(the moon for the sun); danger cold hangs the icicles over the top of the screen. Back returns to
Settings, and the real reading comes back.

Settings › Test › "WeatherGate: next reading" steps through sample readings: the six design
boards' samples, then Zambretti texts with clearing, heavy rain, storm and cloudy, then danger
cold (18 °F, snow) and danger heat (101 °F, sunny).
