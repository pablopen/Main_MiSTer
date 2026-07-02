// Device contract for the mascon -> Densha de GO! adapter: a controller file maps its raw handle
// axis to DDG notches; the console encodings and the translation live in mascon2ddg.cpp.

#ifndef MASCON2DDG_H
#define MASCON2DDG_H

#include <stdint.h>

// DDG handle positions: the discrete set every Densha de GO! controller exposes.
enum ddg_notch
{
	NOTCH_NONE = 0,   // no reading yet -> treated as neutral
	NOTCH_EB,
	NOTCH_B8, NOTCH_B7, NOTCH_B6, NOTCH_B5, NOTCH_B4, NOTCH_B3, NOTCH_B2, NOTCH_B1,
	NOTCH_N,
	NOTCH_P1, NOTCH_P2, NOTCH_P3, NOTCH_P4, NOTCH_P5,
	NOTCH_COUNT
};

static constexpr uint32_t mascon_id(uint16_t vid, uint16_t pid)
{
	return ((uint32_t)vid << 16) | pid;
}

// A physical mascon controller: which axis carries the handle, the raw-byte -> notch table,
// and the USB ids it ships under (built with mascon_id()).
struct mascon_device
{
	uint8_t     axis_code;
	const uint8_t *notches;   // 256 entries indexed by raw value; 0 = NOTCH_NONE
	const uint32_t *ids;
	int         num_ids;
};

#endif
