# Daphne_MiSTer
A MiSTer core capable of running FMV games like Dragon's Lair.

## References

### Verilator and SDL simulator basis
https://projectf.io/posts/verilog-sim-verilator-sdl/

### Mpeg2 Decoder
Still not 100% certain this is required. Possible candidate(s):
https://www.ece.mcmaster.ca/~nicola/mpeg.html

https://opencores.org/websvn/listing?repname=mpeg2fpga&path=&

### COPS420/421 - Not sourced yet
Seems to be a MCU present in the Apple Lisa. Unable to source RTL for this component at this stage.

http://john.ccac.rwth-aachen.de:8000/patrick/COPSreader.htm

### M6502 - Not sourced yet
Should use a verilog implementation for this

### MC6809 - Sourced
Verilog found in this repo. Added to RTL folder as a git submodule

https://github.com/cavnex/mc6809

