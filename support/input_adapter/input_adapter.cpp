// Input-adapter registry and dispatch. Lists the adapters and routes device matching / activation
// for the input layer; each adapter's translation lives in its own module.

#include "input_adapter.h"

extern const input_adapter mascon2ddg_adapter;

static const input_adapter *const adapters[] =
{
	&mascon2ddg_adapter,
};

const input_adapter *const *input_adapter_all(int *count)
{
	if (count) *count = (int)(sizeof(adapters) / sizeof(adapters[0]));
	return adapters;
}

const input_adapter *input_adapter_match(uint16_t vid, uint16_t pid, const void **dev)
{
	for (const input_adapter *a : adapters)
	{
		const void *d = a->match(vid, pid);
		if (d)
		{
			if (dev) *dev = d;
			return a;
		}
	}
	if (dev) *dev = nullptr;
	return nullptr;
}

bool input_adapter_active(const input_adapter *a)
{
	if (!a) return false;
	if (a->enable && !*a->enable) return false;
	return a->supported ? a->supported() : true;
}
