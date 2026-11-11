// boot_screens.h - Flight Board boot screens (792 x 272, 1 bit, 1 = black). Original artwork.
// Shown in order, one per power-on. Version and serial number are drawn on top by the board.
#pragma once
#include <stdint.h>

const int16_t BOOT_W = 792, BOOT_H = 272;
const int16_t VER_RIGHT = 782, VER_Y = 264;        // "v0.5.3", right-aligned, BOOT_14R (HUD, PFD, ND)
const int16_t DWG_REV_X = 694, DWG_REV_Y = 235;   // version after 'REV' on the drawing, BOOT_14
const int16_t PLATE_X = 230, PLATE_SERIAL_Y = 126, PLATE_FW_Y = 216;   // data plate fields, BOOT_18T
enum BootStyle { BOOT_STYLE_VERSION, BOOT_STYLE_DRAWING, BOOT_STYLE_PLATE };






struct BootScreen { const uint8_t* bitmap; BootStyle style; };

// v0.9.16: the same screens, smooth, in 4 greys (black wherever Classic is black) (Waveshare 4-grey buffer layout, 198 bytes a row:
// 4 pixels a byte, leftmost in the top bits; 11 white, 01 light grey, 10 dark grey, 00 black)
const int32_t BOOT_GREY_BYTES = 53856;

// The pictures are in boot_screens.cpp (compiled once and cached by the IDE)
extern const BootScreen BOOT_SCREENS[];
extern const int NUM_BOOT_SCREENS;
extern const uint8_t* const BOOT_GREY[];
