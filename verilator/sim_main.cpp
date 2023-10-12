#include <verilated.h>
#include "common.h"
//#include "Vtop.h"

#include "imgui.h"
//#include "implot.h"
#ifndef _MSC_VER
#include <stdio.h>
#include <SDL.h>
#include <SDL_opengl.h>
#else
#define WIN32
#include <dinput.h>
#endif

#include "sim_console.h"
#include "sim_bus.h"
#include "sim_video.h"
#include "sim_audio.h"
#include "sim_input.h"
#include "sim_clock.h"

#include "../imgui/imgui_memory_editor.h"
#include "../imgui/ImGuiFileDialog.h"

// Include Main_MiSTer-side files required for simulation
#include "../../cpp/Main_MiSTer/support/daphne.h"

#include <iostream>
#include <fstream>
using namespace std;

// Simulation control
// ------------------
int initialReset = 5;
bool run_enable = 1;
bool busy_led = 0;
bool error_led = 0;
int batchSize = 150000;
bool single_step = 0;
bool multi_step = 1;
int multi_step_amount = 1024;

// Debug GUI 
// ---------
const char* windowTitle = "Verilator Sim: Daphne";
const char* windowTitle_Control = "Daphne: Simulation control";
const char* windowTitle_DebugLog = "Debug log";
const char* windowTitle_Video = "VGA output";
const char* windowTitle_Audio = "Audio output";
bool showDebugLog = true;
DebugConsole console;
MemoryEditor mem_edit;

// HPS emulator
// ------------
SimBus bus(console);

// Input handling
// --------------
SimInput input(13);
const int input_right = 0;
const int input_left = 1;
const int input_down = 2;
const int input_up = 3;
const int input_a = 4;
const int input_b = 5;
const int input_x = 6;
const int input_y = 7;
const int input_l = 8;
const int input_r = 9;
const int input_select = 10;
const int input_start = 11;
const int input_menu = 12;

// Video
// -----
// FIXME had to do some weird lies here to get the video to show
// somewhat correctly, possibly indicative of a bug in my video logic.
#define VGA_WIDTH 1366
#define VGA_HEIGHT 504
#define VGA_ROTATE 0  // 90 degrees anti-clockwise
#define VGA_SCALE_X 0.5
#define VGA_SCALE_Y vga_scale
SimVideo video(VGA_WIDTH, VGA_HEIGHT, VGA_ROTATE);
float vga_scale = 1.0;

// Verilog module
// --------------
Vtop* top = NULL;

vluint64_t main_time = 0;	// Current simulation time.
double sc_time_stamp() {	// Called by $time in Verilog.
	return main_time;
}

vluint64_t stream_dat_count = 0;	// count in stream.dat
vluint64_t testpoint = 0;
vluint64_t EXT_BUS = 0;
vluint64_t EXT_BUS_IN = 0;
vluint64_t EXT_BUS_OUT = 0;

//vluint64_t incoming_command_byte_count = 0;

int clk_sys_freq = 100000000;
SimClock clk_sys(1);

// Audio
// -----
#define DISABLE_AUDIO
#ifndef DISABLE_AUDIO
SimAudio audio(clk_sys_freq, false);
#endif

// Reset simulation variables and clocks
void resetSim() {
	main_time = 0;
	top->reset = 1;
	top->EXT_BUS = 0;
	top->EXT_BUS_IN = 0;
	top->EXT_BUS_OUT = 0;
	clk_sys.Reset();
}

