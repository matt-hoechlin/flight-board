// Web fonts for the settings pages, served by the board (no internet needed).
// Subset to Latin; WOFF format. All under the SIL Open Font License 1.1 (https://openfontlicense.org).
//   InstrumentSans-Regular: Copyright 2022 The Instrument Sans Project Authors (https://github.com/Instrument/instrument-sans)
//   InstrumentSans-Bold: Copyright 2022 The Instrument Sans Project Authors (https://github.com/Instrument/instrument-sans)
//   BigShoulders-Bold: Copyright 2019 The Big Shoulders Project Authors (https://github.com/xotypeco/big_shoulders)
//   GeistMono-Regular: Copyright 2024 The Geist Project Authors (https://github.com/vercel/geist-font.git)
#pragma once
#include <stdint.h>
const uint32_t WF_IS_LEN = 19808;
const uint32_t WF_ISB_LEN = 19964;
const uint32_t WF_BS_LEN = 15220;
const uint32_t WF_GM_LEN = 16440;
// The WOFF bytes are in web_fonts.cpp (compiled once and cached by the IDE)
extern const uint8_t WF_IS[];
extern const uint8_t WF_ISB[];
extern const uint8_t WF_BS[];
extern const uint8_t WF_GM[];
