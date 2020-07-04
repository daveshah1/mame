// license:BSD-3-Clause
// copyright-holders:David Shah

/*

Monkey King SoCs (currently only 3B is supported)

Presumably-custom ARM-based system-on-chips by Digital Media Cartridge (DMC).
Intended to run NES and Genesis emulators, primarily for ATgames systems.

Sometimes abbreviated MK. It is a successor of the Titan SoC used in previous
emulation based ATgames systems.

Monkey King and Monkey 2: Presumed custom. Used in some ATgames/Blaze
Genesis systems and the Atari Flashback Portable.

Monkey King 3 and Monkey King 3B: Presumed custom. Used in the ATgames
BLAST system and the RS-70 648-in-1 "PS1 form factor" clone. Supports
HDMI output.

Monkey King 3.6: not a custom part but a rebranded RK3036, usually
running a cut-down Android based OS. Used in newer ATgames systems.

The typical configuration of the Monkey King SoCs (other than the
3.6) is with 8/16MB of SDRAM, NOR flash for the firmware and
built-in games, and a SD card for additional games.

The RS-70 is notable for having a debug UART on the USB port
(serial TX on D+, 115200). It prints the following messages on boot:

	EXEC: Executing 'boot' with 0 args (ZLib ON)...
	EXEC: Loading 'boot' at 0x18000000...
	EXEC: Loaded 372272 bytes of 2097152 available.

This is different from the serial output that this emulation model
currently produces. Perhaps one of the unimplemented IO is causing
it to go into some kind of debug mode. The log output produced by
this machine is:

	Modes:0x00000000
	PUT: Setting joystick to mode 0x0, timer to 250us

	******************************************************
	 MK FIRMWARE INFORMATION
	 Mode:       0xB4
	 Build Time: May  8 2019 14:09:21
	 CPU Clock:  240MHz
	 TFS Start:  0x8070000
	 Video Buf:  0x6000000
	 Stack Top:  0x3001EE8
	 IWRAM Size: 32kB
	 EVRAM Size: 16384kB
	 Heap Size:  6144kB at 0x18200000
	 Video Mode: 0
	 Video Size: 1280x720x16bpp
	******************************************************

There are other strings in the ROM that imply there may be more serial
debug possibilities.

TODO:
	implement everything
	add dumps of more Monkey King systems
*/

#include "emu.h"
#include "cpu/arm7/arm7.h"
#include "cpu/arm7/arm7core.h"
#include "emupal.h"
#include "screen.h"

class mk3b_soc_state : public driver_device
{
public:
	mk3b_soc_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag),
		m_iram0(*this, "iram0"),
		m_iram3(*this, "iram3"),
		m_iram5(*this, "iram5"),
		m_sdram(*this, "sdram"),
		m_maincpu(*this, "maincpu"),
		m_screen(*this, "screen"),
		m_io_p1(*this, "IN0")
	{ }

	void mk3b_soc(machine_config &config);

	void init_rs70();

private:
	required_shared_ptr<uint32_t> m_iram0, m_iram3, m_iram5;
	required_shared_ptr<uint32_t> m_sdram;
	required_device<cpu_device> m_maincpu;
	required_device<screen_device> m_screen;
	required_ioport m_io_p1;

	virtual void machine_reset() override;
	virtual void video_start() override;
	virtual void device_start() override;

	uint32_t io4_r(offs_t offset, uint32_t mem_mask = ~0);
	void io4_w(offs_t offset, uint32_t data, uint32_t mem_mask = ~0);

	uint32_t io6_r(offs_t offset, uint32_t mem_mask = ~0);
	void io6_w(offs_t offset, uint32_t data, uint32_t mem_mask = ~0);

	uint32_t io7_r(offs_t offset, uint32_t mem_mask = ~0);
	void io7_w(offs_t offset, uint32_t data, uint32_t mem_mask = ~0);

	uint32_t io10_r(offs_t offset, uint32_t mem_mask = ~0);
	void io10_w(offs_t offset, uint32_t data, uint32_t mem_mask = ~0);

	uint32_t sdram_r(offs_t offset, uint32_t mem_mask = ~0);

	uint32_t screen_update_mk3b_soc(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect);
	void map(address_map &map);
	std::string debug_buf;
	uint32_t m_ioregs7[16384];
	int i = 0;
	uint32_t m_timer_time = 0;
	bool m_timer_enabled = false;

	emu_timer *m_sys_timer;
	virtual void device_timer(emu_timer &timer, device_timer_id id, int param, void *ptr) override;
};


