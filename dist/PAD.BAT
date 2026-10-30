@echo off
rem Pad benchmark.  Plug stick 1, centre it, then do NOT touch the keyboard
rem until the DOS prompt comes back (about 10 seconds).  Log name is the
rem first argument, default M10-05.LOG.
set LOG=%1
if "%LOG%"=="" set LOG=M10-05.LOG
M10 /ztimer /batch /frames=60 > %LOG%
type %LOG%
