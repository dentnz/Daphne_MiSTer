module hps_ext
(
	input             reset,
	input             RESET_N,
	input             sys_clk,
	input             mem_clk,
	inout      [35:0] EXT_BUS,

	input             perform_debug_test,
	input	          frame_search_req,
	input      [31:0] frame_search,
	input			  play_req,
	input             pause_req,

    output reg  [7:0] stream_data,
    output reg        stream_valid,
	output reg  [4:0] reg_addr,
	output reg [31:0] reg_dta_in,
	output reg        reg_wr_en
);

reg stream_byte_index;

initial begin
    //$readmemh("stream.dat", stream, 0, `MAX_STREAM_LENGTH);
    stream_byte_index = 0;
    stream_data  = 0;
    stream_valid = 0;
end

assign EXT_BUS[15:0] = io_dout;
wire [15:0] io_din = EXT_BUS[31:16];
assign EXT_BUS[32] = dout_en;
wire io_strobe = EXT_BUS[33];
wire io_enable = EXT_BUS[34];

localparam EXT_CMD_MIN = CD_GET;
localparam EXT_CMD_MAX = CD_SET;

localparam CD_GET = 'h34;
localparam CD_SET = 'h35;

reg [15:0] io_dout;
reg        dout_en = 0;
reg  [9:0] byte_cnt;

always @(posedge sys_clk) begin
	reg [15:0] cmd;
	reg  [7:0] cd_req;

	cd_get <= 0;
	if(cd_put) cd_req <= cd_req + 1'd1;

	if(~io_enable) begin
		dout_en <= 0;
		io_dout <= 0;
		byte_cnt <= 0;
		if(cmd == 'h35) cd_get <= 1;
	end
	else if(io_strobe) begin

		io_dout <= 0;
		if(~&byte_cnt) byte_cnt <= byte_cnt + 1'd1;

		if(byte_cnt == 0) begin
			cmd <= io_din;
			dout_en <= (io_din >= EXT_CMD_MIN && io_din <= EXT_CMD_MAX);
			if(io_din == CD_GET) io_dout <= cd_req;
		end else begin
			case(cmd)
				CD_GET:
					if(!byte_cnt[9:3]) begin
						case(byte_cnt[2:0])
							1: io_dout <= cd_in[15:0];
							2: io_dout <= cd_in[31:16];
							3: io_dout <= cd_in[47:32];
						endcase
					end

				CD_SET:
					if(!byte_cnt[9:3]) begin
						case(byte_cnt[2:0])
							1: cd_out[15:0]  <= io_din;
							2: cd_out[31:16] <= io_din;
							3: cd_out[47:32] <= io_din;
						endcase
					end
			endcase
		end
	end
end

reg [47:0] cd_in;
reg [47:0] cd_out;
reg cd_put, cd_get;

always @(posedge sys_clk) begin
	reg reset_old;
	reg frame_search_req_old;
	reg play_req_old;
	reg pause_req_old;
	cd_put <= 0;

	reset_old <= reset;
	if (reset && !reset_old) begin
		cd_in  <= 8'hFF;
		cd_put <= 1;
	end

	// Search for mpeg/disc frame
	frame_search_req_old <= frame_search_req;
	if (!frame_search_req_old && frame_search_req) begin
		cd_in  <= { frame_search, 16'h36 };
		cd_put <= 1;
	end

	//  Play
	play_req_old <= play_req;
	if (!play_req_old && play_req) begin
		cd_in  <= { 16'h35 };
		cd_put <= 1;
	end

	//  Pause
	pause_req_old <= pause_req;
	if (!pause_req_old && pause_req) begin
		cd_in  <= { 16'h36 };
		cd_put <= 1;
	end

// @todo HPS response messaging
/*
	if (cd_get) begin
		case(cd_out[3:0])
			1: msu_enable <= cd_out[15];
			2: {msu_audio_size, msu_track_missing, msu_track_mounting, msu_audio_ack} <= {cd_out[47:16], !cd_out[47:16], 2'b00};
			3: msu_data_base <= cd_out[47:16];
		endcase
	end
*/
end

reg sync_perform_debug_test = 0;
reg ack_perform_debug_test = 0;
reg debug_test = 0;
// Fast clock
always @(posedge mem_clk) begin
    if (perform_debug_test == 1) begin
        $display("Got the message in shell");
        sync_perform_debug_test <= 1;
    end

    if (ack_perform_debug_test) begin
        sync_perform_debug_test <= 0;
    end
end
// Slow clock
always @(posedge sys_clk) begin
    if (!ack_perform_debug_test && sync_perform_debug_test) begin
        $display("sync got the message, crossing clocks now");
        debug_test <= 1;
        ack_perform_debug_test <= 1;
    end else if (ack_perform_debug_test) begin
        debug_test <= 0;
        ack_perform_debug_test <= 0;
    end
end

// Enable persistance in trickmode
// See section 1.4 for set of registers of the mpeg2fpga docs
reg [4:0] trick_mode_dta_in = 5'b10000;
always @(posedge sys_clk) begin
    if (~RESET_N) begin
        reg_wr_en <= 1'b0;
    end else begin
        reg_wr_en <= 1'b1;
        reg_addr <= 5'hb; // trickmode
        reg_dta_in <= trick_mode_dta_in;
    end
end

// mpeg2 Stream handling
wire mpeg2_busy;
reg debug_test_old = 0;
reg is_playing = 0;
always @(posedge sys_clk) begin
    debug_test_old <= debug_test;
    if (~RESET_N) begin
        debug_test_old <= 0;
        stream_byte_index <= 0;
        stream_data <= 0;
        stream_valid <= 1'b0;
        $display("reset player");
        is_playing <= 0;
    end else if (~debug_test_old && debug_test) begin
        $display("playing stream");
        is_playing <= 1;
    end else if (~mpeg2_busy) begin
        if (is_playing) begin
            // TODO needs to be pulling from a fifo at this point
            stream_byte_index <= stream_byte_index + 1;
            stream_data <= mpeg_fifo_dout;
            stream_valid <= 1'b1;
        end else begin
            stream_data <= 1'b0;
        end
    end else if (~mpeg2_busy && is_playing) begin
        stream_data <= 0;
    end else begin
    	// busy or error
    	//led5 <= 1;
    	//led6 <= error;
    	stream_byte_index <= stream_byte_index;
        stream_data <= 0;
        stream_valid <= 1'b0;
    end
end

reg  [31:0] mpeg_fifo_din;
reg   [7:0] mpeg_fifo_dout;
reg         mpeg_fifo_rd_en;
reg         mpeg_fifo_wr_en;
wire        mpeg_fifo_wr_full;
wire        mpeg_fifo_wr_overflow;
wire        mpeg_fifo_wr_almost_full;
wire        mpeg_fifo_rd_empty;
wire        mpeg_fifo_rd_valid;

fifo_dc
#(
.addr_width(9'd8),
.dta_width(9'd32),
.prog_thresh(9'd1)
)
mpeg_fifo
(
    .rst(reset),
    .wr_clk(mem_clk),
    .din(mpeg_fifo_din),
    .wr_en(mpeg_fifo_wr_en),
    .full(mpeg_fifo_wr_full),
    .wr_ack(),
    .overflow(mpeg_fifo_wr_overflow),
    .prog_full(mpeg_fifo_wr_almost_full),
    .rd_clk(mem_clk),
    .dout(mpeg_fifo_dout),
    .rd_en(mpeg_fifo_rd_en),
    .empty(mpeg_fifo_rd_empty),
    .valid(mpeg_fifo_rd_valid),
    .underflow(),
    .prog_empty()
);

endmodule