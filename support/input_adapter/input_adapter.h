// Input adapters: recognise a specific USB controller and translate its raw events into what a
// core natively expects. Adapters are C++ modules or XML configs, both feeding one registry.

#ifndef INPUT_ADAPTER_H
#define INPUT_ADAPTER_H

#include <stdint.h>

struct input_adapter
{
	const char *name;                                   // identity + System-menu label
	const char *persist_key;                            // enable persistence key (NULL = not persisted)
	uint8_t    *enable;                                 // adapter-owned enable byte the menu toggles (NULL = always on)

	// Recognise a device by USB id, returning an opaque per-device handle (the adapter's own
	// profile) or NULL if this adapter does not handle it.
	const void *(*match)(const input_adapter *self, uint16_t vid, uint16_t pid);

	bool        (*supported)(const input_adapter *self); // current core is one this adapter translates for
	void        (*prepare)(const input_adapter *self);   // configure the core when enabled (optional)

	// Feed one raw input event from a matched device; the adapter updates its per-device state
	// word (*state is owned by the input layer; the adapter defines its meaning). Returns true if
	// it consumed the event, so the input layer skips normal processing of it.
	bool        (*on_event)(const input_adapter *self, const void *dev, uint16_t type, uint16_t code, int value, uint32_t *state);

	// Write this device's current contribution into the player's digital button mask (JOY_* bits).
	void        (*apply)(const input_adapter *self, const void *dev, uint32_t *state, uint32_t *joy);

	// Optional: fill per-stick/axis analog scale, signed 8.8 (0 = untouched, negative = inverted).
	void        (*axis_cfg)(const input_adapter *self, const void *dev, int16_t scale[2][2]);
};

// One-time setup: registers built-in adapters, then XML-defined ones. Cheap when re-entered.
void input_adapter_init();

// Add an adapter to the registry (bounds-checked); loads its persisted enable state.
void input_adapter_register(const input_adapter *a);

// Set the enable byte and persist it when the adapter has a persist_key.
void input_adapter_set_enable(const input_adapter *a, int en);

// Registered adapters, for the menu to enumerate. Returns the array; *count gets its length.
const input_adapter *const *input_adapter_all(int *count);

// First registered adapter that recognises the device; sets *dev to its opaque handle. NULL if none.
const input_adapter *input_adapter_match(uint16_t vid, uint16_t pid, const void **dev);

// True if the adapter is enabled and supported on the currently loaded core.
bool input_adapter_active(const input_adapter *a);

#endif
