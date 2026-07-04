// XML-defined input adapters: each config/inputadapters/*.xml file describes a controller
// translation (device ids, axis positions, per-core button encodings, analog scale) and is
// synthesised into a registry entry. Declarative only; adapters needing logic stay C++ modules.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <linux/input.h>
#include "../../sxmlc.h"
#include "../../user_io.h"
#include "../../file_io.h"
#include "input_adapter.h"
#include "adapter_xml.h"

#define ADAPTER_XML_DIR   "config/inputadapters"
#define IA_SCHEMA_VERSION 1
#define IA_MAX_FILE_SIZE  (16 * 1024)
#define IA_MAX_XML        8
#define IA_MAX_FILES      32
#define IA_MAX_IDS        16
#define IA_MAX_POS        24
#define IA_MAX_CORES      4
#define IA_MAX_PREPARES   4
#define IA_MAX_AXES       4

struct ia_prepare   { char option[24]; char value[24]; };
struct ia_axis_rule { uint8_t stick; uint8_t axis; int16_t scale_fp; };  // signed 8.8

struct ia_core_block
{
	char       core[16];                 // "*" = any core
	uint32_t   hold_mask;                // buttons held whenever the adapter is active
	uint32_t   reserved_mask;            // hold | OR(all position masks); computed at </core>
	uint32_t   pos_mask[IA_MAX_POS];
	ia_prepare prepares[IA_MAX_PREPARES];
	uint8_t    n_prepares;
	ia_axis_rule axes[IA_MAX_AXES];
	uint8_t    n_axes;
};

struct xml_adapter
{
	input_adapter iface;                 // first member: callbacks cast self back
	char      name[28];
	char      stem[32];                  // file stem = persist_key
	uint32_t  ids[IA_MAX_IDS];           // (vid<<16)|pid
	uint8_t   n_ids;
	uint8_t   axis_code;
	uint8_t   tolerance;
	uint8_t   n_pos;
	uint8_t   default_pos;               // 1-based; 0 = none until first reading
	int16_t   pos_value[IA_MAX_POS];
	char      pos_id[IA_MAX_POS][8];
	char      default_id[8];             // resolved to default_pos after parse
	ia_core_block cores[IA_MAX_CORES];
	uint8_t   n_cores;
	const ia_core_block *cur;            // per-process resolution for the running core
	uint8_t   resolved;
	uint8_t   enable;
};

// ---- vocabularies (resolved at parse time only) ----
struct name_val { const char *name; uint32_t val; };

static const name_val button_names[] =
{
	{ "right", JOY_RIGHT }, { "left", JOY_LEFT }, { "down", JOY_DOWN }, { "up", JOY_UP },
	{ "a", JOY_A }, { "b", JOY_B }, { "x", JOY_X }, { "y", JOY_Y },
	{ "select", JOY_SELECT }, { "start", JOY_START },
	{ "l", JOY_L }, { "r", JOY_R }, { "l2", JOY_L2 }, { "r2", JOY_R2 }, { "l3", JOY_L3 }, { "r3", JOY_R3 },
	{ "btn1", JOY_BTN1 }, { "btn2", JOY_BTN2 }, { "btn3", JOY_BTN3 }, { "btn4", JOY_BTN4 },
};

static const name_val axis_names[] =
{
	{ "ABS_X", 0 }, { "ABS_Y", 1 }, { "ABS_Z", 2 }, { "ABS_RX", 3 }, { "ABS_RY", 4 }, { "ABS_RZ", 5 },
	{ "ABS_THROTTLE", 6 }, { "ABS_RUDDER", 7 }, { "ABS_WHEEL", 8 }, { "ABS_GAS", 9 }, { "ABS_BRAKE", 10 },
	{ "ABS_HAT0X", 16 }, { "ABS_HAT0Y", 17 },
};

static const name_val dest_axis_names[] =  // val = (stick<<1)|axis
{
	{ "lx", 0 }, { "ly", 1 }, { "rx", 2 }, { "ry", 3 },
};

// ---- parse state (single static temp; input thread only) ----
static xml_adapter parse_tmp;
static struct
{
	bool failed;
	bool in_core;
	char reason[96];
} ps;

