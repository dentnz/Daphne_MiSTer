#include "../../verilator/common.h"
#include "spi.h"
//#include "hardware.h"
//#include "fpga_io.h"

//#define SSPI_FPGA_EN (1<<18)
//#define SSPI_OSD_EN  (1<<19)
//#define SSPI_IO_EN   (1<<20)

//#define SWAPW(a) ((((a)<<8)&0xff00)|(((a)>>8)&0x00ff))

void EnableFpga()
{
//	fpga_spi_en(SSPI_FPGA_EN, 1);

}

void DisableFpga()
{
//	fpga_spi_en(SSPI_FPGA_EN, 0);
}

//static int osd_target = OSD_ALL;

void EnableOsd_on(int target)
{
//	if (!(target & OSD_ALL)) target = OSD_ALL;
//	osd_target = target;
}

void EnableOsd()
{
//	if (!(osd_target & OSD_ALL)) osd_target = OSD_ALL;
//
//	uint32_t mask = SSPI_OSD_EN | SSPI_IO_EN | SSPI_FPGA_EN;
//	if (osd_target & OSD_HDMI) mask &= ~SSPI_FPGA_EN;
//	if (osd_target & OSD_VGA) mask &= ~SSPI_IO_EN;
//
//	fpga_spi_en(mask, 1);
}

void DisableOsd()
{
//	fpga_spi_en(SSPI_OSD_EN | SSPI_IO_EN | SSPI_FPGA_EN, 0);
}

uint8_t io_enabled = 0;

void EnableIO()
{
    if (io_enabled == 1) {
        return;
    }

    printf("Main_MiSTeR -  enable IO\n");
    io_enabled = 1;
//	fpga_spi_en(SSPI_IO_EN, 1);
    // Mock the IO Enable bit, bit 34
    top->EXT_BUS |= 1UL << 34;
    top->EXT_BUS_IN |= 1UL << 34;
    top->EXT_BUS_OUT |= 1UL << 34;
}

void DisableIO()
{
    if (io_enabled == 0) {
        return;
    }
    printf("Main_MiSTeR -  disable IO\n");
    io_enabled = 0;
//	fpga_spi_en(SSPI_IO_EN, 0);
    // Mock disabling the IO Enable bit
    top->EXT_BUS &= ~(1UL << 34);
    top->EXT_BUS_IN &= ~(1UL << 34);
    top->EXT_BUS_OUT &= ~(1UL << 34);
}

uint32_t spi32_w(uint32_t parm)
{
//	uint32_t res;
//	res = spi_w(parm);
//	res |= (spi_w(parm>>16))<<16;
//	return res;
}

// little endian: lsb first
void spi32_b(uint32_t parm)
{
//	spi_b(parm >> 0);
//	spi_b(parm >> 8);
//	spi_b(parm >> 16);
//	spi_b(parm >> 24);
}

/* OSD related SPI functions */
void spi_osd_cmd_cont(uint8_t cmd)
{
//	EnableOsd();
//	spi_b(cmd);
}

void spi_osd_cmd(uint8_t cmd)
{
//	spi_osd_cmd_cont(cmd);
//	DisableOsd();
}

void spi_osd_cmd8_cont(uint8_t cmd, uint8_t parm)
{
//	EnableOsd();
//	spi_b(cmd);
//	spi_b(parm);
}

void spi_osd_cmd8(uint8_t cmd, uint8_t parm)
{
//	spi_osd_cmd8_cont(cmd, parm);
//	DisableOsd();
}

void spi_uio_cmd32_cont(uint8_t cmd, uint32_t parm)
{
//	EnableIO();
//	spi_b(cmd);
//	spi32_b(parm);
}

/* User_io related SPI functions */
vluint64_t incoming_command_byte_count = 0;
uint16_t last_command = 255;
uint16_t last_command_data1 = 0;
uint16_t last_command_data2 = 0;