void mk3b_soc_state::map(address_map &map)
{
	// 64MB external NOR flash
	map(0x08000000, 0x0BFFFFFF).rom().share("norflash").region("norflash", 0x0);;
	// unknown amount and configuration of internal RAM
	map(0x00000000, 0x0000FFFF).ram().share("iram0");
	// This section of RAM seems to contain the stack
	map(0x03000000, 0x0300FFFF).ram().share("iram3");
	map(0x03FF0000, 0x03FFFFFF).ram().share("iram3");
	// unknown if this is RAM or IO
	map(0x05000000, 0x0500FFFF).ram().share("iram5");

	// 16MB of external SDRAM
	map(0x18000000, 0x18FFFFFF).ram().share("sdram").r(FUNC(mk3b_soc_state::sdram_r));
	// IO is totally unknown for now
	// 0x04... seems to be timer and IRQ stuff
	map(0x04000000, 0x0400FFFF).rw(FUNC(mk3b_soc_state::io4_r), FUNC(mk3b_soc_state::io4_w));
	// 0x06... let's assume this aliases to the main framebuffer for now
	map(0x06000000, 0x067FFFFF).rw(FUNC(mk3b_soc_state::io6_r), FUNC(mk3b_soc_state::io6_w));
	// 0x07... seems to be a mix of video-related IO and SRAM
	map(0x07000000, 0x0700FFFF).rw(FUNC(mk3b_soc_state::io7_r), FUNC(mk3b_soc_state::io7_w));
	// 0x10... seems to be misc IO
	map(0x10000000, 0x1000FFFF).rw(FUNC(mk3b_soc_state::io10_r), FUNC(mk3b_soc_state::io10_w));
}

