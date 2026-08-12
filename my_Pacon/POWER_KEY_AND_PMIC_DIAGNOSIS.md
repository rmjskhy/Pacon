# GPIO0 key and AXP2101 diagnostic note

Updated: 2026-07-23

## There are two separate board keys

The supplier's factory program declares `example_key` as GPIO0 in
`09_FactoryProgram/components/gpio_bsp/gpio_bsp.h`.  It configures that GPIO
as an active-low, pull-up input, then uses it for single/double-click events.
That is a direct ESP32-S3 key (and is also the ESP32-S3 boot strap pin).

This is **not** the physical power key: the power key is connected to U1 pin
30.  AXP2101 pin 30 is `PWRON`, the PMIC power-on/off key input with an
internal pull-up to `VINT`.  The two keys must not be confused.

`my_Pacon/main/fluid_pendant.c` does not currently configure or poll GPIO0,
so that other key has no visible effect in the fluid firmware.  A non-working
PWRON key remains relevant to the AXP2101 diagnosis.  With no battery
installed, disconnecting USB still leaves the board with no source from which
the PMIC could start it.

## AXP2101 evidence retained separately

- PMIC address is `0x34`.  A separate scanner has now detected `0x34`
  repeatedly but intermittently.  Therefore AXP2101 has acknowledged on the
  bus at least sometimes, and it must **not** be declared defective yet.
- The earlier failed probes used 400 kHz, disabled ESP32-S3 internal pull-ups,
  and a 50 ms timeout.  That configuration was unsuitable as a PMIC health
  test, but it is not the only problem.
- `test_Pacon` now matches the scanner's conservative I2C configuration and
  performs 20 consecutive non-destructive `0x34` probe + chip-ID reads.  The
  completed test ACKed only 4/20 probes and never returned the required
  `0x4A` ID; details are in `../test_Pacon/I2C_AXP2101_EXPERIMENT.md`.
- Earlier USB input was measured at about `4.9 V`; the empty battery pads
  measured about `4.4 V`.  This suggests that part of the analogue power path
  can be alive, but does not prove that the PMIC digital/I2C section works.

## Current ranked hypotheses and safe next checks

1. U1 soldering, local I2C traces, VINT/ground contact, or the PMIC itself is
   now the leading cause of the intermittent ACK and invalid ID response.
2. I2C timing, pull-up, and timeout configuration caused the original false
   "always absent" conclusion and must be corrected in future application
   firmware, but it cannot explain the invalid ID under the scanner setup.
3. Independently, the physical PWRON key trace/solder may be open, or may not
   pull U1 pin 30 to ground when pressed.

Keep the battery disconnected.  First make unpowered continuity checks from
the key's grounding side to GND and from its signal side to U1 pin 30; also
check GPIO1/GPIO2 to U1's SCL/SDA pads and U1 exposed pad to ground.  With USB
connected, U1 pin 30 should be high at rest and go to 0 V while the key is
pressed; neither I2C line should be held at 0 V.  A PWRON pin that cannot be
pulled low indicates the key path.  A correct low PWRON plus failed I2C
acknowledgement points to U1 or its local contact/power.  Do not short unknown
U1 pads or inject an external voltage into the PMIC.
