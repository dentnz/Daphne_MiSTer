# Daphne_MiSTer
A MiSTer core capable of running FMV games like Dragon's Lair.

## References

### Verilator and SDL simulator basis
https://projectf.io/posts/verilog-sim-verilator-sdl/

### Mpeg2 Decoder

Daphne compatible ROMS seem to all use mpeg2. MPEG2FPGA open core seems to be best.

https://opencores.org/websvn/listing?repname=mpeg2fpga&path=&

https://www.ece.mcmaster.ca/~nicola/mpeg.html

### COPS420/421 - Not sourced yet
Seems to be a MCU present in the Apple Lisa. Unable to source RTL for this component at this stage.

http://john.ccac.rwth-aachen.de:8000/patrick/COPSreader.htm

### M6502 - Not sourced yet
Should use a verilog implementation for this

### MC6809 - Sourced
Verilog found in this repo. Added to RTL folder as a git submodule

https://github.com/cavnex/mc6809

### Z80 - Sourced
Used in Lair.

https://github.com/Time0o/z80-verilog.git

## Game info and analysis

### Dragons Lair

Schematic/owners manual:
http://www.dragons-lair-project.com/tech/schematics/Dragons_Lair_Schematic_Package_Supplement_Europe.pdf

