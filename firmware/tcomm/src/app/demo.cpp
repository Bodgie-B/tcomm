#include "demo.h"
#include <string.h>

namespace tc {

void DemoPeerPlatform::radio_send(uint8_t to, const uint8_t *data, size_t len) { demo_.from_peer(to, data, len); }

Demo::Demo(Platform &host) : host_(host), pp_(host, *this) {}

Demo::~Demo() { delete peer_; }

void Demo::boot_peer() {
  delete peer_;
  peer_ = new Controller(pp_);
  peer_->begin(peer_settings_, pp_.own, pp_.dog, pp_.mail);
  if (peer_reading_) peer_->on_local_reading(true, peer_temp_, peer_hum_);  // default: board sample 69 F, 44 %
}

void Demo::begin(Controller &panel) {
  panel_ = &panel;
  peer_settings_ = panel.settings();
  peer_settings_.floor = other_floor(panel.settings().floor);
  pp_.own.status = Status::Busy;
  WallTime w = host_.wall();
  pp_.own.since = w.valid ? w.utc - 25 * 60 : 0;
  pp_.dog = panel.dog();
  pp_.mail = panel.mail();
  boot_peer();
}

void Demo::queue(bool to_panel, const uint8_t *data, size_t len) {
  if (cut_ || len > kMaxMsg || q_n_ >= (int)(sizeof(q_) / sizeof(q_[0]))) return;
  Frame &f = q_[q_n_++];
  memcpy(f.data, data, len);
  f.len = (uint8_t)len;
  f.to_panel = to_panel;
}

void Demo::from_panel(uint8_t to, const uint8_t *data, size_t len) {
  if (to == kSenderGateway) return;  // ACKs to the fake gateway go nowhere
  queue(false, data, len);
}

void Demo::from_peer(uint8_t to, const uint8_t *data, size_t len) {
  if (to == kSenderGateway) return;
  queue(true, data, len);
}

void Demo::tick() {
  if (!panel_) return;
  // Deliver what was queued before this tick; replies generated now go out on the next one,
  // which gives the loopback a realistic one-tick latency.
  int n = q_n_;
  memcpy(batch_, q_, sizeof(Frame) * n);
  q_n_ = 0;
  for (int i = 0; i < n; i++) {
    if (batch_[i].to_panel) panel_->on_radio(batch_[i].data, batch_[i].len);
    else if (!cut_) peer_->on_radio(batch_[i].data, batch_[i].len);
  }
  if (cut_) return;  // an unplugged panel does nothing
  peer_->tick();

  // The simulated person answers the doorbell after 3 s.
  uint32_t now = host_.mono_ms();
  bool showing = peer_->view().banner.kind == BannerKind::Knock;
  if (showing && !banner_pending_) {
    banner_pending_ = true;
    banner_seen_ = now;
  }
  if (!showing) banner_pending_ = false;
  if (banner_pending_ && auto_ != AutoAnswer::Off && ms_diff(now, banner_seen_) >= 3000) {
    peer_->answer_banner(auto_ == AutoAnswer::ComeOn);
    banner_pending_ = false;
  }

  // A simulated tenant takes the mail in 20 s, unless the other floor's answers are off.
  bool mail = peer_->settings().floor == Floor::Downstairs && peer_->mail().waiting;
  if (mail && !mail_pending_) {
    mail_pending_ = true;
    mail_seen_ = now;
  }
  if (!mail) mail_pending_ = false;
  if (mail_pending_ && auto_ != AutoAnswer::Off && ms_diff(now, mail_seen_) >= 20000) {
    peer_->tap_mail();
    mail_pending_ = false;
  }
}

void Demo::peer_next_status() {
  if (cut_) return;
  status_i_ = (status_i_ + 1) % kStatusCount;
  Status s = (Status)status_i_;
  if (s == peer_->own().status) s = (Status)((status_i_ + 1) % kStatusCount);
  peer_->choose_status(s);
}

void Demo::peer_toggle_unplugged() {
  cut_ = !cut_;
  q_n_ = 0;
  if (!cut_) boot_peer();  // plugging in reboots it: RAM is lost, flash is kept
}

void Demo::peer_ring() {
  if (!cut_) peer_->tap_bell();
}

void Demo::peer_toggle_dog() {
  if (!cut_) peer_->tap_dog();
}

void Demo::peer_tap_mail() {
  if (!cut_) peer_->tap_mail();
}

void Demo::gateway_send(Msg m) {
  m.sender = kSenderGateway;
  m.seq = gw_seq_++;
  uint8_t buf[kMaxMsg];
  size_t n = encode(m, buf, sizeof(buf));
  if (!n) return;
  // The gateway talks to the upstairs panel only.
  bool panel_is_up = panel_ && panel_->settings().floor == Floor::Upstairs;
  if (panel_is_up) panel_->on_radio(buf, n);
  else if (!cut_) peer_->on_radio(buf, n);
}

void Demo::set_peer(const OwnState &s, bool has_reading, int16_t t, uint16_t h) {
  pp_.own = s;
  peer_reading_ = has_reading;
  peer_temp_ = t;
  peer_hum_ = h;
  if (!cut_) boot_peer();
}

void Demo::gateway_outside(int16_t t, uint8_t h, Forecast f, const char *phrase, uint8_t warn) {
  WallTime w = host_.wall();
  Msg m;
  m.type = MsgType::Outside;
  m.u.outside.flags = kOutsideValid | warn;
  m.u.outside.temp_tenths_f = t;
  m.u.outside.humidity = h;
  m.u.outside.forecast = (uint8_t)f;
  m.u.outside.time = w.valid ? w.utc : 0;
  strncpy(m.u.outside.phrase, phrase, kMaxPhrase);
  gateway_send(m);
}

void Demo::gateway_toggle_lucy() {
  // A press toggles what the panels show now, not what this fake gateway last sent.
  lucy_out_ = !(panel_ && panel_->dog().dog[0].out);
  WallTime w = host_.wall();
  Msg m;
  m.type = MsgType::Lucy;
  m.u.lucy.out = lucy_out_ ? 1 : 0;
  m.u.lucy.since = w.valid ? w.utc : 0;
  gateway_send(m);
}

void Demo::gateway_next_outside() {
  // The six board samples (made-up data from the design boards), then WeatherGate's Zambretti
  // texts with each symbol and both warnings (made-up readings).
  struct Sample { int16_t t; uint8_t h; Forecast f; const char *phrase; uint8_t warn; };
  static const Sample kSamples[] = {
      {480, 71, Forecast::Moon, "Clear, cooler", 0}, {440, 80, Forecast::CloudSun, "Fair, mild", 0},
      {310, 78, Forecast::Snow, "Snow likely", 0},    {630, 52, Forecast::Rain, "Rain likely", 0},
      {590, 58, Forecast::Sun, "Settled, fine", 0},   {540, 63, Forecast::CloudSun, "Showery, bright", 0},
      {720, 66, Forecast::Thunder, "Thunder possible", 0}, {550, 70, Forecast::Cloud, "Cloudy", 0},
      {620, 55, Forecast::Clearing, "Fairly Fine, Improving", 0},
      {540, 88, Forecast::HeavyRain, "Rain at Times, Becoming Very Unsettled", 0},
      {660, 91, Forecast::Thunder, "Stormy, Much Rain", 0},
      {560, 74, Forecast::Cloud, "Changeable, Mending", 0},
      {180, 70, Forecast::Snow, "Snow at Times, Worse Later", kOutsideDangerCold},
      {1010, 48, Forecast::Sun, "Settled Fine Weather", kOutsideDangerHeat},
  };
  const Sample &s = kSamples[outside_i_];
  outside_i_ = (outside_i_ + 1) % (int)(sizeof(kSamples) / sizeof(kSamples[0]));
  WallTime w = host_.wall();
  Msg m;
  m.type = MsgType::Outside;
  m.u.outside.flags = kOutsideValid | s.warn;
  m.u.outside.temp_tenths_f = s.t;
  m.u.outside.humidity = s.h;
  m.u.outside.forecast = (uint8_t)s.f;
  m.u.outside.time = w.valid ? w.utc : 0;
  strncpy(m.u.outside.phrase, s.phrase, kMaxPhrase);
  gateway_send(m);
}

void Demo::next_auto_answer() { auto_ = (AutoAnswer)(((int)auto_ + 1) % 3); }

int Demo::next_phase() {
  phase_ = (phase_ + 1) % 7;  // 0-5 forced phases, 6 = follow the clock
  if (panel_) panel_->force_phase(phase_);
  return phase_;
}

}  // namespace tc
