#include "../file_io.h"
#include "../user_io.h"
#include "../spi.h"
#include <stdio.h>
#include <string.h>

////////////// VLDP /////////////

#define MSU_CD_SET               1
#define MSU_AUDIO_TRACK_MOUNTED  2
#define MSU_DATA_BASE            3

static char snes_romFileName[1024] = {};
static char SelectedPath[1024] = {};
static uint8_t buf[1024];
static char has_cd = 0;
static fileTYPE f_audio = {};

/*
static void daphne_send_command(uint64_t cmd)
{
	spi_uio_cmd_cont(UIO_CD_SET);
	spi_w((cmd >> 00) & 0xFFFF);
	spi_w((cmd >> 16) & 0xFFFF);
	spi_w((cmd >> 32) & 0xFFFF);
	DisableIO();
}

static int daphne_send_data(fileTYPE *f, int idx)
{
	int chunk = sizeof(buf);

	memset(buf, 0, chunk);
	if (f->size) FileReadAdv(f, buf, chunk);

	user_io_set_index(idx);
	user_io_set_download(1);
	user_io_file_tx_data(buf, chunk);
	user_io_set_download(0);

	return 1;
}
*/

/*
void snes_msu_init(const char* name)
{
	static fileTYPE f = {};
	FileClose(&f_audio);

	memset(snes_romFileName, 0, 1024);
	int extSize = strlen(strrchr(name, '.'));
	strncpy(snes_romFileName, name, strlen(name) - extSize);
	printf("MSU: Rom named '%s' initialised\n", name);

	snprintf(SelectedPath, sizeof(SelectedPath), "%s.msu", snes_romFileName);
	has_cd = FileOpen(&f, SelectedPath) ? 1 : 0;
	uint32_t size = f.size;
	FileClose(&f);

	printf("MSU: enable cd: %d\n", has_cd);

	if (size && size < 0x1F200000)
	{
		msu_send_command((0x20600000ULL << 16) | MSU_DATA_BASE);
		user_io_file_tx(SelectedPath, 3, 0, 0, 0, 0x20600000);
	}

	msu_send_command((has_cd << 15) | MSU_CD_SET);
}
*/

uint8_t request_latch = 0;
uint8_t last_req = 255;
uint8_t req = 255;

uint8_t daphne_poll(void)
{

	// TODO Handle this later
	//if (!has_cd) return;

    req = spi_uio_cmd_cont(UIO_CD_GET);

	if (req != last_req)
	{
        last_req = req;
		DisableIO();
		printf("req finished");
		return 1;
        /*
		uint16_t command = spi_w(0);
		uint32_t data = spi_w(0);
		data = (spi_w(0) << 16) | data;
		DisableIO();

		switch(command)
		{
		case 0xFF:
			printf("MSU: request to reset\n");
			break;

		case 0x35:
			snprintf(SelectedPath, sizeof(SelectedPath), "%s-%d.pcm", snes_romFileName, data);
			printf("MSU: New track selected: %s\n", SelectedPath);
			FileOpen(&f_audio, SelectedPath);
			printf(f_audio.size ? "MSU: Track mounted\n" : "MSU: Track not found!\n");
			msu_send_command((f_audio.size << 16) | MSU_AUDIO_TRACK_MOUNTED);
			break;

		case 0x36:
			printf("MSU: Jump to offset: 0x%X\n", data * 1024);
			FileSeek(&f_audio, data * 1024, SEEK_SET);
			// fallthrough

		case 0x34:
			// Next sector requested
			msu_send_data(&f_audio, 2);
			break;
		}
		*/
	}
	else
	{
		//DisableIO();
	}

	return 0;
}