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

## Simulation

- If running on Windows, use Virtualbox to create an Ubuntu linux box. Log into this box non-headless.
- Install verilator - may need to use an alternative verilator with scheduling support allowing multiple
  clocks... more to come.
- Install sdl2 through apt
- If running on Windows, install Xming. Get IP address.
- Put a mpg2 file into sim folder, rename to test.mpg
- make stream.dat
- Determine the total number of lines - this is your stream.dat byte count
- Update top_daphne.sv to use this byte count
- make clean
- make
- If running on Windows, export DISPLAY=<xming_ip_address>:0.0
- obj_dir/daphne to start simulation
