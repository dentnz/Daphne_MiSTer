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

/* max number of bytes in stream.dat mpeg2 stream - total number of "lines" */
//`define MAX_STREAM_LENGTH 4194304
// ballerina
//`define MAX_STREAM_LENGTH 691161
// Lair
//`define MAX_STREAM_LENGTH 4194559

`undef DEBUG
`undef DEBUG_DUMP
`undef DUMP_TVOUT

module daphne (
    input  		rst,                  	// sim reset - active high
    input  		sys_clk,          		// sys clock
    input  		dot_clk,
    input  		mem_clk,
	output 		reg h_sync,
	output 		reg v_sync,
	output 		reg [7:0] red,
	output 		reg [7:0] green,
	output 		reg [7:0] blue,
	input		famicom_data,
	input		famicom_pulse,
	input		famicom_latch,
	input		hblank,
	input		vblank,
	output      pixel_en,
	output		framebuffer_write_clock,
	output		framebuffer_write_data,
	output		framebuffer_write_signal,
	output		framebuffer_write_address,
	output		led5,
	output		led6,
	output		led7,
	output		led8,
	output      [31:0] stream_dat_count,
	output		audio_dac,
	output      [33:0] testpoint,
	output      [35:0] EXT_BUS,
	input		digital_volume_control
);

reg [15:0] lair_disc_fps = 23.976;

/*
* read mpeg2 clip from file "stream.dat"
*/

integer     i;
reg   [7:0] stream[0:`MAX_STREAM_LENGTH];
reg   rst_ff;

initial begin
    $readmemh("stream.dat", stream, 0, `MAX_STREAM_LENGTH);
    RESET_N      = 8'b1;
    i            = 0;
    stream_data  = 0;
    stream_valid = 0;
    reg_dta_in   = 32'd0;
    reg_addr     = 4'b0;
    reg_wr_en    = 0;
    reg_rd_en    = 0;
    testpoint	 = 34'd0;
end

reg   [7:0] stream_data;
reg         stream_valid;

reg   [3:0] reg_addr;
reg  [31:0] reg_dta_in;
reg         reg_wr_en;
wire [31:0] reg_dta_out;
reg         reg_rd_en;

wire        busy;
wire        error;
wire        interrupt;

wire  [7:0] r;
wire  [7:0] g;
wire  [7:0] b;
wire  [7:0] y;
wire  [7:0] u;
wire  [7:0] v;

wire  [1:0] mem_req_rd_cmd;
wire [21:0] mem_req_rd_addr;
wire [63:0] mem_req_rd_dta;
wire        mem_req_rd_en;
wire        mem_req_rd_valid;
wire [63:0] mem_res_wr_dta;
wire        mem_res_wr_en;
wire        mem_res_wr_almost_full;
wire        mem_res_wr_full;

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

reg [31:0] stream_byte_index;
assign stream_dat_count = stream_byte_index;

always @(posedge sys_clk) begin
    if (~RESET_N) begin
        stream_byte_index <= 0;
        stream_data <= 0;
        stream_valid <= 1'b0;
        led5 <= 0;
        led6 <= 0;
        $display("reset");
    end else if (~busy && (stream_byte_index < `MAX_STREAM_LENGTH) && (^stream [stream_byte_index] !== 1'bx)) begin
        stream_byte_index <= stream_byte_index + 1;
        stream_data <= stream[stream_byte_index];
        stream_valid <= 1'b1;
        last_search_stream <= search_stream;
        if (~last_search_stream && search_stream) begin
        	stream_byte_index <= search_stream_byte_index;
        end
        led5 <= 0;
        led6 <= 0;
    end else begin
    	// busy or error
    	stream_byte_index <= stream_byte_index;
    	led5 <= 1;
    	led6 <= error;
        stream_data <= 0;
        stream_valid <= 1'b0;
    end
end

/*
 * mpeg2 decoder
 */
mpeg2video mpeg2 (
    .clk(sys_clk),
    .mem_clk(mem_clk),
    .dot_clk(dot_clk),
    .rst(RESET_N),
    .stream_data(stream_data),
    .stream_valid(stream_valid),
    .reg_addr(reg_addr),
    .reg_wr_en(reg_wr_en),
    .reg_dta_in(reg_dta_in),
    .reg_rd_en(reg_rd_en),
    .reg_dta_out(reg_dta_out),
    .busy(busy),
    .error(error),
    .interrupt(interrupt),
    .watchdog_rst(),
    .r(r),
    .g(g),
    .b(b),
    .y(y),
    .u(u),
    .v(v),
    .pixel_en(pixel_en),
    .h_sync(h_sync),
    .v_sync(v_sync),
    .c_sync(),
    .mem_req_rd_cmd(mem_req_rd_cmd),
    .mem_req_rd_addr(mem_req_rd_addr),
    .mem_req_rd_dta(mem_req_rd_dta),
    .mem_req_rd_en(mem_req_rd_en),
    .mem_req_rd_valid(mem_req_rd_valid),
    .mem_res_wr_dta(mem_res_wr_dta),
    .mem_res_wr_en(mem_res_wr_en),
    .mem_res_wr_almost_full(mem_res_wr_almost_full),
    .testpoint(testpoint),
    .testpoint_dip(4'h0),
    .testpoint_dip_en(1'b0)
);

reg [7:0] pixel_count = 0;
always @(posedge dot_clk) begin
    if (pixel_en && ((^r === 1'bx) || (^g === 1'bx) || (^g === 1'bx))) begin
        red 	= 8'hFF;
        green 	= 8'h00;
        blue	= 8'h00;
    end else if (pixel_en) begin
        red		= r;
        green	= g;
        blue	= b;
    end else if (v_sync || h_sync) begin
        red		= 8'hff;
        green	= 8'h00;
        blue    = 8'hff;
    end else begin
        red		= 8'h44;
        green	= 8'h44;
        blue	= 8'h44;
    end
    pixel_count <= pixel_count + 1;
end

/*
* Memory controller
*/
mem_ctl mem_ctl (
    .clk(mem_clk),
    .rst(RESET_N),
    .mem_req_rd_cmd(mem_req_rd_cmd),
    .mem_req_rd_addr(mem_req_rd_addr),
    .mem_req_rd_dta(mem_req_rd_dta),
    .mem_req_rd_en(mem_req_rd_en),
    .mem_req_rd_valid(mem_req_rd_valid),
    .mem_res_wr_dta(mem_res_wr_dta),
    .mem_res_wr_en(mem_res_wr_en),
    .mem_res_wr_almost_full(mem_res_wr_almost_full)
);

endmodule