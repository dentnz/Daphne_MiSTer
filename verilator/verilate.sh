verilator \
-cc -exe --public --trace --savable \
--compiler msvc +define+SIMULATION=1 \
-O3 --x-assign fast --x-initial fast --noassert \
--converge-limit 6000 \
-Wno-UNOPTFLAT \
-Wno-WIDTH -Wno-MODDUP -Wno-INFINITELOOP -Wno-STMTDLY -Wno-CASEX \
--top-module top sim.v gigatron_shell.sv \
-I.. \
-I../rtl/mpeg2fpga/rtl/mpeg2
