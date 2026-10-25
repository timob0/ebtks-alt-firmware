#!/usr/bin/env python3
"""Check opt-in C2a EBTKS RMIDLE counter only runs in safe task context."""
from pathlib import Path
r=Path(__file__).resolve().parents[1]
probe=(r/"src/EBTKS_CPM85_Rmidle_Probe.cpp").read_text()
h=(r/"include/EBTKS_CPM85_Rmidle_Probe.h").read_text()
keys=(r/"include/EBTKS_CPM85_Keyboard.h").read_text()
services=(r/"src/EBTKS_AUXROM_SD_Services.cpp").read_text()
main=(r/"src/EBTKS.cpp").read_text()
ini=(r/"platformio.ini").read_text()
assert "-DEBTKS_CPM85_RMIDLE_PROBE_ENABLE=1" in ini
assert "#if EBTKS_CPM85_RMIDLE_PROBE_ENABLE" in probe
assert "static volatile uint32_t rmidleCount = 0;" in probe
assert "void onRmidle()" in probe and "if (active) ++rmidleCount" in probe
assert "Cpm85RmidleProbe::onRmidle();" in services.split("void AUXROM_RMIDLE(void)",1)[1].split("int     newchar;",1)[0]
assert "Cpm85RmidleProbe::service();" in main
assert main.index("AUXROM_Poll();") < main.index("Cpm85RmidleProbe::service();")
assert "Cpm85RmidleProbe::onKey(value)" in keys
assert "Cpm85RmidleProbe::onControl(value)" in keys
assert "Cpm85Keys::onControl(value)" in keys
assert "if(value!=0 && value!=1) return;" in keys  # Old CP/M path unchanged
assert "Write_on_CRT_Alpha(" in probe
assert "CRT_capture_screen();" in probe and "CRT_restore_screen();" in probe
assert "if (!abortWithoutRestore)" in probe
assert "onControl(uint8_t value)" in probe and "value == 4" in probe
assert "(uint32_t)(now - lastDrawMs) < 250" in probe
assert "(uint32_t)(now - lastRmidleChangeMs) > 2000" in probe
assert "STALLED" in probe and "ACTIVE" in probe
for func in ("void onControl(uint8_t value)", "bool onKey(uint8_t key)", "void onRmidle()"):
    body=probe.split(func,1)[1].split("\n}\n",1)[0]
    for forbidden in ("Write_on_CRT_Alpha", "Serial.", "esp32.", "DMA_", "CRT_restore_screen", "delay(", "malloc(", "new "):
        assert forbidden not in body, (func, forbidden)
assert "STBD R22,=CPKEY" not in probe
print("PASS: C2a RMIDLE counting in AUXROM task; key ISR only updates state; CRT main-loop only")