static void fail(const char *why)
{
	if (!ps.failed) snprintf(ps.reason, sizeof(ps.reason), "%s", why);
	ps.failed = true;
}

static const char *attr(const XMLNode *node, const char *name)
{
	for (int i = 0; i < node->n_attributes; i++)
		if (!strcasecmp(node->attributes[i].name, name)) return node->attributes[i].value;
	return NULL;
}

static bool parse_num(const char *s, long *out, int base)
{
	if (!s || !*s) return false;
	char *end;
	*out = strtol(s, &end, base);
	return !*end;
}

static bool parse_buttons(const char *s, uint32_t *mask)
{
	*mask = 0;
	if (!s) return false;
	char buf[128];
	snprintf(buf, sizeof(buf), "%s", s);
	char *save = NULL;
	for (char *tok = strtok_r(buf, " ", &save); tok; tok = strtok_r(NULL, " ", &save))
	{
		bool found = false;
		for (const name_val &b : button_names)
			if (!strcasecmp(tok, b.name)) { *mask |= b.val; found = true; break; }
		if (!found) return false;
	}
	return true;
}

// ---- SAX events ----
static void node_inputadapter(const XMLNode *node)
{
	long v = 1;
	const char *s = attr(node, "version");
	if (s && (!parse_num(s, &v, 10) || v > IA_SCHEMA_VERSION)) { fail("unsupported schema version"); return; }

	s = attr(node, "name");
	if (!s || !*s) { fail("missing name"); return; }
	snprintf(parse_tmp.name, sizeof(parse_tmp.name), "%s", s);

	parse_tmp.enable = 1;
	s = attr(node, "enabled");
	if (s) parse_tmp.enable = strcmp(s, "0") ? 1 : 0;
}

static void node_match(const XMLNode *node)
{
	long vid, pid;
	if (!parse_num(attr(node, "vid"), &vid, 16) || !parse_num(attr(node, "pid"), &pid, 16)
		|| vid < 0 || vid > 0xFFFF || pid < 0 || pid > 0xFFFF) { fail("bad match vid/pid"); return; }
	if (parse_tmp.n_ids >= IA_MAX_IDS) { printf("input_adapter: too many <match>, extra ignored\n"); return; }
	parse_tmp.ids[parse_tmp.n_ids++] = ((uint32_t)vid << 16) | (uint32_t)pid;
}

static void node_positions(const XMLNode *node)
{
	const char *s = attr(node, "axis");
	long code = -1;
	if (s)
	{
		for (const name_val &a : axis_names)
			if (!strcasecmp(s, a.name)) { code = a.val; break; }
		if (code < 0 && !parse_num(s, &code, 10)) code = -1;
	}
	if (code < 0 || code > 255) { fail("bad positions axis"); return; }
	parse_tmp.axis_code = (uint8_t)code;

	long tol = 0;
	s = attr(node, "tolerance");
	if (s && (!parse_num(s, &tol, 10) || tol < 0 || tol > 127)) { fail("bad tolerance"); return; }
	parse_tmp.tolerance = (uint8_t)tol;

	s = attr(node, "default");
	if (s) snprintf(parse_tmp.default_id, sizeof(parse_tmp.default_id), "%s", s);
}

static void node_pos(const XMLNode *node)
{
	const char *id = attr(node, "id");
	long value;
	if (!id || !*id || !parse_num(attr(node, "value"), &value, 10) || value < -32768 || value > 32767)
	{
		fail("bad <pos>");
		return;
	}
	if (parse_tmp.n_pos >= IA_MAX_POS) { fail("too many <pos>"); return; }
	snprintf(parse_tmp.pos_id[parse_tmp.n_pos], sizeof(parse_tmp.pos_id[0]), "%s", id);
	parse_tmp.pos_value[parse_tmp.n_pos] = (int16_t)value;
	parse_tmp.n_pos++;
}

static int pos_index(const char *id)
{
	for (int i = 0; i < parse_tmp.n_pos; i++)
		if (!strcasecmp(parse_tmp.pos_id[i], id)) return i;
	return -1;
}

