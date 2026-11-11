// Page background: the Reno / Lake Tahoe / Fallon area of the FAA San Francisco VFR sectional chart
// (v0.9.29: 2000 x 1277, about 2x the detail of the earlier 900 x 1063 image)
// (FAA charts are U.S. government works), softened for use behind the settings pages.
// Served by the board at /bg.jpg.
#pragma once
#include <Arduino.h>
const uint32_t BG_JPG_LEN = 300933;
// The JPEG bytes are in bg_image.cpp (compiled once and cached by the IDE)
extern const uint8_t BG_JPG[];
