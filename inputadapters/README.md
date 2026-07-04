# Input adapters

Input adapters translate a special USB controller into what a core natively expects — in software,
no extra hardware. Drop an XML file into `/media/fat/config/inputadapters/` and the matching
controller gains a `- On/Off` row in the OSD System menu whenever a supported core is running.

Deleting the file removes the adapter. A malformed file is rejected as a whole (reason in
MiSTer.log); other files are unaffected. Users without this folder pay no cost.

## Schema (version 1)

```xml
<inputadapter version="1" name="Menu Label" enabled="1">
	<match vid="33DD" pid="0001"/>          <!-- USB ids, hex; repeatable -->

	<!-- optional: discrete handle/selector positions on one axis -->
	<positions axis="ABS_Y" tolerance="4" default="n">
		<pos id="n" value="128"/>            <!-- nearest raw value within tolerance wins -->
	</positions>

	<core name="PSX">                        <!-- or name="*" for any core; repeatable -->
		<prepare option="Pad1" value="Digital"/>  <!-- set a core OSD option when enabled -->
		<hold buttons="up down"/>            <!-- held constantly while the adapter is active -->
		<map pos="n" buttons="left right"/>  <!-- complete button set for that position -->
		<axis to="lx" scale="0.8"/>          <!-- analog scale; negative value inverts -->
	</core>
</inputadapter>
```

- **Buttons:** `up down left right a b x y select start l r l2 r2 l3 r3` (space-separated,
  OR'd together). The adapter overlays only the buttons it declares; everything else keeps the
  user's normal mapping.
- **Source axes:** `ABS_X ABS_Y ABS_Z ABS_RX ABS_RY ABS_RZ ABS_THROTTLE ABS_RUDDER ABS_WHEEL
  ABS_GAS ABS_BRAKE ABS_HAT0X ABS_HAT0Y`, or a raw evdev code number.
- **Destination axes** (`<axis to=…>`): `lx ly rx ry`. Scale is applied per core, magnitude
  clamped to 0.1..4.0.
- `enabled="0"` ships an adapter off by default; the OSD toggle is persisted per adapter.
- Unknown tags are ignored (older firmware can load newer files); `version` above the firmware's
  supported schema rejects the file.

## Examples

`zuiki_mascon.xml` (in this folder) — the Zuiki One-Handle MasCon (Switch train controller) as a
Densha de GO! controller on the PSX core: discrete positions to button combinations, plus a
`<prepare>` that switches Pad1 to Digital.

Per-core analog scale, e.g. a pad that over-ranges on the N64 core:

```xml
<inputadapter version="1" name="MyPad N64 Range" enabled="1">
	<match vid="0F0D" pid="00DC"/>
	<core name="N64">
		<axis to="lx" scale="0.8"/>
		<axis to="ly" scale="0.8"/>
	</core>
</inputadapter>
```

Adapters that need real logic (timing, macros, state machines) are out of scope for XML — those
are written as C++ modules against `support/input_adapter/input_adapter.h`, which feeds the same
registry.
