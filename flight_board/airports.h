// airports.h - built-in airports for the Flight Board
// Every US public-use airport (incl. Alaska and Hawaii) plus large and airline-served international airports.
// Source: OurAirports (ourairports.com), public domain. ident = unique OurAirports/ICAO id (what gets saved);
// code = what people type and see: FAA code in the US, IATA code elsewhere. city = for the board header.
#pragma once
#include <stdint.h>

struct Airport { const char* ident; const char* code; const char* name; const char* city; const char* region; float lat; float lon; int16_t elevFt; bool us; };
// The list itself is in airports.cpp (compiled once and cached by the IDE)
extern const Airport AIRPORTS[];
extern const int NUM_AIRPORTS;
