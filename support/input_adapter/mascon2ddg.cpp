// Mascon -> Densha de GO! adapter: DDG console encodings and the shared translation; each
// recognised controller is defined in its own file.
// Device IDs and encodings from Marc Riera's https://github.com/marcriera/train-controller-db

#include <linux/input.h>
#include "input_adapter.h"
#include "mascon2ddg.h"
#include "../../cfg.h"
#include "../../user_io.h"

// ---- Layer A registry: the mascon devices this adapter recognises (one per file). ----
extern const mascon_device zuiki_one_handle;

static const mascon_device *const devices[] =
{
	&zuiki_one_handle,
};

// ---- Layer B: console encodings, one per console (notch -> that console's DDG buttons). ----
struct mascon_output
{
	uint32_t ident_mask;              // bits held permanently so the game detects the controller
	uint32_t reserved_mask;           // every bit the handle owns; cleared from the user mask
	void   (*prepare)();              // configure the core's controller type (optional)
	uint32_t notch_mask[NOTCH_COUNT]; // notch -> power/brake bits (ident added on apply)
};

// PlayStation Densha de GO!. Identity is UP+DOWN held; POWER1=Triangle/2=Left/3=Right, BRAKE1=L1/
// 2=L2/3=R1/4=R2. The inactive handle holds its rest combination (power neutral / brake released).
static constexpr uint32_t PSX_IDENT    = JOY_UP | JOY_DOWN;
static constexpr uint32_t PSX_RESERVED = JOY_UP | JOY_DOWN | JOY_LEFT | JOY_RIGHT | JOY_BTN1 | JOY_L | JOY_L2 | JOY_R | JOY_R2;

static constexpr uint32_t PWR_N  = JOY_LEFT | JOY_RIGHT;
static constexpr uint32_t PWR_P1 = JOY_BTN1 | JOY_RIGHT;
static constexpr uint32_t PWR_P2 = JOY_RIGHT;
static constexpr uint32_t PWR_P3 = JOY_BTN1 | JOY_LEFT;
static constexpr uint32_t PWR_P4 = JOY_LEFT;
static constexpr uint32_t PWR_P5 = JOY_BTN1;

static constexpr uint32_t BRK_REL = JOY_L2 | JOY_R | JOY_R2;
static constexpr uint32_t BRK_B1  = JOY_L  | JOY_R | JOY_R2;
static constexpr uint32_t BRK_B2  = JOY_R  | JOY_R2;
static constexpr uint32_t BRK_B3  = JOY_L  | JOY_L2 | JOY_R2;
static constexpr uint32_t BRK_B4  = JOY_L2 | JOY_R2;
static constexpr uint32_t BRK_B5  = JOY_L  | JOY_R2;
static constexpr uint32_t BRK_B6  = JOY_R2;
static constexpr uint32_t BRK_B7  = JOY_L  | JOY_L2 | JOY_R;
static constexpr uint32_t BRK_B8  = JOY_L2 | JOY_R;
static constexpr uint32_t BRK_EB  = 0;

// DDG controllers are standard digital pads; force Pad1 to Digital (PSX boots it as DualShock).
static void psx_prepare()
{
	user_io_set_option_by_name("Pad1", "Digital");
}

static const mascon_output psx_ddg =
{
	PSX_IDENT, PSX_RESERVED, psx_prepare,
	{
		[NOTCH_NONE] = PWR_N  | BRK_REL,
		[NOTCH_EB]   = PWR_N  | BRK_EB,
		[NOTCH_B8]   = PWR_N  | BRK_B8,
		[NOTCH_B7]   = PWR_N  | BRK_B7,
		[NOTCH_B6]   = PWR_N  | BRK_B6,
		[NOTCH_B5]   = PWR_N  | BRK_B5,
		[NOTCH_B4]   = PWR_N  | BRK_B4,
		[NOTCH_B3]   = PWR_N  | BRK_B3,
		[NOTCH_B2]   = PWR_N  | BRK_B2,
		[NOTCH_B1]   = PWR_N  | BRK_B1,
		[NOTCH_N]    = PWR_N  | BRK_REL,
		[NOTCH_P1]   = PWR_P1 | BRK_REL,
		[NOTCH_P2]   = PWR_P2 | BRK_REL,
		[NOTCH_P3]   = PWR_P3 | BRK_REL,
		[NOTCH_P4]   = PWR_P4 | BRK_REL,
		[NOTCH_P5]   = PWR_P5 | BRK_REL,
	},
};

static const mascon_output *output_for_current_core()
{
	if (is_psx()) return &psx_ddg;
	return NULL;
}

// ---- input_adapter driver ----
static const void *mascon2ddg_match(uint16_t vid, uint16_t pid)
{
	uint32_t key = mascon_id(vid, pid);
	for (const mascon_device *d : devices)
		for (int i = 0; i < d->num_ids; i++)
			if (d->ids[i] == key) return d;

	return NULL;
}

static bool mascon2ddg_supported()
{
	return output_for_current_core() != NULL;
}

static void mascon2ddg_prepare()
{
	const mascon_output *out = output_for_current_core();
	if (out && out->prepare) out->prepare();
}

static bool mascon2ddg_on_event(const void *dev, uint16_t type, uint16_t code, int value, uint32_t *state)
{
	const mascon_device *d = (const mascon_device *)dev;
	if (type == EV_ABS && code == d->axis_code)
	{
		*state = d->notches[(uint8_t)value];
		return true;
	}
	return false;
}

static void mascon2ddg_apply(const void *dev, uint32_t *state, uint32_t *joy)
{
	(void)dev;
	const mascon_output *out = output_for_current_core();
	if (!out) return;

	ddg_notch n = (*state < NOTCH_COUNT) ? (ddg_notch)*state : NOTCH_NONE;
	*joy = (*joy & ~out->reserved_mask) | out->ident_mask | out->notch_mask[n];
}

extern const input_adapter mascon2ddg_adapter =
{
	"Train Controller",
	&cfg.mascon_enable,
	mascon2ddg_match,
	mascon2ddg_supported,
	mascon2ddg_prepare,
	mascon2ddg_on_event,
	mascon2ddg_apply,
};
