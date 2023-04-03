module top(
    VGA_R,
    VGA_B,
    VGA_G,
    VGA_HS,
    VGA_VS,
    VGA_DE,
    VGA_HB,
    VGA_VB,
    CE_PIXEL,
    AUDIO_L,
    AUDIO_R,
    ps2_key,
    joystick_0,
    joystick_1,
    joystick_2,
    joystick_3,
    menu,
    reset,
    clk_sys,
    ioctl_upload,
    ioctl_download,
    ioctl_addr,
    ioctl_dout,
    ioctl_din,
    ioctl_index,
    ioctl_wait,
    ioctl_wr,
    led5,
    led6,
    stream_dat_count,
    EXT_BUS,
    EXT_BUS_IN,
    EXT_BUS_OUT,
    perform_debug_test,
    perform_io_strobe
);
    input clk_sys;
    input reset;

    output [7:0] VGA_R;
    output [7:0] VGA_G;
    output [7:0] VGA_B;

    output VGA_HS;
    output VGA_VS;
    output VGA_DE;
    output VGA_HB;
    output VGA_VB;
    output CE_PIXEL;

    output led5;
    output led6;
    inout  [35:0] EXT_BUS;
    input  [35:0] EXT_BUS_IN;
    output [35:0] EXT_BUS_OUT;

    output [15:0] AUDIO_L;
    output [15:0] AUDIO_R;
    output [31:0] stream_dat_count;

    input        ioctl_upload;
    input        ioctl_download;
    input        ioctl_wr;
    input [24:0] ioctl_addr;
    input  [7:0] ioctl_dout;
    output [7:0] ioctl_din;
    input  [7:0] ioctl_index;
    output  reg  ioctl_wait=1'b0;

    input reg [10:0] ps2_key;
    input reg [31:0] joystick_0;
    input reg [31:0] joystick_1;
    input reg [31:0] joystick_2;
    input reg [31:0] joystick_3;
    input reg menu;
    input reg perform_debug_test;
    input reg perform_io_strobe;

////////////////////////////  HPS I/O  //////////////////////////////////

wire [1:0] buttons;
wire [31:0] status;
wire        img_mounted;
wire        img_readonly;
wire [63:0] img_size;
wire        ioctl_download;
wire [24:0] ioctl_addr;
wire [7:0]  ioctl_dout;
wire        ioctl_wr;
wire [7:0]  ioctl_index;

wire vsync;
wire hsync;
wire [7:0] red;
wire [7:0] green;
wire [7:0] blue;
wire hblank, vblank;

wire famicom_pulse;
wire famicom_latch;
wire famicom_data;

wire clk;
wire clk_vid;

// Divide incoming clk_sys of 100 into
// - 100 for mem_clk
// - 50 for clk (also sys_clk)
// - 25 for vid_clk
ripple_clock_divider clk_divider(
	.reset(reset),
	// 100mhz
	.clk(clk_sys),
	// 50mhz
	.clk_div2(clk),
	// 25mhz
	.clk_div4(clk_vid)
);

wire pixel_en;

reg perform_debug_test_out = 0;
reg perform_io_strobe_out = 0;
// Clk_sys here is actually going to become mem_clk, since it's the fastest of the three
always @(posedge clk_sys) begin
    reg perform_debug_test_old;
    reg perform_io_strobe_old;

    if (reset) begin
        perform_debug_test_old <= 0;
        perform_io_strobe_old <= 0;
        perform_debug_test_out <= 0;
        perform_io_strobe_out <= 0;
    end else begin
        perform_debug_test_old <= perform_debug_test;
        perform_io_strobe_old <= perform_io_strobe;
        perform_debug_test_out <= 0;
        perform_io_strobe_out <= 0;
        if (~perform_debug_test_old && perform_debug_test) begin
            $display("SIM - debug trigger at first synchroniser");
            perform_debug_test_out <= 1;
        end
        if (~perform_io_strobe_old && perform_io_strobe) begin
            $display("SIM - io strobe trigger at first synchroniser");
            perform_io_strobe_out <= 1;
        end
    end
end

