SDL_CFLAGS = `sdl2-config --cflags`
SDL_LDFLAGS = `sdl2-config --libs`

daphne: daphne.exe
#STREAM = ../../tools/streams/stream-susi.mpg
STREAM = test.mpg

%.exe: %.mk
	make -C ./obj_dir -f Vtop_$<

%.mk: top_%.sv
	verilator -I.. -I../rtl/mpeg2fpga/rtl/mpeg2 -Wno-WIDTH -Wno-MODDUP -Wno-STMTDLY -Wno-CASEX -cc $< --exe main_$(basename $@).cpp -o $(basename $@) \
		-CFLAGS "${SDL_CFLAGS}" -LDFLAGS "${SDL_LDFLAGS}"

# convert mpeg2 stream to .dat file for testbench
stream.dat: $(STREAM)
	head --bytes=4m $? | xxd -c 1 - | cut -d\  -f 2 > $@
	xxd -c 1 end-of-sequence.mpg | cut -d\  -f 2 >> $@

all: daphne stream.dat

clean:
	rm -rf ./obj_dir

.PHONY: all clean