int verilate() {
    static uint8_t polling_finished = 0;
	if (!Verilated::gotFinish()) {

		// Assert reset during startup
		if (main_time < initialReset) { top->reset = 1; }
		// Deassert reset after startup
		if (main_time == initialReset) { top->reset = 0; }

		// Clock dividers
		clk_sys.Tick();

		// Set system clock in core
		top->clk_sys = clk_sys.clk;

		// Simulate both edges of system clock
		if (clk_sys.clk != clk_sys.old) {
			if (clk_sys.clk) {
				//input.BeforeEval();
				bus.BeforeEval();
			}
			top->eval();
			if (clk_sys.clk) { bus.AfterEval(); }
		}

#ifndef DISABLE_AUDIO
		if (clk_sys.IsRising())
		{
			audio.Clock(top->AUDIO_L, top->AUDIO_R);
		}
#endif
		busy_led = top->led5;
		error_led = top->led6;
		stream_dat_count = top->stream_dat_count;
		EXT_BUS = top->EXT_BUS;

		// Output pixels on rising edge of pixel clock
		if (clk_sys.IsRising() && top->CE_PIXEL) {
			uint32_t colour = 0xFF000000 | top->VGA_B << 16 | top->VGA_G << 8 | top->VGA_R;
			video.Clock(top->VGA_HB, top->VGA_VB, top->VGA_HS, top->VGA_VS, colour);
		}

		if (clk_sys.IsRising()) {
			main_time++;
            if (main_time == 600000) {
                printf("SIM - debug test - PLAY the video\n");
                printf("SIM - ext bus out %lu\n", top->EXT_BUS_OUT);
                top->perform_debug_test = 1;
                daphne_init();
            }

            if (main_time == 600500) {
                top->perform_debug_test = 0;
//                top->EXT_BUS |= 1UL << 34;
//                top->EXT_BUS_IN |= 1UL << 34;
//                top->EXT_BUS_OUT |= 1UL << 34;
//                printf("SIM - io_enable going high - ext bus in %lu\n", top->EXT_BUS_IN);
            }

            /*
            if (main_time == 612000) {
                // CD_GET, needs to be in bits 16-31
                top->EXT_BUS_IN = 52;
                top->EXT_BUS_IN = top->EXT_BUS_IN << 16;
                top->EXT_BUS_IN |= 1UL << 33;
                top->EXT_BUS_IN |= 1UL << 34;
                printf("SIM - ext bus - trying to send CD_GET, io_strobe high, io_enable high - %lu\n", top->EXT_BUS_IN);
            }
            */

            if (main_time > 612500 && polling_finished == 0) {
                polling_finished = daphne_poll();
            }

            /*
            if (top->EXT_BUS_OUT & (1ULL << 32)) {
                incoming_command_byte_count++;

                if (incoming_command_byte_count == 4) {
                    printf("SIM - ext bus - command detected - byte 1 - %lu\n", top->EXT_BUS_OUT);
                }
                if (incoming_command_byte_count == 5) {
                    printf("SIM - ext bus - command detected - byte 2 - %lu\n", top->EXT_BUS_OUT);
                }
                if (incoming_command_byte_count == 6) {
                    printf("SIM - ext bus - command detected - byte 3 - %lu\n", top->EXT_BUS_OUT);
                }

                if (incoming_command_byte_count == 7) {
                    // All bytes for incoming command have been captured, close off communications
                    // Disable io_enable and io_strobe
                    top->EXT_BUS_OUT &= ~(1UL << 34);
                    top->EXT_BUS_OUT &= ~(1UL << 33);
                    top->EXT_BUS_IN &= ~(1UL << 34);
                    top->EXT_BUS_IN &= ~(1UL << 35);
                    incoming_command_byte_count = 0;
                }
            }
            */
		}

		return 1;
	}

	// Stop verilating and cleanup
	top->final();
	delete top;
	exit(0);
	return 0;
}

unsigned char mouse_clock = 0;
unsigned char mouse_clock_reduce = 0;
unsigned char mouse_buttons = 0;
unsigned char mouse_x = 0;
unsigned char mouse_y = 0;

char spinner_toggle = 0;

