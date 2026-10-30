@echo off
rem FIXES.md 1.4: joystick timeout cut 0x7FFF to 0x0FFF.  Raw counts should
rem stay in the hundreds.  Same rules as PAD.BAT: stick centred, no keys.
M10 /stick /batch /frames=60 > M10-STK.LOG
type M10-STK.LOG
