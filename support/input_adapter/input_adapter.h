// Input adapters: recognise a specific USB controller and translate its raw events into what a
// core natively expects. Each adapter is one self-contained .cpp registered in input_adapter.cpp.

#ifndef INPUT_ADAPTER_H
#define INPUT_ADAPTER_H

#include <stdint.h>

struct input_adapter
{
	const char *name;                                   // identity + System-menu label
	uint8_t    *enable;                                 // cfg enable flag the menu toggles (NULL = always on)

	// Recognise a device by USB id, returning an opaque per-device handle (the adapter's own
	// profile) or NULL if this adapter does not handle it.
	const void *(*match)(uint16_t vid, uint16_t pid);

	bool        (*supported)();                          // current core is one this adapter translates for
	void        (*prepare)();                            // configure the core when enabled (optional)

	// Feed one raw input event from a matched device; the adapter updates its per-device state
	// word (*state is owned by the input layer; the adapter defines its meaning). Returns true if
	// it consumed the event, so the input layer skips normal processing of it.
	bool        (*on_event)(const void *dev, uint16_t type, uint16_t code, int value, uint32_t *state);

	// Write this device's current contribution into the player's digital button mask (JOY_* bits).
	void        (*apply)(const void *dev, uint32_t *state, uint32_t *joy);
};

// Registered adapters, for the menu to enumerate. Returns the array; *count gets its length.
const input_adapter *const *input_adapter_all(int *count);

// First registered adapter that recognises the device; sets *dev to its opaque handle. NULL if none.
const input_adapter *input_adapter_match(uint16_t vid, uint16_t pid, const void **dev);

// True if the adapter is enabled and supported on the currently loaded core.
bool input_adapter_active(const input_adapter *a);

#endif
