`define __IVERILOG__

// mpeg2fpga Files with names not recognised by verilator lookup
`include "mem_addr"
`include "synchronizer"
`include "syncgen"
`include "vbuf"
`include "getbits"
`include "read_write"
`include "rld"

// SIM and updated versions of mpeg2fpga verilog files
`include "sim/wrappers"
`include "sim/fwft"
`include "sim/iquant"
`include "sim/motcomp"
`include "sim/watchdog"

/* max number of bytes in stream.dat mpeg2 stream - total number of "lines" */
//`define MAX_STREAM_LENGTH 4194304
`define MAX_STREAM_LENGTH 706819

/* clk at 75 MHz */
`define CLK_PERIOD 13.3

/* mem_clk at 125 MHz */
`define MEMCLK_PERIOD 8.0

/* dot_clk at 27 MHz */
`define VIDCLK_PERIOD 37.0

`undef DEBUG
//`define DEBUG 1

/* write (lxt) dumpfile of simulation run */
`undef DEBUG_DUMP
//`define DEBUG_DUMP 1

// write rgb+sync output to file tvout_0.ppm or tv_out_1.ppm, alternately.
`undef DUMP_TVOUT
`define DUMP_TVOUT 1

module top_daphne #(parameter CORDW=10) (  // coordinate width
    input  wire logic clk,                 // sys clock
    input  wire logic rst,                 // sim reset
    output      logic [CORDW-1:0] sdl_sx,  // horizontal SDL position
    output      logic [CORDW-1:0] sdl_sy,  // vertical SDL position
    output      logic sdl_de,              // data enable (low in blanking interval)
    output      logic [7:0] sdl_r,         // 8-bit red
    output      logic [7:0] sdl_g,         // 8-bit green
    output      logic [7:0] sdl_b          // 8-bit blue
    );

reg   [7:0]rst_ff;

// display sync signals and coordinates
logic [CORDW-1:0] sx, sy;
logic de;
simple_480p display_inst (
    .clk_pix(clk),
    .rst_pix(rst),
    .sx,
    .sy,
    .hsync(),
    .vsync(),
    .de
);

//// define a square with screen coordinates
//logic square;
//always_comb begin
//    square = (sx > 220 && sx < 420) && (sy > 140 && sy < 340);
//end

//// paint colours: white inside square, blue outside
logic [3:0] paint_r, paint_g, paint_b;
//always_comb begin
//    paint_r = (square) ? 4'hF : 4'h1;
//    paint_g = (square) ? 4'hF : 4'h3;
//    paint_b = (square) ? 4'hF : 4'h7;
//end

// SDL output (8 bits per colour channel)
always_ff @(posedge clk) begin
    sdl_sx <= sx;
    sdl_sy <= sy;
    sdl_de <= de;
    sdl_r <= {2{paint_r}};  // double signal width from 4 to 8 bits
    sdl_g <= {2{paint_g}};
    sdl_b <= {2{paint_b}};
end

/*
* read mpeg2 clip from file "stream.dat"
*/

integer     i;
reg   [7:0] stream[0:`MAX_STREAM_LENGTH];
reg         resetff;

initial begin
    $readmemh("stream.dat", stream, 0, `MAX_STREAM_LENGTH);
    rst_ff       = 8'b0;
    i            = 0;
    stream_data  = 0;
    stream_valid = 0;
    reg_dta_in   = 32'd0;
    reg_addr     = 4'b0;
    reg_wr_en    = 0;
    reg_rd_en    = 0;
end

assign resetff = rst_ff[7];

always @(posedge clk)
    rst_ff <= {rst_ff[6:0], 1'b1};

reg  mem_clk = clk;
wire dot_clk = clk;

reg   [7:0] stream_data;
reg         stream_valid;

// register file access - not sure what this is for yet
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

wire        pixel_en;
wire        h_sync;
wire        v_sync;

wire  [1:0] mem_req_rd_cmd;
wire [21:0] mem_req_rd_addr;
wire [63:0] mem_req_rd_dta;
wire        mem_req_rd_en;
wire        mem_req_rd_valid;
wire [63:0] mem_res_wr_dta;
wire        mem_res_wr_en;
wire        mem_res_wr_almost_full;
wire        mem_res_wr_full;

wire [32:0] testpoint;

always @(posedge clk) begin
    if (~resetff) begin
        i <= 0;
        stream_data <= 0;
        stream_valid <= 1'b0;
    end else if (~busy && (i < `MAX_STREAM_LENGTH) && (^stream [i] !== 1'bx)) begin
        i <= i + 1;
        stream_data <= stream [i];
        $display(stream_data);
        stream_valid <= 1'b1;
    end else begin
        i <= i;
        stream_data <= 0;
        stream_valid <= 1'b0;
    end
end

/*
 * mpeg2 decoder
 */
mpeg2video mpeg2 (
    .clk(clk),
    .mem_clk(mem_clk),
    .dot_clk(dot_clk),
    .rst(resetff),
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
    .testpoint_dip_en(1'b1)
);

reg [7:0] pixel_count = 0;
always @(posedge dot_clk) begin
    if (pixel_en && ((^r === 1'bx) || (^g === 1'bx) || (^g === 1'bx))) begin
        paint_r = 4'hF;
        paint_g = 4'h0;
        paint_b = 4'h0;
    end else if (pixel_en) begin
        paint_r = r;
        paint_g = g;
        paint_b = b;
    end else if (v_sync || h_sync) begin
        paint_r = 4'h0;
        paint_g = 4'h0;
        paint_b = 4'h0;
    end else begin
        paint_r = 4'h4;
        paint_g = 4'h4;
        paint_b = 4'h4;
    end
    pixel_count <= pixel_count + 1;
end

endmodule