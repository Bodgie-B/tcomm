# House events to phones and WhatsApp, for the WeatherGate Pi (2026-11-11)

For the WeatherGate project's Claude Code session, from the TComm session. Mike carries it across.

Mike wants his phone to buzz, and his family group chat to get a message, when certain things
happen at the house: a doorbell nobody answered, the tenant asking for heat or cooling in a hurry,
a dog left out too long. TComm's panels now report those events. Your side turns them into
messages. This brief says what the gateway prints and what Mike wants done with it. It asks for
behaviour only; how you build it is yours.

## What changed on the gateway

From firmware `gateway-c6 0.3` (the hello reply's `fw` says which), the gateway prints extra lines
on the same serial port the bridge already holds. They are unsolicited: nobody asked for them, they
have no `id`, and nothing needs to be sent back to the gateway for them. Until the new firmware is
flashed (Mike does that from the laptop with the flash command from the earlier brief), the gateway
prints none, so you can build and test with the old one.

The lines look like this, one per event, in one write each:

    {"event":"notice","kind":"knock","floor":"downstairs","value":0,"time":1793000000,"text":""}

- `event` is always `"notice"`. Treat any other `event` word you don't know as something to log
  and skip, not an error.
- `kind` is one of the seven words below.
- `floor` is `"upstairs"` or `"downstairs"`: who the event is about.
- `value` is a signed whole number.
- `time` is UTC seconds since 1970 of the event itself, or 0 when the panel had no clock.
- `text` is up to 40 printable ASCII characters, JSON-escaped, possibly empty.

| `kind` | What happened | `floor` | `value` | `text` |
|---|---|---|---|---|
| `knock` | a doorbell ring nobody answered in its minute, or one that rang while upstairs was set to Away | who rang | 0 unanswered, 1 rang while Away | empty |
| `arrow_urgent` | the tenant asked upstairs for heat or cooling, urgently | downstairs | +1 wants heat, -1 wants cooling | empty |
| `arrow_mild` | the same, not urgent | downstairs | +1 heat, -1 cooling | empty |
| `mail` | mail was marked for the tenant | downstairs | 1 | empty |
| `dog_out` | a dog has been out past its limit (10, 15 or 30 minutes, by the outside weather) | upstairs (ignore it) | minutes out so far | `LUCY`, `COPPER` or `BOTH` |
| `status` | a floor changed its status | whose status | 0 | `OPEN`, `BUSY`, `PRIVATE`, `SLEEPING` or `AWAY` |
| `note` | a floor wrote a message on its tile | whose message | 0 | the message, cut at 40 characters |

Only the first, second and fifth (`knock`, `arrow_urgent`, `dog_out`) are sent to begin with. The
panel has a switch for each kind, and the other four stay off until Mike turns them on in a later
firmware, so build all seven now. Which kinds the panel sends is not something you can see or
change from the Pi.

Things to know about the stream:

- Each event prints once per occurrence. A repeat is possible after the gateway restarts, or if the
  panel raises something again after its own restart. De-duplicate on the pair `(kind, time)` and
  keep the last few hours of pairs. For `dog_out`, `time` is when that outing began, so the "BOTH"
  line that follows a "COPPER" line for the same afternoon is a different notice (different `time`
  or `text`): include `text` in the key for `dog_out`, and `floor` for `status` and `note`.
- When `time` is 0 (the panel's clock wasn't set), use the moment you received the line, and skip
  the de-duplication for that line (the rate limit below still applies).
- A line can be old: while the bridge isn't running, the gateway keeps a little output and prints
  it when the port opens. Don't send a message for a line whose `time` is more than 15 minutes
  before you read it (log it instead), except when `time` is 0.
- The lines arrive among the replies to the OUTSIDE, LUCY and UPDATE requests. Tell them apart by
  the `"event"` key. The bridge's existing reply handling must not mistake one for a reply or an
  error, and a `notice` line must never be counted as the answer to anything.
- Never print or log what the gateway's keys are, and never write anything to the gateway because
  of a notice. A notice needs no reply.

## What Mike wants done with them

1. A phone notification for every notice that is switched on, in plain words.
2. A WhatsApp message to the family group chat for the same events.
3. The same wording on both. Short, calm, no emoji beyond what's below, the house's own words
   ("upstairs", "downstairs", "the dogs"). Suggestions, which Mike can change:

   | `kind` | Message |
   |---|---|
   | `knock`, value 0, `floor` downstairs | "The doorbell was rung from downstairs and nobody answered." |
   | `knock`, value 0, `floor` upstairs | "The doorbell was rung from upstairs and nobody answered." |
   | `knock`, value 1 | "Someone rang the doorbell from downstairs while upstairs was away." |
   | `arrow_urgent`, value +1 | "Downstairs is asking for heat, urgently." |
   | `arrow_urgent`, value -1 | "Downstairs is asking for cooling, urgently." |
   | `arrow_mild`, value +1 | "Downstairs would like it a little warmer." |
   | `arrow_mild`, value -1 | "Downstairs would like it a little cooler." |
   | `mail` | "Mail has been marked for downstairs." |
   | `dog_out`, text `LUCY` or `COPPER` | "Lucy has been out for 32 minutes." (the dog's name in sentence case, `value` as the minutes) |
   | `dog_out`, text `BOTH` | "Both dogs have been out for 32 minutes." |
   | `status` | "Upstairs is now Busy." / "Downstairs is now Sleeping." (the word in sentence case) |
   | `note` | "Upstairs wrote: <text>" / "Downstairs wrote: <text>" |

   Put the local time of the event in the phone notification's body, never in the group chat
   message unless Mike asks. Use the house time zone you already use for the weather.
4. A rate limit: no more than one message per `kind` per 10 minutes, to each destination. The
   first one goes at once; ones that arrive inside the window are dropped for the chat and the
   phone alike (log them), except that an urgent request that changes direction (`arrow_urgent`
   with the opposite `value` from the last one sent) is allowed through. Keep the counters in
   memory only, so a restart can't block anything. The limit is per kind, so a knock and a dog
   notice at the same time both go out.
5. Quiet hours: between 10 PM and 7 AM (house time, both configurable; the hours and the
   exceptions are my suggestion, for Mike to confirm), send the phone
   notification without sound and send nothing to the group chat, except `arrow_urgent`, `knock`
   with value 1, and `dog_out`, which Mike wants even at night. A notice held back for quiet hours
   is dropped, not saved for the morning.
6. An easy on/off switch per kind in a config file on the Pi: one line per kind (for example
   `knock = on`), plus one master switch and one switch each for the phone and WhatsApp
   destinations. Read it again when it changes (or every minute), so Mike can edit it over ssh with
   no restart. A kind that is off is logged and dropped. The default for all seven kinds is on;
   the panel decides which kinds exist at all.
7. If a destination fails (no network, the service says no), log it and move on. Never retry for
   more than a minute, never queue messages for later, and never let a sending problem slow down or
   stop the weather and Lucy work the bridge already does. Sending runs apart from the bridge's
   serial loop.

## The services (ask Mike before building)

Sending a phone notification and sending a WhatsApp message need outside services. Accounts, tokens
and phone numbers are Mike's to create and hold. Please propose options and wait for his choice
before building any sending code. What I know of them, to start from:

- **WhatsApp**: the only official route is the WhatsApp Business Cloud API from Meta. It needs a
  Meta business account, a registered phone number for the sender, message templates approved in
  advance, and a call to its API with an access token. Be straight with Mike about the limits: as
  far as I know, it sends to individual numbers rather than posting into an ordinary family group,
  so check whether it can reach his group chat at all today before promising it, and say what the
  alternatives would be (a separate small broadcast list of the family's numbers, or a different
  service) if it can't. Do not suggest unofficial WhatsApp Web scrapers or reverse-engineered
  libraries: they get numbers banned and are against the service's rules.
- **Phone notifications only**: a push service is far simpler. ntfy (self-hosted or its public
  server, with a long random topic name that is the secret) and Pushover (a one-time purchase per
  platform, an app token and a user key) both send a notification to a phone from one HTTPS call.
- A middle path Mike may like: push notifications to his own phone through one of those, and the
  family group chat message through the WhatsApp route, switched on separately.

Whatever Mike picks:

- Every token, key, topic name, phone number and group name lives only in the Pi project's own
  config file, which is untracked (git-ignored), readable only by the bridge's user, and never
  printed or logged. None of it goes into TComm's repository, which is public, or into the notice
  lines, or into this brief's follow-ups. If something has to be shown to Mike, show it masked.
- The messages carry only what the table above says. No addresses, no phone numbers of the
  tenant, no location, no times of when the house is empty.
- Ask Mike to create the accounts and paste the tokens into the Pi's config himself.

## Testing

- The notice lines can be tried without the panels: feed lines in the format above to the new
  code from a file or a test harness, and check the wording, the de-duplication, the rate limit,
  the quiet hours and the on/off switches with the sending stubbed out. Nothing in testing should
  send a real message until Mike has chosen the services and set the config up.
- With real firmware: after Mike has flashed `gateway-c6 0.3` and the new panel firmware, a doorbell
  ring from the downstairs panel left alone for a minute, or an urgent arrow sent from the
  downstairs survey, makes the gateway print a line. Please don't ask Mike to do these at night.
- The existing bridge behaviour must not change: OUTSIDE, LUCY and UPDATE requests, their replies,
  the hello check and the boot handling stay exactly as they are.

## What to send back to Mike

- The services you recommend and why, in a few sentences, with what each would cost him and what he
  has to set up. Wait for his answer before building the sending part.
- The config file's layout (the keys, not any real values) and where it lives.
- How the bridge tells a `notice` line from a reply, and where the de-duplication keys are kept.
- Anything in this brief that doesn't fit how the bridge is built, before building around it.
