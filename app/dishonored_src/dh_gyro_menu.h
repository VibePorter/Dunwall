// Dishonored (2026-11-21) - gyro aiming rows in Options -> Controls (dh_gyro_menu.cpp). Called from the hooks that
// already exist on the same game functions: dh_main_menu.cpp (the options' category builders) and dh_control_scheme.cpp
// (Localize, applying the gamepad settings).
#pragma once

#include <cstdint>

#include <rex/ppc.h>

namespace dh::gyro_menu {

// One settings array (TArray<DisSetting>) with the gyro rows put in for one call of a builder, put back afterwards.
struct Injected {
  uint32_t array = 0, data = 0, num = 0, max = 0;
};
// Adds the four rows after the gamepad rows when `array` holds them (Control Scheme ... Invert Y); false = left alone.
bool Inject(PPCContext& ctx, uint8_t* base, uint32_t array, Injected& saved);
void Restore(uint8_t* base, const Injected& saved);

// Localize(FString* out r3, section r4, key r5, package r6, ...): the gyro mode's value names. True = handled (out
// filled, r3 = out); false = call the game's.
bool Localize(PPCContext& ctx, uint8_t* base);

// After the game applied the gamepad settings (control scheme): input_gyro_aim_button follows the button bound to the
// left hand (Power/Gadget). `input` = the PlayerInput object.
void AfterPadSettings(uint8_t* base, uint32_t input);

}  // namespace dh::gyro_menu
