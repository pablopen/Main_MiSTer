// Input-adapter registry and dispatch. Built-in adapters register at init; XML-defined ones are
// loaded after them. Each adapter's translation lives in its own module.

#include <stdio.h>
#include "input_adapter.h"
#include "adapter_xml.h"
#include "../../file_io.h"

#define IA_MAX_ADAPTERS 12

// enable toggles persist per adapter as a tiny config blob, keyed by persist_key
struct ia_enable_blob { uint8_t version; uint8_t enable; };

static void enable_file(const input_adapter *a, char *name, int sz)
{
	snprintf(name, sz, "ia_%s.cfg", a->persist_key);
}

static const input_adapter *registry[IA_MAX_ADAPTERS];
static int reg_count = 0;

void input_adapter_register(const input_adapter *a)
{
	if (!a || reg_count >= IA_MAX_ADAPTERS)
	{
		if (a) printf("input_adapter: registry full, dropping %s\n", a->name);
		return;
	}
	registry[reg_count++] = a;

	if (a->persist_key && a->enable)
	{
		char name[64];
		enable_file(a, name, sizeof(name));
		ia_enable_blob b;
		if (FileLoadConfig(name, &b, sizeof(b)) && b.version == 1) *a->enable = b.enable ? 1 : 0;
	}
}

void input_adapter_init()
{
	static bool loaded = false;
	if (loaded) return;
	loaded = true;

	// built-in (C++) adapters register here, before XML-defined ones
	adapter_xml_load();
}

void input_adapter_set_enable(const input_adapter *a, int en)
{
	if (!a || !a->enable) return;
	*a->enable = en ? 1 : 0;

	if (a->persist_key)
	{
		char name[64];
		enable_file(a, name, sizeof(name));
		ia_enable_blob b = { 1, *a->enable };
		FileSaveConfig(name, &b, sizeof(b));
	}
}

const input_adapter *const *input_adapter_all(int *count)
{
	if (count) *count = reg_count;
	return registry;
}

const input_adapter *input_adapter_match(uint16_t vid, uint16_t pid, const void **dev)
{
	for (int i = 0; i < reg_count; i++)
	{
		const void *d = registry[i]->match(registry[i], vid, pid);
		if (d)
		{
			if (dev) *dev = d;
			return registry[i];
		}
	}
	if (dev) *dev = nullptr;
	return nullptr;
}

bool input_adapter_active(const input_adapter *a)
{
	if (!a) return false;
	if (a->enable && !*a->enable) return false;
	return a->supported ? a->supported(a) : true;
}
