// Turns interface actions into controller (and demo) calls. Shared by the panel firmware and the
// screenshot simulator. Plain C++: no LVGL here.
#pragma once
#include "../ui/ui.h"
#include "controller.h"
#include "demo.h"

namespace tc {

class PanelApp : public ui::Actions {
 public:
  PanelApp(Controller &c, Platform &p, Demo *demo) : c_(c), p_(p), demo_(demo) {}

  void choose_status(Status s) override { c_.choose_status(s); }
  void start_timer(Status s, uint32_t secs, Status fb) override { c_.start_timer(s, secs, fb); }
  void tap_bell() override { c_.tap_bell(); }
  void tap_dog() override { c_.tap_dog(); }
  void tap_mail() override { c_.tap_mail(); }
  void answer(bool come_on) override { c_.answer_banner(come_on); }
  void dismiss_banner() override { c_.dismiss_banner(); }
  void clean_screen(bool on) override { c_.set_clean_screen(on); }
  void preview(Sound s, uint8_t volume) override { p_.preview(s, volume); }
  uint32_t save_event(const Event &e) override { return c_.save_event(e); }
  bool delete_event(uint32_t id) override { return c_.delete_event(id); }
  bool restore_event(uint32_t id) override { return c_.restore_event(id); }
  uint32_t save_occurrence(uint32_t s, int32_t day, const Event &e) override { return c_.save_occurrence(s, day, e); }
  bool delete_occurrence(uint32_t s, int32_t day) override { return c_.delete_occurrence(s, day); }
  bool restore_occurrence(uint32_t s, int32_t day) override { return c_.restore_occurrence(s, day); }

  // Phase 3: with ESP-NOW on, the board's own MAC decides its floor (the radio drops frames whose
  // sender byte doesn't match the MAC), so the Settings swap is locked.
  void lock_floor(bool on) { floor_locked_ = on; }
  bool floor_locked() override { return floor_locked_; }
  bool updater_installed() override { return p_.updater_installed(); }
  void start_updater() override { p_.start_updater(); }

  void apply_settings(const Settings &in) override {
    Settings s = in;
    if (floor_locked_) s.floor = c_.settings().floor;
    bool floor_changed = s.floor != c_.settings().floor;
    c_.apply_settings(s);
    if (floor_changed && demo_) demo_->begin(c_);  // the simulated panel moves to the other floor
  }

  void set_clock_local(int64_t local) override {
    // Two passes find the right offset even across a daylight-saving change.
    int32_t off = p_.utc_offset((uint32_t)local);
    off = p_.utc_offset((uint32_t)(local - off));
    c_.set_clock_manual((uint32_t)(local - off));
  }

  ui::DemoInfo demo_info() override {
    ui::DemoInfo d;
    if (!demo_) return d;
    d.available = true;
    d.unplugged = demo_->unplugged();
    d.auto_answer = (int)demo_->auto_answer();
    d.phase = demo_->phase();
    p_.display_test_label(d.display, sizeof(d.display));
    d.strips = p_.strip_drawing();
    return d;
  }

  void demo(ui::DemoAction a) override {
    if (!demo_) return;
    switch (a) {
      case ui::kDemoPeerStatus: demo_->peer_next_status(); break;
      case ui::kDemoPeerPlug: demo_->peer_toggle_unplugged(); break;
      case ui::kDemoPeerRing: demo_->peer_ring(); break;
      case ui::kDemoPeerDog: demo_->peer_toggle_dog(); break;
      case ui::kDemoLucy: demo_->gateway_toggle_lucy(); break;
      case ui::kDemoOutside: demo_->gateway_next_outside(); break;
      case ui::kDemoAutoAnswer: demo_->next_auto_answer(); break;
      case ui::kDemoPhase: demo_->next_phase(); break;
      case ui::kDemoDisplay: p_.display_test_next(); break;
      case ui::kDemoStrips: p_.toggle_strip_drawing(); break;
      case ui::kDemoPeerMail: demo_->peer_tap_mail(); break;
    }
  }

 private:
  Controller &c_;
  Platform &p_;
  Demo *demo_;
  bool floor_locked_ = false;
};

}  // namespace tc
