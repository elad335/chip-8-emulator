# (Super)Chip-8 emulator

Executable loading
---------------------------------------

Put super images at `/roms/super/`.
Non-super images go into `roms/`.

Link to additional chip-8 images and guidlines can be found at https://github.com/dmatlack/chip8/tree/master/roms
Link to super chip 8 ingaes https://www.zophar.net/pdroms/chip8/super-chip-games-pack.html

Keyboard Input
---------------------------------------
Keys are mapped as such:
```
Original               Emulator
1 2 3 C                1 2 3 4
4 5 6 D                Q W E R
7 8 9 E                A S D F
A 0 B F                Z X C V
```

Interpreter
---------------------------------------
Interpreter is entirely based on asmjit to allow unique optimizations.
