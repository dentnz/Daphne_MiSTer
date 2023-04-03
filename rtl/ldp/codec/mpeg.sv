module mpeg(
    input               RESET_N,
    input               sys_clk,
    input               mem_clk,
    input               dot_clk,

	output  wire        h_sync,
	output  wire        v_sync,
	output  reg   [7:0] r,
	output  reg   [7:0] g,
	output 	reg   [7:0] b,
	// TODO yuv

	output              busy,
    input               hblank,
    input               vblank,

    input   wire  [4:0] reg_addr,
    input   wire [31:0] reg_dta_in,
    input   wire        reg_wr_en,

    input   wire  [7:0] stream_data,
    input   wire        stream_valid,
    inout   wire [35:0] EXT_BUS
);

wire [31:0] reg_dta_out;
reg         reg_rd_en;

wire        error;
wire        interrupt;

wire  [7:0] red;
wire  [7:0] green;
wire  [7:0] blue;

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

wire        testpoint;

/*
 * mpeg2 decoder
 */
mpeg2video mpeg2fpga_inst (
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
    .r(red),
    .g(green),
    .b(blue),
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
reg       pixel_en = 0;
always @(posedge dot_clk) begin
    if (pixel_en && ((^r === 1'bx) || (^g === 1'bx) || (^g === 1'bx))) begin
        r 	    = 8'hFF;
        g    	= 8'h00;
        b   	= 8'h00;
    end else if (pixel_en) begin
        r		= red;
        g   	= green;
        b   	= blue;
    end else if (v_sync || h_sync) begin
        r		= 8'hff;
        g   	= 8'h00;
        b       = 8'hff;
    end else begin
        r		= 8'h44;
        g   	= 8'h44;
        b	    = 8'h44;
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