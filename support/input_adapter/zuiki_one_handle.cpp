// Zuiki One-Handle MasCon (ZKNS-001 family): handle on ABS_Y, range 0..255. All editions are
// electrically identical, so they share one profile.

#include <linux/input.h>
#include "mascon2ddg.h"

// direct raw->notch table, built at compile time
struct zuiki_notch_table
{
	constexpr zuiki_notch_table()
	{
		v[  0] = NOTCH_EB; v[  5] = NOTCH_B8; v[ 19] = NOTCH_B7; v[ 32] = NOTCH_B6;
		v[ 46] = NOTCH_B5; v[ 60] = NOTCH_B4; v[ 73] = NOTCH_B3; v[ 87] = NOTCH_B2;
		v[101] = NOTCH_B1; v[128] = NOTCH_N;  v[159] = NOTCH_P1; v[183] = NOTCH_P2;
		v[206] = NOTCH_P3; v[230] = NOTCH_P4; v[255] = NOTCH_P5;
	}
	constexpr operator const uint8_t*() const { return v; }

private:
	uint8_t v[256] = {};
};
static constexpr zuiki_notch_table zuiki_one_handle_notches;

static const uint32_t zuiki_one_handle_ids[] =
{
	mascon_id(0x33DD, 0x0001), // One Handle MasCon for Nintendo Switch
	mascon_id(0x33DD, 0x0002), // 1st-anniversary translucent
	mascon_id(0x33DD, 0x0003), // red
	mascon_id(0x33DD, 0x0004), // blue
	mascon_id(0x33DD, 0x0005), // black
	mascon_id(0x0F0D, 0x00C1), // Densha de GO! edition
};

extern const mascon_device zuiki_one_handle =
{
	ABS_Y,
	zuiki_one_handle_notches,
	zuiki_one_handle_ids,
	sizeof(zuiki_one_handle_ids) / sizeof(zuiki_one_handle_ids[0]),
};