static void node_core(const XMLNode *node)
{
	const char *s = attr(node, "name");
	if (!s || !*s) { fail("core without name"); return; }
	if (parse_tmp.n_cores >= IA_MAX_CORES) { fail("too many <core>"); return; }
	ia_core_block *c = &parse_tmp.cores[parse_tmp.n_cores++];
	snprintf(c->core, sizeof(c->core), "%s", s);
	ps.in_core = true;
}

static bool is_rule_tag(const char *t)
{
	return !strcasecmp(t, "prepare") || !strcasecmp(t, "hold") || !strcasecmp(t, "map")
		|| !strcasecmp(t, "axis") || !strcasecmp(t, "button");
}

static void node_core_child(const XMLNode *node)
{
	if (!ps.in_core) { fail("rule outside <core>"); return; }
	ia_core_block *c = &parse_tmp.cores[parse_tmp.n_cores - 1];

	if (!strcasecmp(node->tag, "prepare"))
	{
		const char *opt = attr(node, "option"), *val = attr(node, "value");
		if (!opt || !val) { fail("bad <prepare>"); return; }
		if (c->n_prepares >= IA_MAX_PREPARES) { fail("too many <prepare>"); return; }
		snprintf(c->prepares[c->n_prepares].option, sizeof(c->prepares[0].option), "%s", opt);
		snprintf(c->prepares[c->n_prepares].value, sizeof(c->prepares[0].value), "%s", val);
		c->n_prepares++;
	}
	else if (!strcasecmp(node->tag, "hold"))
	{
		if (!parse_buttons(attr(node, "buttons"), &c->hold_mask)) fail("bad <hold> buttons");
	}
	else if (!strcasecmp(node->tag, "map"))
	{
		const char *pos = attr(node, "pos");
		int idx = pos ? pos_index(pos) : -1;
		uint32_t mask;
		if (idx < 0) { fail("<map> with unknown pos"); return; }
		if (!parse_buttons(attr(node, "buttons"), &mask)) { fail("bad <map> buttons"); return; }
		c->pos_mask[idx] = mask;
	}
	else if (!strcasecmp(node->tag, "axis"))
	{
		const char *to = attr(node, "to"), *sc = attr(node, "scale");
		long dest = -1;
		if (to) for (const name_val &d : dest_axis_names) if (!strcasecmp(to, d.name)) { dest = d.val; break; }
		if (dest < 0) { fail("bad <axis> to"); return; }
		if (!sc) { fail("<axis> without scale"); return; }
		char *end;
		float f = strtof(sc, &end);
		float mag = f < 0 ? -f : f;
		if (*end || mag < 0.1f || mag > 4.0f) { fail("<axis> scale out of 0.1..4.0"); return; }
		if (c->n_axes >= IA_MAX_AXES) { fail("too many <axis>"); return; }
		c->axes[c->n_axes].stick = (uint8_t)(dest >> 1);
		c->axes[c->n_axes].axis = (uint8_t)(dest & 1);
		c->axes[c->n_axes].scale_fp = (int16_t)(f * 256.0f);
		c->n_axes++;
	}
	else if (!strcasecmp(node->tag, "button"))
	{
		printf("input_adapter: <button> rules not supported yet, ignored\n");
	}
}

static void validate_end_doc()
{
	if (ps.failed) return;
	if (!parse_tmp.name[0]) { fail("missing <inputadapter>"); return; }
	if (!parse_tmp.n_ids) { fail("no <match>"); return; }
	if (!parse_tmp.n_cores) { fail("no <core>"); return; }

	// a core with <map>/<hold> needs positions; pure <axis> configs don't
	for (int i = 0; i < parse_tmp.n_cores; i++)
	{
		ia_core_block *c = &parse_tmp.cores[i];
		bool has_maps = c->hold_mask != 0;
		for (int p = 0; p < IA_MAX_POS; p++) has_maps |= c->pos_mask[p] != 0;
		if (has_maps && !parse_tmp.n_pos) { fail("<map>/<hold> without <positions>"); return; }
	}

	// tolerance windows must not overlap
	for (int i = 0; i < parse_tmp.n_pos; i++)
		for (int j = i + 1; j < parse_tmp.n_pos; j++)
		{
			int d = parse_tmp.pos_value[i] - parse_tmp.pos_value[j];
			if (d < 0) d = -d;
			if (d <= 2 * parse_tmp.tolerance) { fail("overlapping position tolerances"); return; }
		}

	if (parse_tmp.default_id[0])
	{
		int idx = pos_index(parse_tmp.default_id);
		if (idx < 0) { fail("unknown default position"); return; }
		parse_tmp.default_pos = (uint8_t)(idx + 1);
	}
}

