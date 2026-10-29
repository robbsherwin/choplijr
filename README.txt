Choplifter! for IBM PCjr -- running it in DOSBox
=================================================

What you need
--------------
1. DOSBox (the plain, standard build from https://www.dosbox.com/ -- you
   do NOT need DOSBox-X for this).
2. build\m10.exe -- the game itself. It is a single self-contained DOS
   .exe (no data files, no CD, nothing else to copy alongside it).
3. conf\pcjr-640k-m10.conf -- a DOSBox settings file that sets up a PCjr
   with 640 KB of RAM and starts the game automatically. Optional --
   see "Running it by hand" below if you would rather not use it.

Running it with the conf file
------------------------------
The conf file's [autoexec] section mounts a folder as drive C: with this
exact line:

    mount c: Y:\github\choplijr\build

That is an absolute path baked in from the machine this was built on, so
before using the conf file, open it in a text editor and change that
line to wherever YOU put m10.exe -- for example, if you copied m10.exe
into C:\Games\Choplifter, change the line to:

    mount c: C:\Games\Choplifter

Then, from a command prompt in the folder holding the conf file:

    dosbox -conf pcjr-640k-m10.conf

Plain DOSBox may print a warning that it does not recognize the
"automount" setting on startup -- that setting is DOSBox-X only. It is
harmless; DOSBox ignores it and keeps going.

Running it by hand (no conf file)
-----------------------------------
1. Start DOSBox.
2. Mount the folder containing m10.exe as drive C:, then switch to it:

       mount c: C:\Games\Choplifter
       c:

3. Run the game with /force:

       m10.exe /force

Why /force? The game checks a BIOS byte to confirm it is really running
on a PCjr before it trusts the hardware. DOSBox reports a different byte
there, so /force tells the game "yes, I know, run anyway." Timings and
sound under any DOSBox are a smoke test, not a hardware guarantee -- see
docs/M10.md if you want the details.

Controls
---------
- Title screen: J or K picks Joystick or Keyboard, C calibrates a
  joystick, SPACE starts flying, Esc quits to DOS.
- Keyboard: WASD or arrow keys to fly, '.' rotates, '/' fires.
- Joystick: stick to fly (plus WASD/arrows also work), button 0 fires,
  button 1 (or '.'  or Alt/LShift) rotates. Ctrl-A / Ctrl-V invert the
  X / Y axes; Ctrl-S mutes sound.
- Esc during play or on the story/win/lose screens returns to the title.

Goal: fly out, rescue hostages from the barracks and fly them back to
the base pad. 64 rescued wins; 64 killed (or dying on the third and
final sortie) ends the game.