int main(int argc, char** argv, char** env) {

	// Create core and initialise
	top = new Vtop();
	Verilated::commandArgs(argc, argv);

#ifdef WIN32
	// Attach debug console to the verilated code
	Verilated::setDebug(console);
#endif

	// Attach bus
	bus.ioctl_addr = &top->ioctl_addr;
	bus.ioctl_index = &top->ioctl_index;
	bus.ioctl_wait = &top->ioctl_wait;
	bus.ioctl_download = &top->ioctl_download;
	//bus.ioctl_upload = &top->ioctl_upload;
	bus.ioctl_wr = &top->ioctl_wr;
	bus.ioctl_dout = &top->ioctl_dout;
	//bus.ioctl_din = &top->ioctl_din;
	//input.ps2_key = &top->ps2_key;

#ifndef DISABLE_AUDIO
	audio.Initialise();
#endif

	// Set up input module
	input.Initialise();
#ifdef WIN32
	input.SetMapping(input_up, DIK_UP);
	input.SetMapping(input_right, DIK_RIGHT);
	input.SetMapping(input_down, DIK_DOWN);
	input.SetMapping(input_left, DIK_LEFT);
	input.SetMapping(input_a, DIK_Z); // A
	input.SetMapping(input_b, DIK_X); // B
	input.SetMapping(input_x, DIK_A); // X

	input.SetMapping(input_y, DIK_S); // Y
	input.SetMapping(input_l, DIK_Q); // L
	input.SetMapping(input_r, DIK_W); // R
	input.SetMapping(input_select, DIK_1); // Select
	input.SetMapping(input_start, DIK_2); // Start
	input.SetMapping(input_menu, DIK_M); // System menu trigger

#else
	input.SetMapping(input_up, SDL_SCANCODE_UP);
	input.SetMapping(input_right, SDL_SCANCODE_RIGHT);
	input.SetMapping(input_down, SDL_SCANCODE_DOWN);
	input.SetMapping(input_left, SDL_SCANCODE_LEFT);
	input.SetMapping(input_a, SDL_SCANCODE_A);
	input.SetMapping(input_b, SDL_SCANCODE_B);
	input.SetMapping(input_x, SDL_SCANCODE_X);
	input.SetMapping(input_y, SDL_SCANCODE_Y);
	input.SetMapping(input_l, SDL_SCANCODE_L);
	input.SetMapping(input_r, SDL_SCANCODE_E);
	input.SetMapping(input_start, SDL_SCANCODE_1);
	input.SetMapping(input_select, SDL_SCANCODE_2);
	input.SetMapping(input_menu, SDL_SCANCODE_M);
#endif
	// Setup video output
	if (video.Initialise(windowTitle) == 1) { return 1; }

	//bus.QueueDownload("zombie.tap",1,0);

#ifdef WIN32
	MSG msg;
	ZeroMemory(&msg, sizeof(msg));
	while (msg.message != WM_QUIT)
	{
		if (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			continue;
		}
#else
	bool done = false;
	while (!done)
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			ImGui_ImplSDL2_ProcessEvent(&event);
			if (event.type == SDL_QUIT)
				done = true;
		}
#endif
		video.StartFrame();

		input.Read();


		// Draw GUI
		// --------
		ImGui::NewFrame();

		// Simulation control window
		ImGui::Begin(windowTitle_Control);
		ImGui::SetWindowPos(windowTitle_Control, ImVec2(0, 0), ImGuiCond_Once);
		ImGui::SetWindowSize(windowTitle_Control, ImVec2(500, 150), ImGuiCond_Once);
		if (ImGui::Button("Reset simulation")) { resetSim(); } ImGui::SameLine();
		if (ImGui::Button("Start running")) { run_enable = 1; } ImGui::SameLine();
		if (ImGui::Button("Stop running")) { run_enable = 0; } ImGui::SameLine();
		ImGui::Checkbox("RUN", &run_enable);
		ImGui::Checkbox("BSY", &busy_led);
		ImGui::Checkbox("ERR", &error_led);
		//ImGui::PopItemWidth();
		ImGui::SliderInt("Run batch size", &batchSize, 1, 250000);
		if (single_step == 1) { single_step = 0; }
		if (ImGui::Button("Single Step")) { run_enable = 0; single_step = 1; }
		ImGui::SameLine();
		if (multi_step == 1) { multi_step = 0; }
		if (ImGui::Button("Multi Step")) { run_enable = 0; multi_step = 1; }
		//ImGui::SameLine();
		ImGui::SliderInt("Multi step amount", &multi_step_amount, 8, 1024);
if (ImGui::Button("Load Tape"))
    ImGuiFileDialog::Instance()->OpenDialog("ChooseFileDlgKey", "Choose File", ".tap", ".");

		ImGui::End();

		// Debug log window
		console.Draw(windowTitle_DebugLog, &showDebugLog, ImVec2(500, 700));
		ImGui::SetWindowPos(windowTitle_DebugLog, ImVec2(0, 160), ImGuiCond_Once);

		// Memory debug
		//ImGui::Begin("PGROM Editor");
		//mem_edit.DrawContents(top->emu__DOT__system__DOT__pgrom__DOT__mem, 32768, 0);
		//ImGui::End();
		//ImGui::Begin("CHROM Editor");
		//mem_edit.DrawContents(top->emu__DOT__system__DOT__chrom__DOT__mem, 2048, 0);
		//ImGui::End();
		//ImGui::Begin("WKRAM Editor");
		//mem_edit.DrawContents(&top->emu__DOT__system__DOT__wkram__DOT__mem, 16384, 0);
		//ImGui::End();
		//ImGui::Begin("CHRAM Editor");
		//mem_edit.DrawContents(&top->emu__DOT__system__DOT__chram__DOT__mem, 2048, 0);
		//ImGui::End();
		//ImGui::Begin("FGCOLRAM Editor");
		//mem_edit.DrawContents(&top->emu__DOT__system__DOT__fgcolram__DOT__mem, 2048, 0);
		//ImGui::End();
		//ImGui::Begin("BGCOLRAM Editor");
		//mem_edit.DrawContents(&top->emu__DOT__system__DOT__bgcolram__DOT__mem, 2048, 0);
		//ImGui::End();
		//ImGui::Begin("Sprite RAM");
		//mem_edit.DrawContents(&top->emu__DOT__system__DOT__spriteram__DOT__mem, 96, 0);
		//ImGui::End();
		//ImGui::Begin("Sprite Linebuffer RAM");
		//mem_edit.DrawContents(&top->emu__DOT__system__DOT__spritelbram__DOT__mem, 1024, 0);
		//ImGui::End();
		//ImGui::Begin("Sprite Collision Buffer RAM A");
		//mem_edit.DrawContents(&top->emu__DOT__system__DOT__comet__DOT__spritecollisionbufferram_a__DOT__mem, 512, 0);
		//ImGui::End();
		//ImGui::Begin("Sprite Collision Buffer RAM B");
		//mem_edit.DrawContents(&top->emu__DOT__system__DOT__comet__DOT__spritecollisionbufferram_b__DOT__mem, 512, 0);
		//ImGui::End();
		//ImGui::Begin("Sprite Collision RAM ");
		//mem_edit.DrawContents(&top->emu__DOT__system__DOT__spritecollisionram__DOT__mem, 32, 0);
		//ImGui::End();
		//ImGui::Begin("Sprite Debug RAM");
		//mem_edit.DrawContents(&top->emu__DOT__system__DOT__spritedebugram__DOT__mem, 128000, 0);
		//ImGui::End();
		//ImGui::Begin("Palette ROM");
		//mem_edit.DrawContents(&top->emu__DOT__system__DOT__palrom__DOT__mem, 64, 0);
		//ImGui::End();
		//ImGui::Begin("Sprite ROM");
		//mem_edit.DrawContents(&top->emu__DOT__system__DOT__spriterom__DOT__mem, 2048, 0);
		//ImGui::End();
		//ImGui::Begin("Tilemap ROM");
		//mem_edit.DrawContents(&top->emu__DOT__system__DOT__tilemaprom__DOT__mem, 8192, 0);
		//ImGui::End();
		//ImGui::Begin("Tilemap RAM");
		//	mem_edit.DrawContents(&top->emu__DOT__system__DOT__tilemapram__DOT__mem, 768, 0);
		//ImGui::End();
		//ImGui::Begin("Sound ROM");
		//mem_edit.DrawContents(&top->emu__DOT__system__DOT__soundrom__DOT__mem, 64000, 0);
		//ImGui::End();

		int windowX = 550;
		int windowWidth = (VGA_WIDTH * VGA_SCALE_X) + 24;
		int windowHeight = (VGA_HEIGHT * VGA_SCALE_Y) + 90;

		// Video window
		ImGui::Begin(windowTitle_Video);
		ImGui::SetWindowPos(windowTitle_Video, ImVec2(windowX, 0), ImGuiCond_Once);
		ImGui::SetWindowSize(windowTitle_Video, ImVec2(windowWidth, windowHeight), ImGuiCond_Once);

		ImGui::SliderFloat("Zoom", &vga_scale, 1, 8); ImGui::SameLine();
		ImGui::SliderInt("Rotate", &video.output_rotate, -1, 1); ImGui::SameLine();
		ImGui::Checkbox("Flip V", &video.output_vflip);
		ImGui::Text("main_time: %d frame_count: %d sim FPS: %f Stream.dat: %d", main_time, video.count_frame, video.stats_fps, stream_dat_count);
		ImGui::Text("EXT_BUS: %lu", EXT_BUS);

		// Draw VGA output
		ImGui::Image(video.texture_id, ImVec2(video.output_width * VGA_SCALE_X, video.output_height * VGA_SCALE_Y));
		ImGui::End();

  if (ImGuiFileDialog::Instance()->Display("ChooseFileDlgKey"))
  {
    // action if OK
    if (ImGuiFileDialog::Instance()->IsOk())
    {
      std::string filePathName = ImGuiFileDialog::Instance()->GetFilePathName();
      std::string filePath = ImGuiFileDialog::Instance()->GetCurrentPath();
      // action
fprintf(stderr,"filePathName: %s\n",filePathName.c_str());
fprintf(stderr,"filePath: %s\n",filePath.c_str());
     bus.QueueDownload(filePathName, 1,0);
    }
   
    // close
    ImGuiFileDialog::Instance()->Close();
  }


#ifndef DISABLE_AUDIO

		ImGui::Begin(windowTitle_Audio);
		ImGui::SetWindowPos(windowTitle_Audio, ImVec2(windowX, windowHeight), ImGuiCond_Once);
		ImGui::SetWindowSize(windowTitle_Audio, ImVec2(windowWidth, 250), ImGuiCond_Once);

		
		//float vol_l = ((signed short)(top->AUDIO_L) / 256.0f) / 256.0f;
		//float vol_r = ((signed short)(top->AUDIO_R) / 256.0f) / 256.0f;
		//ImGui::ProgressBar(vol_l + 0.5f, ImVec2(200, 16), 0); ImGui::SameLine();
		//ImGui::ProgressBar(vol_r + 0.5f, ImVec2(200, 16), 0);

		int ticksPerSec = (clk_sys_freq / 60);
		if (run_enable) {
			audio.CollectDebug((signed short)top->AUDIO_L, (signed short)top->AUDIO_R);
		}
		int channelWidth = (windowWidth / 2)  -16;
		//ImPlot::CreateContext();
		//if (ImPlot::BeginPlot("Audio - L", ImVec2(channelWidth, 220), ImPlotFlags_NoLegend | ImPlotFlags_NoMenus | ImPlotFlags_NoTitle)) {
			//ImPlot::SetupAxes("T", "A", ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_NoTickMarks, ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_NoTickMarks);
			//ImPlot::SetupAxesLimits(0, 1, -1, 1, ImPlotCond_Once);
			//ImPlot::PlotStairs("", audio.debug_positions, audio.debug_wave_l, audio.debug_max_samples, audio.debug_pos);
			//ImPlot::EndPlot();
		//}
		ImGui::SameLine();
		//if (ImPlot::BeginPlot("Audio - R", ImVec2(channelWidth, 220), ImPlotFlags_NoLegend | ImPlotFlags_NoMenus | ImPlotFlags_NoTitle)) {
			//ImPlot::SetupAxes("T", "A", ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_NoTickMarks, ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_NoTickMarks);
			//ImPlot::SetupAxesLimits(0, 1, -1, 1, ImPlotCond_Once);
			//ImPlot::PlotStairs("", audio.debug_positions, audio.debug_wave_r, audio.debug_max_samples, audio.debug_pos);
			//ImPlot::EndPlot();
		//}
		//ImPlot::DestroyContext();
		ImGui::End();
#endif

		video.UpdateTexture();


		// Pass inputs to sim

		//top->menu = input.inputs[input_menu];

		top->joystick_0 = 0;
		for (int i = 0; i < input.inputCount; i++)
		{
			if (input.inputs[i]) { top->joystick_0 |= (1 << i); }
		}
		top->joystick_1 = top->joystick_0;

		/*top->joystick_analog_0 += 1;
		top->joystick_analog_0 -= 256;*/
		//top->paddle_0 += 1;
		//if (input.inputs[0] || input.inputs[1]) {
		//	spinner_toggle = !spinner_toggle;
		//	top->spinner_0 = (input.inputs[0]) ? 16 : -16;
		//	for (char b = 8; b < 16; b++) {
		//		top->spinner_0 &= ~(1UL << b);
		//	}
		//	if (spinner_toggle) { top->spinner_0 |= 1UL << 8; }
		//}

		mouse_buttons = 0;
		mouse_x = 0;
		mouse_y = 0;
		if (input.inputs[input_left]) { mouse_x = -2; }
		if (input.inputs[input_right]) { mouse_x = 2; }
		if (input.inputs[input_up]) { mouse_y = 2; }
		if (input.inputs[input_down]) { mouse_y = -2; }

		if (input.inputs[input_a]) { mouse_buttons |= (1UL << 0); }
		if (input.inputs[input_b]) { mouse_buttons |= (1UL << 1); }

		unsigned long mouse_temp = mouse_buttons;
		mouse_temp += (mouse_x << 8);
		mouse_temp += (mouse_y << 16);
		if (mouse_clock) { mouse_temp |= (1UL << 24); }
		mouse_clock = !mouse_clock;

		//top->ps2_mouse = mouse_temp;
		//top->ps2_mouse_ext = mouse_x + (mouse_buttons << 8);

		// Run simulation
		if (run_enable) {
			for (int step = 0; step < batchSize; step++) { verilate(); }
		}
		else {
			if (single_step) { verilate(); }
			if (multi_step) {
				for (int step = 0; step < multi_step_amount; step++) { verilate(); }
			}
		}
	}

	// Clean up before exit
	// --------------------

#ifndef DISABLE_AUDIO
	audio.CleanUp();
#endif 
	video.CleanUp();
	input.CleanUp();

	return 0;
}