static int sax_cb(XMLEvent event, const XMLNode *node, SXML_CHAR *text, const int n, SAX_Data *sd)
{
	(void)text; (void)n;
	switch (event)
	{
	case XML_EVENT_START_DOC:
		memset(&parse_tmp, 0, sizeof(parse_tmp));
		memset(&ps, 0, sizeof(ps));
		break;

	case XML_EVENT_START_NODE:
		if (ps.failed) break;
		if (node->tag_type != TAG_FATHER && node->tag_type != TAG_SELF) break;  // comments, prolog, ...
		if (!strcasecmp(node->tag, "inputadapter")) node_inputadapter(node);
		else if (!strcasecmp(node->tag, "match")) node_match(node);
		else if (!strcasecmp(node->tag, "positions")) node_positions(node);
		else if (!strcasecmp(node->tag, "pos")) node_pos(node);
		else if (!strcasecmp(node->tag, "core")) node_core(node);
		else if (is_rule_tag(node->tag)) node_core_child(node);
		else printf("input_adapter: unknown tag <%s> ignored\n", node->tag);
		break;

	case XML_EVENT_END_NODE:
		if (!strcasecmp(node->tag, "core"))
		{
			ia_core_block *c = &parse_tmp.cores[parse_tmp.n_cores - 1];
			c->reserved_mask = c->hold_mask;
			for (int p = 0; p < IA_MAX_POS; p++) c->reserved_mask |= c->pos_mask[p];
			ps.in_core = false;
		}
		break;

	case XML_EVENT_ERROR:
		snprintf(ps.reason, sizeof(ps.reason), "XML error at line %d", sd ? sd->line_num : 0);
		ps.failed = true;
		break;

	case XML_EVENT_END_DOC:
		validate_end_doc();
		break;

	default:
		break;
	}
	return true;
}

// ---- registry callbacks (shared by all XML adapters; self identifies the file) ----
#define XA(self) ((xml_adapter *)(self))

static const ia_core_block *resolve_core(xml_adapter *xa)
{
	if (!xa->resolved)
	{
		xa->resolved = 1;
		const char *core = user_io_get_core_name(1);
		for (int i = 0; i < xa->n_cores; i++)  // exact match beats "*"
			if (!strcasecmp(xa->cores[i].core, core)) { xa->cur = &xa->cores[i]; return xa->cur; }
		for (int i = 0; i < xa->n_cores; i++)
			if (!strcmp(xa->cores[i].core, "*")) { xa->cur = &xa->cores[i]; break; }
	}
	return xa->cur;
}

static const void *xml_match(const input_adapter *self, uint16_t vid, uint16_t pid)
{
	xml_adapter *xa = XA(self);
	uint32_t key = ((uint32_t)vid << 16) | pid;
	for (int i = 0; i < xa->n_ids; i++)
		if (xa->ids[i] == key) return xa;
	return NULL;
}

static bool xml_supported(const input_adapter *self)
{
	return resolve_core(XA(self)) != NULL;
}

static void xml_prepare(const input_adapter *self)
{
	const ia_core_block *c = resolve_core(XA(self));
	if (!c) return;
	for (int i = 0; i < c->n_prepares; i++)
		user_io_set_option_by_name(c->prepares[i].option, c->prepares[i].value);
}

static bool xml_on_event(const input_adapter *self, const void *dev, uint16_t type, uint16_t code, int value, uint32_t *state)
{
	(void)dev;
	xml_adapter *xa = XA(self);
	if (!xa->n_pos || type != EV_ABS || code != xa->axis_code) return false;

	// nearest position within tolerance; out-of-window values keep the previous state
	int best = -1, best_d = 0;
	for (int i = 0; i < xa->n_pos; i++)
	{
		int d = value - xa->pos_value[i];
		if (d < 0) d = -d;
		if (best < 0 || d < best_d) { best = i; best_d = d; }
	}
	if (best >= 0 && best_d <= xa->tolerance) *state = (uint32_t)(best + 1);
	return true;  // the handle axis always belongs to the adapter
}

