module daphne_shell (
    // MEM 100Mhz clock
    input       mem_clk,

    // SYS 50 Mhz
    input       clk,

    // VGA 25Mhz clock
    input       vga_clk,

    // Reset when == 1
    input reset,

    // run == 0 Halt, run == 1 run/resume
    input run,

    // I/O Port signals
    output       famicom_pulse,
    output       famicom_latch,
    input        famicom_data,

    // Raw VGA signals from the Gigatron
    output hsync,
    output vsync,
    output [7:0] red,
    output [7:0] green,
    output [7:0] blue,
    output hblank,
    output vblank,
    output pixel_en,

    // Write output to external framebuffer
    output        framebuffer_write_clock,
    output        framebuffer_write_signal,
    output [18:0] framebuffer_write_address,
    output  [7:0] framebuffer_write_data,

    // BlinkenLights Signals
    output led5, // extended_output_port bit 0
    output led6, // "" bit 1
    output led7, // "" bit 2
    output led8, // "" bit 3

    output [31:0] stream_dat_count,

    // 16 bit LPCM audio output from the Gigatron.
    output [15:0] audio_dac,

    // Digital volume control with range 0 - 11.
    input [3:0] digital_volume_control,

    // Signals from user interface to select program to load
    input loader_go,  // true when user select load
    input [3:0] loader_program_select,
    output loader_active, // true when loader is active

    output [35:0] EXT_BUS,
    input         perform_debug_test
);

// The Gigatron loader operates in passthrough mode for
// the Famicom controller signals by default.
//
// When the Gigatron loader program in the ROM is being run, the
// Gigatron loader is activated by the user, and the Famicom
// input is switched to the loader which executes the Gigatron
// BabelFish program loading protocol which is a form of RDMA
// (Remote Direct Memory Access) because it performs a scatter-write
// of memory fragments through the Gigatrons RAM address space.
//
// This is because Gigatron programs must fit within the dead space
// in memory for the screen data's horizontal scan lines, as an optimization
// in Gigatron page address is used to provide each scan line at the start
// of a 256 byte Gigatron page as addressed by the high address register.
//
// In the Gigatron kit the BabelFish protocol is run from an Arduino,
// but in this implementation is written in 100% System Verilog.
//
wire gigatron_famicom_pulse;
wire gigatron_famicom_latch;
wire gigatron_famicom_data;

daphne daphne_inst(
    .mem_clk(mem_clk),		// 100hz clock
    .sys_clk(clk),			// 50Mhz
    .dot_clk(vga_clk),		// 25Mhz VGA clock from the PLL
    .rst(reset),

    //
    // Famicom Serial game controller.
    .famicom_pulse(gigatron_famicom_pulse), // output
    .famicom_latch(gigatron_famicom_latch), // output
    .famicom_data(gigatron_famicom_data),   // input

    // Raw VGA signals from the Gigatron
    .h_sync(hsync),
    .v_sync(vsync),
    .red(red),
    .green(green),
    .blue(blue),
    .hblank(hblank),
    .vblank(vblank),
    .pixel_en(pixel_en),

    // Write output to external framebuffer
    // The output is standard 8 bit true color with RRRGGGBB.
    //
    // https://en.wikipedia.org/wiki/8-bit_color

    .framebuffer_write_clock(framebuffer_write_clock),
    .framebuffer_write_signal(framebuffer_write_signal),
    .framebuffer_write_address(framebuffer_write_address),
    .framebuffer_write_data(framebuffer_write_data),

    // BlinkenLights
    .led5(led5),
    .led6(led6),
    .led7(led7),
    .led8(led8),

    .stream_dat_count(stream_dat_count),

    // 16 bit LPCM audio output from the Gigatron.
    .audio_dac(audio_dac),

    // Digital volume control with range 0 - 11.
    .digital_volume_control(digital_volume_control),
    .perform_debug_test(perform_debug_test),
    .EXT_BUS(EXT_BUS)
);

endmodule