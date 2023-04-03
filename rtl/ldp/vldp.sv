// Combines streaming and codec behind a (soon to be) serial and video interface
module vldp(
    input               RESET_N,
    input               rst,
    input               sys_clk,
    input               mem_clk,
    input               dot_clk,

    // @todo Serial interface
/*
    input    wire6
    input    wire5
    input    wire4
    input    wire3
    input    wire1
*/

    input    reg        play,
    input    reg        pause,
    output   reg        is_playing,
    output   reg        is_paused,
    output   reg        is_searching,

    // Should be synced to sys_clk
    input               perform_debug_test,
    input               perform_io_strobe,

    output   reg        h_sync,
    output   reg        v_sync,
    input    reg        vblank,
    input    reg        hblank,
    output 	 reg  [7:0] r,
    output   reg  [7:0] g,
    output 	 reg  [7:0] b,

    output   reg [31:0] stream_dat_count,
    inout    reg [35:0] EXT_BUS,
    input    reg [35:0] EXT_BUS_IN,
    output   reg [35:0] EXT_BUS_OUT
);

`define MAX_STREAM_LENGTH 4194559

reg        frame_search_req;
reg [31:0] frame_search;
integer    stream_byte_index;
reg  [7:0] stream[0:`MAX_STREAM_LENGTH];
reg        rst_ff;
wire  [7:0]stream_data = 0;
wire       stream_valid = 0;
wire       mpeg2_busy;
wire [4:0] reg_addr;
wire       reg_wr_en;
wire [31:0]reg_dta_in;

mpeg mpeg_inst(
    .RESET_N(RESET_N),
    .sys_clk(sys_clk),
    .mem_clk(mem_clk),
    .dot_clk(dot_clk),
    .h_sync(h_sync),
    .v_sync(v_sync),
    .r(r),
    .g(g),
    .b(b),
    .busy(mpeg2_busy),
    .hblank(hblank),
    .vblank(vblank),

    .reg_addr(reg_addr),
    .reg_wr_en(reg_wr_en),
    .reg_dta_in(reg_dta_in),

    .stream_data(stream_data),
    .stream_valid(stream_valid),
    .EXT_BUS(EXT_BUS)

    // TODO may need to add some more control over the mpeg decoder via it's register access
    // Trick mode is cool, as it allows slow motion... which might come in handy
);

/* verilator lint_off PINMISSING */
hps_ext hps_ext_inst(
    .reset(rst),
    .mem_clk(mem_clk),
    .sys_clk(sys_clk),
    .RESET_N(RESET_N),
    .stream_data(stream_data),
    .frame_search_req(frame_search_req),
    .frame_search(frame_search),
    .play_req(play),
    .pause_req(pause),
    .reg_addr(reg_addr),
    .reg_dta_in(reg_dta_in),
    .reg_wr_en(reg_wr_en),
    .perform_debug_test(perform_debug_test),
    .perform_io_strobe(perform_io_strobe),
    .EXT_BUS(EXT_BUS),
    .EXT_BUS_IN(EXT_BUS_IN),
    .EXT_BUS_OUT(EXT_BUS_OUT)
);
/* verilator lint_on PINMISSING */

endmodule