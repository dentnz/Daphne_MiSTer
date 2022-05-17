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

module top_daphne #(parameter CORDW=10) (  // coordinate width
    input  wire logic clk_pix,             // pixel clock
    input  wire logic sim_rst,             // sim reset
    output      logic [CORDW-1:0] sdl_sx,  // horizontal SDL position
    output      logic [CORDW-1:0] sdl_sy,  // vertical SDL position
    output      logic sdl_de,              // data enable (low in blanking interval)
    output      logic [7:0] sdl_r,         // 8-bit red
    output      logic [7:0] sdl_g,         // 8-bit green
    output      logic [7:0] sdl_b          // 8-bit blue
    );

    // display sync signals and coordinates
    logic [CORDW-1:0] sx, sy;
    logic de;
    simple_480p display_inst (
        .clk_pix,
        .rst_pix(sim_rst),
        .sx,
        .sy,
        .hsync(),
        .vsync(),
        .de
    );

    // define a square with screen coordinates
    logic square;
    always_comb begin
        square = (sx > 220 && sx < 420) && (sy > 140 && sy < 340);
    end

    // paint colours: white inside square, blue outside
    logic [3:0] paint_r, paint_g, paint_b;
    always_comb begin
        paint_r = (square) ? 4'hF : 4'h1;
        paint_g = (square) ? 4'hF : 4'h3;
        paint_b = (square) ? 4'hF : 4'h7;
    end

    // SDL output (8 bits per colour channel)
    always_ff @(posedge clk_pix) begin
        sdl_sx <= sx;
        sdl_sy <= sy;
        sdl_de <= de;
        sdl_r <= {2{paint_r}};  // double signal width from 4 to 8 bits
        sdl_g <= {2{paint_g}};
        sdl_b <= {2{paint_b}};
    end


wire clk     = clk_pix;
wire mem_clk = clk_pix;
wire dot_clk = clk_pix;
wire rst     = sim_rst;

wire  [7:0] stream_data = 0;
wire        stream_valid = 1;

// register file access - not sure what this is for yet
wire  [3:0] reg_addr;
wire [31:0] reg_dta_in;
wire        reg_wr_en;
wire [31:0] reg_dta_out;
wire        reg_rd_en;

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

/*
 * mpeg2 decoder
 */

    mpeg2video mpeg2 (
    .clk(clk),
    .mem_clk(mem_clk),
    .dot_clk(dot_clk),
    .rst(rst),
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

endmodule