static INPUT_PORTS_START( mk3b_soc )
	PORT_START("IN0")
	PORT_DIPNAME( 0x00000001, 0x00000000, "B0" )
	PORT_DIPSETTING( 0x00000000, DEF_STR( Off ) )
	PORT_DIPSETTING( 0x00000001, DEF_STR( On ) )
	PORT_DIPNAME( 0x00000002, 0x00000000, "B1" )
	PORT_DIPSETTING( 0x00000000, DEF_STR( Off ) )
	PORT_DIPSETTING( 0x00000002, DEF_STR( On ) )
	PORT_DIPNAME( 0x00000004, 0x00000000, "B2" )
	PORT_DIPSETTING( 0x00000000, DEF_STR( Off ) )
	PORT_DIPSETTING( 0x00000004, DEF_STR( On ) )
	PORT_DIPNAME( 0x00000008, 0x00000000, "B3" )
	PORT_DIPSETTING( 0x00000000, DEF_STR( Off ) )
	PORT_DIPSETTING( 0x00000008, DEF_STR( On ) )
	PORT_DIPNAME( 0x00000010, 0x00000000, "B4" )
	PORT_DIPSETTING( 0x00000000, DEF_STR( Off ) )
	PORT_DIPSETTING( 0x00000010, DEF_STR( On ) )
	PORT_DIPNAME( 0x00000020, 0x00000000, "B5" )
	PORT_DIPSETTING( 0x00000000, DEF_STR( Off ) )
	PORT_DIPSETTING( 0x00000020, DEF_STR( On ) )
	PORT_DIPNAME( 0x00000040, 0x00000000, "B6" )
	PORT_DIPSETTING( 0x00000000, DEF_STR( Off ) )
	PORT_DIPSETTING( 0x00000040, DEF_STR( On ) )
	PORT_DIPNAME( 0x00000080, 0x00000000, "B7" )
	PORT_DIPSETTING( 0x00000000, DEF_STR( Off ) )
	PORT_DIPSETTING( 0x00000080, DEF_STR( On ) )
	PORT_DIPNAME( 0x00000100, 0x00000000, "B8" )
	PORT_DIPSETTING( 0x00000000, DEF_STR( Off ) )
	PORT_DIPSETTING( 0x00000100, DEF_STR( On ) )
	PORT_DIPNAME( 0x00000200, 0x00000000, "B9" )
	PORT_DIPSETTING( 0x00000000, DEF_STR( Off ) )
	PORT_DIPSETTING( 0x00000200, DEF_STR( On ) )
	PORT_DIPNAME( 0x00000400, 0x00000000, "B10" )
	PORT_DIPSETTING( 0x00000000, DEF_STR( Off ) )
	PORT_DIPSETTING( 0x00000400, DEF_STR( On ) )
	PORT_DIPNAME( 0x00000800, 0x00000000, "B11" )
	PORT_DIPSETTING( 0x00000000, DEF_STR( Off ) )
	PORT_DIPSETTING( 0x00000800, DEF_STR( On ) )
	PORT_DIPNAME( 0x00001000, 0x00000000, "B12" )
	PORT_DIPSETTING( 0x00000000, DEF_STR( Off ) )
	PORT_DIPSETTING( 0x00001000, DEF_STR( On ) )
	PORT_DIPNAME( 0x00002000, 0x00000000, "B13" )
	PORT_DIPSETTING( 0x00000000, DEF_STR( Off ) )
	PORT_DIPSETTING( 0x00002000, DEF_STR( On ) )
	PORT_DIPNAME( 0x00004000, 0x00000000, "B14" )
	PORT_DIPSETTING( 0x00000000, DEF_STR( Off ) )
	PORT_DIPSETTING( 0x00004000, DEF_STR( On ) )
	PORT_DIPNAME( 0x00008000, 0x00000000, "B15" )
	PORT_DIPSETTING( 0x00000000, DEF_STR( Off ) )
	PORT_DIPSETTING( 0x00008000, DEF_STR( On ) )
	PORT_DIPNAME( 0x00010000, 0x00000000, "B16" )
	PORT_DIPSETTING( 0x00000000, DEF_STR( Off ) )
	PORT_DIPSETTING( 0x00010000, DEF_STR( On ) )
	PORT_DIPNAME( 0x00020000, 0x00000000, "B17" )
	PORT_DIPSETTING( 0x00000000, DEF_STR( Off ) )
	PORT_DIPSETTING( 0x00020000, DEF_STR( On ) )
	PORT_DIPNAME( 0x00040000, 0x00000000, "B18" )
	PORT_DIPSETTING( 0x00000000, DEF_STR( Off ) )
	PORT_DIPSETTING( 0x00040000, DEF_STR( On ) )
	PORT_DIPNAME( 0x00080000, 0x00000000, "B19" )
	PORT_DIPSETTING( 0x00000000, DEF_STR( Off ) )
	PORT_DIPSETTING( 0x00080000, DEF_STR( On ) )
	PORT_DIPNAME( 0x00100000, 0x00000000, "B20" )
	PORT_DIPSETTING( 0x00000000, DEF_STR( Off ) )
	PORT_DIPSETTING( 0x00100000, DEF_STR( On ) )
	PORT_DIPNAME( 0x00200000, 0x00000000, "B21" )
	PORT_DIPSETTING( 0x00000000, DEF_STR( Off ) )
	PORT_DIPSETTING( 0x00200000, DEF_STR( On ) )
	PORT_DIPNAME( 0x00400000, 0x00000000, "B22" )
	PORT_DIPSETTING( 0x00000000, DEF_STR( Off ) )
	PORT_DIPSETTING( 0x00400000, DEF_STR( On ) )
	PORT_DIPNAME( 0x00800000, 0x00000000, "B23" )
	PORT_DIPSETTING( 0x00000000, DEF_STR( Off ) )
	PORT_DIPSETTING( 0x00800000, DEF_STR( On ) )
	PORT_DIPNAME( 0x01000000, 0x00000000, "B24" )
	PORT_DIPSETTING( 0x00000000, DEF_STR( Off ) )
	PORT_DIPSETTING( 0x01000000, DEF_STR( On ) )
	PORT_DIPNAME( 0x02000000, 0x00000000, "B25" )
	PORT_DIPSETTING( 0x00000000, DEF_STR( Off ) )
	PORT_DIPSETTING( 0x02000000, DEF_STR( On ) )
	PORT_DIPNAME( 0x04000000, 0x00000000, "B26" )
	PORT_DIPSETTING( 0x00000000, DEF_STR( Off ) )
	PORT_DIPSETTING( 0x04000000, DEF_STR( On ) )
	PORT_DIPNAME( 0x08000000, 0x00000000, "B27" )
	PORT_DIPSETTING( 0x00000000, DEF_STR( Off ) )
	PORT_DIPSETTING( 0x08000000, DEF_STR( On ) )
	PORT_DIPNAME( 0x10000000, 0x00000000, "B28" )
	PORT_DIPSETTING( 0x00000000, DEF_STR( Off ) )
	PORT_DIPSETTING( 0x10000000, DEF_STR( On ) )
	PORT_DIPNAME( 0x20000000, 0x00000000, "B29" )
	PORT_DIPSETTING( 0x00000000, DEF_STR( Off ) )
	PORT_DIPSETTING( 0x20000000, DEF_STR( On ) )
	PORT_DIPNAME( 0x40000000, 0x00000000, "B30" )
	PORT_DIPSETTING( 0x00000000, DEF_STR( Off ) )
	PORT_DIPSETTING( 0x40000000, DEF_STR( On ) )
	PORT_DIPNAME( 0x80000000, 0x00000000, "B31" )
	PORT_DIPSETTING( 0x00000000, DEF_STR( Off ) )
	PORT_DIPSETTING( 0x80000000, DEF_STR( On ) )