// verilator lint_off PINMISSING
daphne_shell daphne_shell_inst(
    .mem_clk(clk_sys),      // 100Mhz MEM clock
    .vga_clk(clk_vid),      // 24Mhz VGA clock
    .clk(clk),				// 50mhz clock
    .reset(reset),
    .run(1'b1),

    .perform_debug_test(perform_debug_test_out),
    .perform_io_strobe(perform_io_strobe_out),
    //
    // These signals are from the Famicom serial game controller.
    //
    .famicom_pulse(famicom_pulse), // output
    .famicom_latch(famicom_latch), // output
    .famicom_data(famicom_data), // input

    // VGA output
    .hsync(hsync),
    .vsync(vsync),
    .red(red),
    .green(green),
    .blue(blue),
    .hblank(hblank),
    .vblank(vblank),
    .pixel_en(pixel_en),

    ////
    //// Write output to external framebuffer
    ////
    //// Note: Gigatron outputs its 6.25Mhz clock as the clock
    //// to synchronize these signals.
    ////
    //// The output is standard 8 bit true color with RRRGGGBB.
    ////
    //// https://en.wikipedia.org/wiki/8-bit_color
    ////
    ////    .framebuffer_write_clock(framebuffer_write_clock),
    ////    .framebuffer_write_signal(framebuffer_write_signal),
    ////    .framebuffer_write_address(framebuffer_write_address),
    ////    .framebuffer_write_data(framebuffer_write_data),

	// mpeg2 Debug
	.led5(led5),
	.led6(led6),
	.stream_dat_count(stream_dat_count),
	.EXT_BUS(EXT_BUS),
	.EXT_BUS_IN(EXT_BUS_IN),
	.EXT_BUS_OUT(EXT_BUS_OUT),

    //// 16 bit LPCM audio output
    .audio_dac(AUDIO_L),
    ////    // Digital volume control with range 0 - 11.
    .digital_volume_control(4'd11),

    //// Signals from user interface to select program to load
    .loader_go(buttons[1]),  // input, true when user select load
    .loader_program_select(4'd0)
    //.loader_active(application_active) // output
);	
// verilator lint_on PINMISSING
assign AUDIO_R = AUDIO_L;

//////////////////////////////  VIDEO  ////////////////////////////////////
assign VGA_R=red;
assign VGA_G=green;
assign VGA_B=blue;

assign VGA_HS = hsync;
assign VGA_VS = vsync;
assign VGA_DE = (hblank|vblank);
assign VGA_HB = hblank;
assign VGA_VB = vblank;
//assign CE_PIXEL = 1'b1;
assign CE_PIXEL = clk_vid;

////////////////////////////  INPUT  //////////////////////////////////////

reg [7:0] joypad_bits;
reg joypad_clock, last_joypad_clock;
reg joypad_out;

wire [7:0] nes_joy_A = { 
    joystick_0[0], joystick_0[1], joystick_0[2], joystick_0[3],
    joystick_0[7], joystick_0[6], joystick_0[5], joystick_0[4] 
};

reg [7:0] ascii_code;

wire [7:0] ascii_bitmap = {
	ascii_code[0], ascii_code[1], ascii_code[2], ascii_code[3],
	ascii_code[4], ascii_code[5], ascii_code[6], ascii_code[7]
};

wire discard;

Keyboard keyboard(
    .ps2_key(ps2_key),
    .pulse(famicom_pulse),
    .ascii_code(ascii_code),
    .kb_lang(3'd0),
    .caps_lock(discard),
    .reset(reset)
);

always @(posedge clk_sys) begin
	if (reset) begin
		joypad_bits <= 0;
		last_joypad_clock <= 0;
	end else begin
		if (joypad_out) begin
			joypad_bits  <= ~(nes_joy_A | ~ascii_bitmap);
		end
		if (!joypad_clock && last_joypad_clock) begin
			joypad_bits <= {1'b0, joypad_bits[7:1]};
		end
		last_joypad_clock <= joypad_clock;
	end
end

assign joypad_out=famicom_latch;
assign joypad_clock=~famicom_pulse;
assign famicom_data=joypad_bits[0];

endmodule