/**
 * @param cmd - command you want to send to the FPGA
 * @returns req - request result, 255 is "no request result"
 */
uint16_t spi_uio_cmd_cont(uint16_t cmd)
{
	EnableIO();

    if (top->EXT_BUS_OUT & (1ULL << 32)) {
        incoming_command_byte_count++;

        if (incoming_command_byte_count == 4) {
            last_command = top->EXT_BUS_OUT;
            printf("Main_MiSTer - command detected - byte 1 - %lu\n", top->EXT_BUS_OUT);
        }
        if (incoming_command_byte_count == 5) {
            printf("Main_MiSTer - command detected - byte 2 - %lu\n", top->EXT_BUS_OUT);
        }
        if (incoming_command_byte_count == 6) {
            printf("Main_MiSTer - command detected - byte 3 - %lu\n", top->EXT_BUS_OUT);
            incoming_command_byte_count = 0;
            return last_command;
        }

        return 255;
    }

    top->EXT_BUS_IN = cmd;
    top->EXT_BUS_IN = top->EXT_BUS_IN << 16;
    top->EXT_BUS_IN |= 1UL << 33;
    top->EXT_BUS_IN |= 1UL << 34;

    return 255;
//	return spi_w(cmd);
}

uint16_t spi_uio_cmd(uint16_t cmd)
{
//	uint16_t res = spi_uio_cmd_cont(cmd);
//	DisableIO();
//	return res;
}

uint8_t spi_uio_cmd8_cont(uint8_t cmd, uint8_t parm)
{
//	EnableIO();
//	spi_b(cmd);
//	return spi_b(parm);
}

uint8_t spi_uio_cmd8(uint8_t cmd, uint8_t parm)
{
//	uint8_t res = spi_uio_cmd8_cont(cmd, parm);
//	DisableIO();
//	return res;
}

uint16_t spi_uio_cmd16(uint8_t cmd, uint16_t parm)
{
//	spi_uio_cmd_cont(cmd);
//	uint16_t res = spi_w(parm);
//	DisableIO();
//	return res;
}

void spi_uio_cmd32(uint8_t cmd, uint32_t parm, int wide)
{
//	EnableIO();
//	spi_b(cmd);
//	if (wide)
//	{
//		spi_w((uint16_t)parm);
//		spi_w((uint16_t)(parm >> 16));
//	}
//	else
//	{
//		spi_b(parm);
//		spi_b(parm >> 8);
//		spi_b(parm >> 16);
//		spi_b(parm >> 24);
//	}
//	DisableIO();
}

void spi_n(uint8_t value, uint16_t cnt)
{
//	while (cnt--) spi_b(value);
}

void spi_read(uint8_t *addr, uint32_t len, int wide)
{
//	if (wide)
//	{
//		uint32_t len16 = len >> 1;
//		uint16_t *a16 = (uint16_t*)addr;
//		while (len16--) *a16++ = spi_w(0);
//		if (len & 1) *((uint8_t*)a16) = spi_w(0);
//	}
//	else
//	{
//		while (len--) *addr++ = spi_b(0);
//	}
}

void spi_write(const uint8_t *addr, uint32_t len, int wide)
{
//	if (wide)
//	{
//		uint32_t len16 = len >> 1;
//		uint16_t *a16 = (uint16_t*)addr;
//		while (len16--) spi_w(*a16++);
//		if(len & 1) spi_w(*((uint8_t*)a16));
//	}
//	else
//	{
//		while (len--) spi_b(*addr++);
//	}
}

void spi_block_read(uint8_t *addr, int wide, int sz)
{
//	if (wide) fpga_spi_fast_block_read((uint16_t*)addr, sz/2);
//	else fpga_spi_fast_block_read_8(addr, sz);
}

void spi_block_write(const uint8_t *addr, int wide, int sz)
{
//	if (wide) fpga_spi_fast_block_write((const uint16_t*)addr, sz/2);
//	else fpga_spi_fast_block_write_8(addr, sz);
}