INPUT_PORTS_END

void mk3b_soc_state::video_start()
{
}

void mk3b_soc_state::machine_reset()
{
	// In practice, this will probably be done by a small
	// internal boot ROM.
	m_iram0[0] = 0xe59f0000; // ldr r0, [pc]
	m_iram0[1] = 0xe12fff10; // bx, r0
	m_iram0[2] = 0x08000000; // target address

	m_timer_time = 0;
	m_timer_enabled = false;
}

void mk3b_soc_state::device_start()
{
	driver_device::device_start();
	m_sys_timer = timer_alloc(0);
	m_sys_timer->adjust(attotime::never);	
}

uint32_t mk3b_soc_state::screen_update_mk3b_soc(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect)
{
	const uint32_t base = 0x00800000 / 4;
	const int width = (m_ioregs7[0x21] >> 16), height = 2*(m_ioregs7[0x21] & 0xFFFF);
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			uint16_t rgb16 = m_sdram[base + (y * width + x) / 2] >> ((x % 1) ? 16 : 0);
			bitmap.pix32(y, x) = ((rgb16 & 0x1F) << 19) | (((rgb16 & 0x07E0) >> 5) << 10) | ((rgb16 >> 11) << 3);
		}
	}
	return 0;
}

void mk3b_soc_state::mk3b_soc(machine_config &config)
{
	// type unknown (should actually have VFP?)
	// debug output suggests 240MHz clock
	ARM920T(config, m_maincpu, 240000000);
	m_maincpu->set_addrmap(AS_PROGRAM, &mk3b_soc_state::map);

	SCREEN(config, m_screen, SCREEN_TYPE_RASTER);
	m_screen->set_refresh_hz(60);
	m_screen->set_vblank_time(ATTOSECONDS_IN_USEC(2500) /* not accurate */);
	m_screen->set_size(1920, 1080);
	m_screen->set_visarea(0, 1920-1, 0, 1080-1);
	m_screen->set_screen_update(FUNC(mk3b_soc_state::screen_update_mk3b_soc));
}