static void xml_apply(const input_adapter *self, const void *dev, uint32_t *state, uint32_t *joy)
{
	(void)dev;
	xml_adapter *xa = XA(self);
	const ia_core_block *c = resolve_core(xa);
	if (!c || !xa->n_pos) return;

	uint32_t n = *state ? *state : xa->default_pos;
	if (!n || n > xa->n_pos) return;
	*joy = (*joy & ~c->reserved_mask) | c->hold_mask | c->pos_mask[n - 1];
}

static void xml_axis_cfg(const input_adapter *self, const void *dev, int16_t scale[2][2])
{
	(void)dev;
	const ia_core_block *c = resolve_core(XA(self));
	if (!c) return;
	for (int i = 0; i < c->n_axes; i++)
		scale[c->axes[i].stick][c->axes[i].axis] = c->axes[i].scale_fp;
}

// ---- loader ----
static bool parse_file(const char *dir, const char *fname)
{
	char path[512];
	snprintf(path, sizeof(path), "%s/%s", dir, fname);

	struct stat st;
	if (stat(path, &st) || !S_ISREG(st.st_mode) || st.st_size > IA_MAX_FILE_SIZE)
	{
		printf("input_adapter: %s: not a regular file or over %d bytes, skipped\n", fname, IA_MAX_FILE_SIZE);
		return false;
	}

	SAX_Callbacks sax;
	SAX_Callbacks_init(&sax);
	sax.all_event = sax_cb;
	ps.failed = true;                    // a file sxmlc cannot even open stays rejected
	snprintf(ps.reason, sizeof(ps.reason), "cannot open");
	XMLDoc_parse_file_SAX(path, &sax, NULL);

	if (ps.failed)
	{
		printf("input_adapter: %s rejected: %s\n", fname, ps.reason);
		return false;
	}

	xml_adapter *xa = (xml_adapter *)calloc(1, sizeof(xml_adapter));
	if (!xa) return false;
	memcpy(xa, &parse_tmp, sizeof(xml_adapter));
	snprintf(xa->stem, sizeof(xa->stem), "%.*s", (int)(strlen(fname) - 4), fname);

	xa->iface.name = xa->name;
	xa->iface.persist_key = xa->stem;
	xa->iface.enable = &xa->enable;
	xa->iface.match = xml_match;
	xa->iface.supported = xml_supported;
	xa->iface.prepare = xml_prepare;
	xa->iface.on_event = xml_on_event;
	xa->iface.apply = xml_apply;
	xa->iface.axis_cfg = xml_axis_cfg;

	input_adapter_register(&xa->iface);
	printf("input_adapter: loaded %s (%s)\n", fname, xa->name);
	return true;
}

void adapter_xml_load()
{
	char dir[300];
	snprintf(dir, sizeof(dir), "%s/%s", getRootDir(), ADAPTER_XML_DIR);

	struct stat st;
	if (stat(dir, &st) || !S_ISDIR(st.st_mode)) return;   // no folder: the common case, free

	DIR *d = opendir(dir);
	if (!d) return;

	static char names[IA_MAX_FILES][64];
	int count = 0;
	struct dirent *de;
	while ((de = readdir(d)) && count < IA_MAX_FILES)
	{
		size_t len = strlen(de->d_name);
		if (len > 4 && len < sizeof(names[0]) && !strcasecmp(de->d_name + len - 4, ".xml"))
			snprintf(names[count++], sizeof(names[0]), "%s", de->d_name);
	}
	closedir(d);

	// filename sort order decides priority when two files claim the same device
	for (int i = 1; i < count; i++)
		for (int j = i; j > 0 && strcasecmp(names[j - 1], names[j]) > 0; j--)
		{
			char t[64];
			memcpy(t, names[j - 1], sizeof(t));
			memcpy(names[j - 1], names[j], sizeof(t));
			memcpy(names[j], t, sizeof(t));
		}

	int accepted = 0;
	for (int i = 0; i < count && accepted < IA_MAX_XML; i++)
		if (parse_file(dir, names[i])) accepted++;
}
