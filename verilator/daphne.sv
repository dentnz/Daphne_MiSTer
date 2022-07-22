`define __IVERILOG__
`timescale  1 ps / 1 ps

// mpeg2fpga Files with names not recognised by verilator lookup
`include "mem_addr"
`include "synchronizer"
`include "syncgen"
`include "vbuf"
`include "getbits"
`include "read_write"
`include "rld"

// SIM and updated versions of mpeg2fpga verilog files
`include "verilator/wrappers"
`include "verilator/fwft"
`include "verilator/iquant"
`include "verilator/motcomp"
`include "verilator/watchdog"

`undef DEBUG
`undef DEBUG_DUMP
`undef DUMP_TVOUT

module daphne (
    input  		rst,                  	// sim reset - active high
    input  		sys_clk,
    input  		dot_clk,
    input  		mem_clk,
	output 		reg h_sync,
	output 		reg v_sync,
	output 		reg [7:0] red,
	output 		reg [7:0] green,
	output 		reg [7:0] blue,
	input		hblank,
	input		vblank,
	output      pixel_en,
	input		famicom_data,
	input		famicom_pulse,
	input		famicom_latch,
	output		framebuffer_write_clock,
	output		framebuffer_write_data,
	output		framebuffer_write_signal,
	output		framebuffer_write_address,
	output		reg led5,
	output		reg led6,
	output		reg led7,
	output		reg led8,
	output      reg [31:0] stream_dat_count,
	output		audio_dac,
	input		digital_volume_control,
	input       perform_debug_test,
	output      reg [35:0] EXT_BUS
);

reg RESET_N = 1;
reg rst_old = 0;

always @(posedge sys_clk) begin
	rst_old <= rst;
	if (!rst_old && rst) begin
		RESET_N <= 0;
	end

	if (!rst && rst_old) begin
		RESET_N <= 1;
	end
end

reg play = 0;

/* verilator lint_off PINMISSING */
vldp vldp_inst(
    .RESET_N(RESET_N),
    .rst(rst),
    .sys_clk(sys_clk),
    .mem_clk(mem_clk),
    .dot_clk(dot_clk),

    .r(red),
    .g(green),
    .b(blue),
    .h_sync(h_sync),
    .v_sync(v_sync),
    .vblank(vblank),
    .hblank(hblank),

    // mem_clk synced
    .perform_debug_test(perform_debug_test),

    .stream_dat_count(stream_dat_count),
    .EXT_BUS(EXT_BUS)
);
/* verilator lint_on PINMISSING */

endmodule