uint32_t mk3b_soc_state::io4_r(offs_t offset, uint32_t mem_mask)
{
	switch (offset) {
		case 0x00:
			logerror("%s: IO 0x04 read 0x00\n", machine().describe_context());
			return 0x55;
		case 0x01:
			return (m_screen->vblank() << 27) | m_screen->vblank(); // who knows? seems to need to toggle between 0 and 1
		case 0x80: // some kind of IRQ pending
			return 0x44444444;
		case 0x82:
			return 0x04000000;
		default:
			logerror("%s: IO 0x04 read 0x%04X\n", machine().describe_context(), offset);
			return 0x00;
	}
}

void mk3b_soc_state::io4_w(offs_t offset, uint32_t data, uint32_t mem_mask)
{
	switch (offset) {
		case 0x41:
			logerror("%s: set timer0 %08x %08x\n", machine().describe_context(), data, mem_mask);
			if (mem_mask & 0xFFFF)
				m_timer_time = data & 0xFFFF;
			if (mem_mask & 0x00FF0000) {
				if (data & 0x00800000) {
					logerror("%s: enable timer0\n", machine().describe_context());
					m_sys_timer->adjust(attotime::from_ticks(m_timer_time, 240000));
					m_timer_enabled = true;
				} else {
					logerror("%s: disable timer0\n", machine().describe_context());
					m_sys_timer->adjust(attotime::never);
					m_timer_enabled = false;
				}
			}
			break;
		case 0x80:
			break;
		case 0x82:
			//logerror("%s: timer0 ctl %08x & %08x\n", machine().describe_context(), data, mem_mask);
			if (data & 0x04000000) {
				m_maincpu->set_input_line(ARM7_IRQ_LINE, CLEAR_LINE);
				if (m_timer_enabled)
					m_sys_timer->adjust(attotime::from_ticks(m_timer_time, 240000));
			}
			break;
		default:
			logerror("%s: IO 0x04 write 0x%04X 0x%08X & 0x%08X\n", machine().describe_context(), offset, data, mem_mask);
			break;
	}
}


void mk3b_soc_state::device_timer(emu_timer &timer, device_timer_id id, int param, void *ptr)
{
	switch (id) {
	case 0:
		m_maincpu->set_input_line(ARM7_IRQ_LINE, ASSERT_LINE);
		break;
	}
}

uint32_t mk3b_soc_state::io6_r(offs_t offset, uint32_t mem_mask)
{
	return m_sdram[offset + (0x00800000 / 4)];
}

void mk3b_soc_state::io6_w(offs_t offset, uint32_t data, uint32_t mem_mask)
{
	m_sdram[offset + (0x00800000 / 4)] = (m_sdram[offset + (0x00800000 / 4)] & ~mem_mask) |
		(data & mem_mask);
}

uint32_t mk3b_soc_state::io7_r(offs_t offset, uint32_t mem_mask)
{
	switch (offset) {
		case 0x21: // video size
			// Without the *2 the image is cut off
			return (m_ioregs7[offset] & 0xFFFF0000) | ((m_ioregs7[offset] & 0x00007FFF) * 2);
		case 0x12:
			return m_screen->vblank() ? 0xFF : 0x00;
		case 0x1E:
			//logerror("%s: IO 0x07 read 0x%04X %08X\n", machine().describe_context(), offset, mem_mask);
			//return m_screen->vblank() ? 0x01 : 0x00;
			return m_io_p1->read();
		case 0x00:
		case 0x01:
			//logerror("%s: IO 0x07 read 0x%04X %08X\n", machine().describe_context(), offset, mem_mask);
			return m_io_p1->read();
		default:
			if (offset < 0x10)
				logerror("%s: IO 0x07 read 0x%04X %08X\n", machine().describe_context(), offset, mem_mask);
			return m_ioregs7[offset];
	}
}

void mk3b_soc_state::io7_w(offs_t offset, uint32_t data, uint32_t mem_mask)
{
	//logerror("%s: IO 0x07 write 0x%04X 0x%08X & 0x%08X\n", machine().describe_context(), offset, data, mem_mask);
	m_ioregs7[offset] = (m_ioregs7[offset] & ~mem_mask) | (data & mem_mask);
}

uint32_t mk3b_soc_state::io10_r(offs_t offset, uint32_t mem_mask)
{
	switch (offset) {
		// Definitely not correct, but toggling somehow keeps things moving
		case 0x008:
			return 0xFFFFFFFF;
		case 0x148:
		case 0x149:
			logerror("%s: read %08x %08x\n", machine().describe_context(), offset, mem_mask);
			return m_screen->vblank() ? 0x00000000 : 0xFFFFFFFF;
		default:
			logerror("%s: IO 0x10 read 0x%04X\n", machine().describe_context(), offset);
			return 0x00;
	}
}

uint32_t mk3b_soc_state::sdram_r(offs_t offset, uint32_t mem_mask)
{
	/*
	if (((i++) % 91) == 0 && ((offset * 4) & 0xFFF000) != 0xFFC000)
		logerror("%s: SDRAM read 0x%06X\n", machine().describe_context(), offset*4);
	*/
	if ((offset * 4) == 0xF03AF0)
		return 0; // Why is this needed?
	return m_sdram[offset];
}


void mk3b_soc_state::io10_w(offs_t offset, uint32_t data, uint32_t mem_mask)
{
	switch (offset) {
		case 0x148: { // debug UART
			char c =  data & 0xFF;
			logerror("%s: UART W: %c\n", machine().describe_context(), c);
			if (c == '\n') {
				logerror("%s: [DEBUG] %s\n", machine().describe_context(), debug_buf.c_str());
				debug_buf.clear();
			} else if (c != '\r') {
				debug_buf += c;
			}
			break;
		}
		default:
			logerror("%s: IO 0x10 write 0x%04X 0x%08X & 0x%08X\n", machine().describe_context(), offset, data, mem_mask);
	}
}

void mk3b_soc_state::init_rs70()
{
	// Uppermost address bit seems to be inverted
	uint8_t *ROM = memregion("norflash")->base();
	int size = memregion("norflash")->bytes();

	for (int i = 0; i < (size / 2); i++) {
		std::swap(ROM[i], ROM[i + (size / 2)]);
	}
	// FIXME: Work around missing FPU for now
	//ROM[0x32f24] = 0x00;
	//ROM[0x32f25] = 0x00;
	//ROM[0x32f26] = 0x00;
	//ROM[0x32f27] = 0x00;
}


ROM_START( rs70_648 )
	ROM_REGION(0x04000000, "norflash", 0)
	ROM_LOAD("s29gl512p.bin", 0x000000, 0x04000000, CRC(cb452bd7) SHA1(0b19a13a3d0b829725c10d64d7ff852ff5202ed0) )
ROM_END

CONS( 2019, rs70_648,  0,        0, mk3b_soc, mk3b_soc, mk3b_soc_state, init_rs70, "<unknown>", "RS-70 648-in-1",      MACHINE_IS_SKELETON )
