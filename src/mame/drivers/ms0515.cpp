// license:BSD-3-Clause
// copyright-holders:Miodrag Milanovic, Sergey Svishchev
/***************************************************************************

    Elektronika MS 0515

    To do:
    - softlist
    . sound
    - 512K memory expansion
    - ?? refresh rate change
    - ?? parallel printer
    - ?? cassette (only with Version A firmware)
    - ?? port 177770
    - ?? mc1702 (8086 co-processor)

    Docs:
    - http://www.tis.kz/docs/MC-0515/mc0515-ed.rar schematics etc.
    - http://www.tis.kz/docs/MC-0515/mc0515-to.rar user manual
    - http://www.tis.kz/docs/MC-0515/hc4-to.rar technical manual
    - http://www.tis.kz/docs/MC-0515/mc0515-po.rar diag manual
    - http://www.tis.kz/docs/MC-0515/mc0515-osa.rar OS manual

****************************************************************************/

#include "emu.h"

#include "bus/rs232/rs232.h"
#include "cpu/t11/t11.h"
#include "machine/clock.h"
#include "machine/i8251.h"
#include "machine/i8255.h"
#include "machine/ms7004.h"
#include "machine/pit8253.h"
#include "machine/ram.h"
#include "machine/wd_fdc.h"
#include "sound/spkrdev.h"
#include "sound/wave.h"

#include "screen.h"
#include "speaker.h"

#include "formats/ms0515_dsk.h"

#include "ms0515.lh"


#define LOG_GENERAL (1U <<  0)
#define LOG_BANK    (1U <<  1)
#define LOG_SYSREG  (1U <<  2)

//#define VERBOSE (LOG_GENERAL | LOG_BANK | LOG_SYSREG)
//#define LOG_OUTPUT_FUNC printf
#include "logmacro.h"

#define LOGBANK(format, ...)    LOGMASKED(LOG_BANK,   "%11.6f at %s: " format, machine().time().as_double(), machine().describe_context(), __VA_ARGS__)
#define LOGSYSREG(format, ...)  LOGMASKED(LOG_SYSREG, "%11.6f at %s: " format, machine().time().as_double(), machine().describe_context(), __VA_ARGS__)


class ms0515_state : public driver_device
{
public:
	ms0515_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_maincpu(*this, "maincpu")
		, m_ram(*this, RAM_TAG)
		, m_fdc(*this, "vg93")
		, m_floppy0(*this, "vg93:0:525qd")
		, m_floppy1(*this, "vg93:1:525qd")
		, m_i8251line(*this, "i8251line")
		, m_rs232(*this, "rs232")
		, m_i8251kbd(*this, "i8251kbd")
		, m_ms7004(*this, "ms7004")
		, m_pit8253(*this, "pit8253")
		, m_speaker(*this, "speaker")
	{ }

	DECLARE_PALETTE_INIT(ms0515);
	uint32_t screen_update_ms0515(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect);
	DECLARE_WRITE_LINE_MEMBER(screen_vblank);

	DECLARE_WRITE16_MEMBER(ms0515_bank_w);

	DECLARE_READ16_MEMBER(ms0515_halt_r);
	DECLARE_WRITE16_MEMBER(ms0515_halt_w);

	DECLARE_WRITE8_MEMBER(ms0515_porta_w);
	DECLARE_READ8_MEMBER(ms0515_portb_r);
	DECLARE_WRITE8_MEMBER(ms0515_portc_w);

	DECLARE_WRITE_LINE_MEMBER(write_keyboard_clock);
	DECLARE_WRITE_LINE_MEMBER(write_line_clock);
	DECLARE_WRITE_LINE_MEMBER(pit8253_out2_changed);

	DECLARE_FLOPPY_FORMATS(floppy_formats);

	DECLARE_WRITE_LINE_MEMBER(irq2_w);
	DECLARE_WRITE_LINE_MEMBER(irq5_w);
	DECLARE_WRITE_LINE_MEMBER(irq8_w);
	DECLARE_WRITE_LINE_MEMBER(irq9_w);
	DECLARE_WRITE_LINE_MEMBER(irq11_w);

	void ms0515(machine_config &config);
protected:
	virtual void machine_reset() override;

	void irq_encoder(int irq, int state);

	required_device<cpu_device> m_maincpu;
	required_device<ram_device> m_ram;
	required_device<kr1818vg93_device> m_fdc;
	required_device<floppy_image_device> m_floppy0;
	required_device<floppy_image_device> m_floppy1;
	required_device<i8251_device> m_i8251line;
	required_device<rs232_port_device> m_rs232;
	required_device<i8251_device> m_i8251kbd;
	required_device<ms7004_device> m_ms7004;
	required_device<pit8253_device> m_pit8253;
	required_device<speaker_sound_device> m_speaker;

private:
	uint8_t *m_video_ram;
	uint8_t m_sysrega, m_sysregc;
	uint16_t m_bankreg, m_haltreg;
	uint16_t m_irqs;
	int m_blink;
	floppy_image_device *m_floppy;
};

static ADDRESS_MAP_START(ms0515_mem, AS_PROGRAM, 16, ms0515_state)
	ADDRESS_MAP_UNMAP_HIGH
	AM_RANGE(0000000, 0017777) AM_RAMBANK("bank0") // RAM
	AM_RANGE(0020000, 0037777) AM_RAMBANK("bank1") // RAM
	AM_RANGE(0040000, 0057777) AM_RAMBANK("bank2") // RAM
	AM_RANGE(0060000, 0077777) AM_RAMBANK("bank3") // RAM
	AM_RANGE(0100000, 0117777) AM_RAMBANK("bank4") // RAM
	AM_RANGE(0120000, 0137777) AM_RAMBANK("bank5") // RAM
	AM_RANGE(0140000, 0157777) AM_RAMBANK("bank6") // RAM

	AM_RANGE(0160000, 0177377) AM_ROM AM_WRITENOP

	AM_RANGE(0177400, 0177437) AM_WRITE(ms0515_bank_w) // Register for RAM expansion

	AM_RANGE(0177440, 0177441) AM_DEVREAD8("i8251kbd", i8251_device, data_r, 0x00ff)
	AM_RANGE(0177442, 0177443) AM_DEVREADWRITE8("i8251kbd", i8251_device, status_r, control_w, 0x00ff)
	AM_RANGE(0177460, 0177461) AM_DEVWRITE8("i8251kbd", i8251_device, data_w, 0x00ff)
	AM_RANGE(0177462, 0177463) AM_DEVWRITE8("i8251kbd", i8251_device, control_w, 0x00ff)

	AM_RANGE(0177500, 0177507) AM_DEVREADWRITE8("pit8253", pit8253_device, read, write, 0x00ff)
	AM_RANGE(0177520, 0177527) AM_DEVWRITE8("pit8253", pit8253_device, write, 0x00ff)

	AM_RANGE(0177540, 0177547) AM_NOP
//  AM_RANGE(0177540, 0177541)
//  AM_RANGE(0177542, 0177543)
//  AM_RANGE(0177544, 0177545)  // i8255 for MS-7007 Keyboard
//  AM_RANGE(0177546, 0177547)

	AM_RANGE(0177600, 0177607) AM_DEVREADWRITE8("ppi8255_1", i8255_device, read, write, 0x00ff)

	AM_RANGE(0177640, 0177641) AM_DEVREADWRITE8("vg93", kr1818vg93_device, status_r, cmd_w, 0x00ff)
	AM_RANGE(0177642, 0177643) AM_DEVREADWRITE8("vg93", kr1818vg93_device, track_r, track_w, 0x00ff)
	AM_RANGE(0177644, 0177645) AM_DEVREADWRITE8("vg93", kr1818vg93_device, sector_r, sector_w, 0x00ff)
	AM_RANGE(0177646, 0177647) AM_DEVREADWRITE8("vg93", kr1818vg93_device, data_r, data_w, 0x00ff)

	AM_RANGE(0177700, 0177701) AM_DEVREAD8("i8251line", i8251_device, data_r, 0x00ff)
	AM_RANGE(0177702, 0177703) AM_DEVREADWRITE8("i8251line", i8251_device, status_r, control_w, 0x00ff)
	AM_RANGE(0177720, 0177721) AM_DEVWRITE8("i8251line", i8251_device, data_w, 0x00ff)
	AM_RANGE(0177722, 0177723) AM_DEVWRITE8("i8251line", i8251_device, control_w, 0x00ff)

	AM_RANGE(0177770, 0177771) AM_READWRITE(ms0515_halt_r, ms0515_halt_w) // read/write -- halt and system timer
ADDRESS_MAP_END

/*
 * (page 15-16)
 *
 * 6-0  RAM banking
 * 7    VRAM access enable
 * 8    vblank IRQ line (1 -- assert)
 * 9    timer IRQ enable (1 -- enable)
 * 11-10 VRAM banking
 * 12   parallel port STROBE signal
 * 13   parallel port ... signal
 * 14-15 unused
 */
WRITE16_MEMBER(ms0515_state::ms0515_bank_w)
{
	uint8_t *ram = m_ram->pointer();

	LOGBANK("Bank <- %04x & %04x (vblank %d timer %d)\n", data, mem_mask, BIT(data, 8), BIT(data, 9));

	if (BIT(data ^ m_bankreg, 8)) irq2_w(BIT(data, 8) ? ASSERT_LINE : CLEAR_LINE);

	m_bankreg = data;

	membank("bank0")->set_base(ram + 0000000 + BIT(data, 0) * 0160000);
	membank("bank1")->set_base(ram + 0020000 + BIT(data, 1) * 0160000);
	membank("bank2")->set_base(ram + 0040000 + BIT(data, 2) * 0160000);
	membank("bank3")->set_base(ram + 0060000 + BIT(data, 3) * 0160000);
	membank("bank4")->set_base(ram + 0100000 + BIT(data, 4) * 0160000);
	membank("bank5")->set_base(ram + 0120000 + BIT(data, 5) * 0160000);
	membank("bank6")->set_base(ram + 0140000 + BIT(data, 6) * 0160000);

	if (BIT(data, 7))
	{
		switch ((data >> 10) & 3)
		{
		case 0: // 000000 - 037777
			membank("bank0")->set_base(ram + 0000000 + 0340000);
			membank("bank1")->set_base(ram + 0020000 + 0340000);
			break;
		case 1: // 040000 - 077777
			membank("bank2")->set_base(ram + 0000000 + 0340000);
			membank("bank3")->set_base(ram + 0020000 + 0340000);
			break;
		case 2:
		case 3: // 100000 - 137777
			membank("bank4")->set_base(ram + 0000000 + 0340000);
			membank("bank5")->set_base(ram + 0020000 + 0340000);
			break;
		}
	}
}

READ16_MEMBER(ms0515_state::ms0515_halt_r)
{
	return m_haltreg;
}

WRITE16_MEMBER(ms0515_state::ms0515_halt_w)
{
	COMBINE_DATA(&m_haltreg);
}

/*
 * b7 -- ROM bank
 * b6 -- cassette data out
 * b5 -- LED VD16
 * b4 -- LED VD9
 * b3 -- floppy side select (?? 1 -- top)
 * b2 -- floppy motor (0 -- on)
 * b1-0 -- floppy drive select
 *
 * DZ0 = drive 0 side 0 (bottom)
 * DZ1 = drive 1 side 0 (bottom)
 * DZ2 = drive 0 side 1 (top)
 * DZ3 = drive 1 side 1 (top)
 *
 * MZ1 = drive 1 side 0-1
 */
WRITE8_MEMBER(ms0515_state::ms0515_porta_w)
{
	LOGSYSREG("Sysreg A <- %02x\n", data);

	output().set_value("led16", BIT(data, 5));
	output().set_value("led9", BIT(data, 4));

	switch (data & 3)
	{
	case 0:
		m_floppy = m_floppy0;
		break;

	case 1:
		m_floppy = m_floppy1;
		break;

	default:
		m_floppy = nullptr;
		break;
	}

	if (m_floppy)
	{
		m_fdc->set_floppy(m_floppy);
		m_floppy->ss_w(!BIT(data, 3));
		m_floppy->mon_w(BIT(data, 2));
	}
	else
	{
		m_floppy0->mon_w(1);
		m_floppy1->mon_w(1);
	}

	m_sysrega = data;
}

/*
 * b7 -- cassette data in
 * b6-5 -- reserved for IRPR-M (parallel) port
 * b4-3 -- DIP switches on video board, 00 -- 50 Hz, 01 -- 60 Hz, 1x -- 72 Hz
 * b2 -- floppy ready signal (0 -- ready)
 * b1 -- floppy drq (1 -- ready)
 * b0 -- floppy intrq (0 -- ready)
 */
READ8_MEMBER(ms0515_state::ms0515_portb_r)
{
	uint8_t data;

	data = m_fdc->intrq_r();
	data |= m_fdc->drq_r() << 1;

	if (m_floppy)
	{
		data |= !m_floppy->ready_r() << 2;
	}

	LOGSYSREG("Sysreg B == %02x\n", data);

	return data;
}


/*
 * b7 -- sound out gate
 * b6 -- sound out route to speaker
 * b5 -- sound ??
 * b4 -- LED VD17
 * b3 -- video resolution, 0: 320x200, 1: 640x200
 * b2-0 -- overscan color
 */
WRITE8_MEMBER(ms0515_state::ms0515_portc_w)
{
	LOGSYSREG("Sysreg C <- %02x\n", data);

	m_pit8253->write_gate2(BIT(data, 7));
	output().set_value("led17", BIT(data, 4));

	m_sysregc = data;
}

WRITE_LINE_MEMBER(ms0515_state::write_keyboard_clock)
{
	m_i8251kbd->write_txc(state);
	m_i8251kbd->write_rxc(state);
}

WRITE_LINE_MEMBER(ms0515_state::write_line_clock)
{
	m_i8251line->write_txc(state);
	m_i8251line->write_rxc(state);
}

WRITE_LINE_MEMBER(ms0515_state::pit8253_out2_changed)
{
	m_speaker->level_w(state);
}

void ms0515_state::machine_reset()
{
	uint8_t *ram = m_ram->pointer();
	ms0515_bank_w(machine().dummy_space(), 0, 0);

	m_video_ram = ram + 0000000 + 0340000;
	m_blink = 0;
	m_haltreg = 0;
	m_irqs = 0;
	m_floppy = nullptr;
}

/* Input ports */
static INPUT_PORTS_START( ms0515 )
	PORT_START("SA1")
	PORT_DIPNAME(0x03, 0x00, "Refresh rate") PORT_DIPLOCATION("E:3,4")
	PORT_DIPSETTING(0x00, "50 Hz")
	PORT_DIPSETTING(0x01, "60 Hz")
	PORT_DIPSETTING(0x02, "72 Hz")
INPUT_PORTS_END

FLOPPY_FORMATS_MEMBER( ms0515_state::floppy_formats )
	FLOPPY_MS0515_FORMAT
FLOPPY_FORMATS_END

static SLOT_INTERFACE_START( ms0515_floppies )
	SLOT_INTERFACE( "525qd", FLOPPY_525_QD )
SLOT_INTERFACE_END

uint32_t ms0515_state::screen_update_ms0515(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect)
{
	int y, x, b;
	int addr = 0;

	if (BIT(m_sysregc, 3))
	{
		uint8_t fg = m_sysregc & 7;
		uint8_t bg = fg ^ 7;
		for (y = 0; y < 200; y++)
		{
			int horpos = 0;
			for (x = 0; x < 40; x++)
			{
				uint16_t code = (m_video_ram[addr++] << 8);
				code += m_video_ram[addr++];
				for (b = 0; b < 16; b++)
				{
					// In lower res mode we will just double pixels
					bitmap.pix16(y, horpos++) = ((code >> (15 - b)) & 0x01) ? bg : fg;
				}
			}
		}
	}
	else
	{
		for (y = 0; y < 200; y++)
		{
			int horpos = 0;
			for (x = 0; x < 40; x++)
			{
				uint8_t code = m_video_ram[addr++];
				uint8_t attr = m_video_ram[addr++];
				uint8_t fg = (attr & 7) + BIT(attr, 6) * 8;
				uint8_t bg = ((attr >> 3) & 7) + BIT(attr, 6) * 8;
				if (BIT(attr, 7) && (m_blink == 20))
				{
					uint8_t tmp = fg;
					fg = bg;
					bg = tmp;
					m_blink = -1;
				}
				for (b = 0; b < 8; b++)
				{
					// In lower res mode we will just double pixels
					bitmap.pix16(y, horpos++) = ((code >> (7 - b)) & 0x01) ? fg : bg;
					bitmap.pix16(y, horpos++) = ((code >> (7 - b)) & 0x01) ? fg : bg;
				}
			}
		}
	}
	m_blink++;
	return 0;
}

WRITE_LINE_MEMBER(ms0515_state::screen_vblank)
{
//  irq2_w(state ? ASSERT_LINE : CLEAR_LINE);
	if (BIT(m_bankreg, 9))
		irq11_w(state ? ASSERT_LINE : CLEAR_LINE);
}

PALETTE_INIT_MEMBER(ms0515_state, ms0515)
{
	palette.set_pen_color(0, rgb_t(0, 0, 0));
	palette.set_pen_color(1, rgb_t(0, 0, 127));
	palette.set_pen_color(2, rgb_t(127, 0, 0));
	palette.set_pen_color(3, rgb_t(127, 0, 127));
	palette.set_pen_color(4, rgb_t(0, 127, 0));
	palette.set_pen_color(5, rgb_t(0, 127, 127));
	palette.set_pen_color(6, rgb_t(127, 127, 0));
	palette.set_pen_color(7, rgb_t(127, 127, 127));

	palette.set_pen_color(8, rgb_t(127, 127, 127));
	palette.set_pen_color(9, rgb_t(127, 127, 255));
	palette.set_pen_color(10, rgb_t(255, 127, 127));
	palette.set_pen_color(11, rgb_t(255, 127, 255));
	palette.set_pen_color(12, rgb_t(127, 255, 127));
	palette.set_pen_color(13, rgb_t(127, 255, 255));
	palette.set_pen_color(14, rgb_t(255, 255, 127));
	palette.set_pen_color(15, rgb_t(255, 255, 255));
}

// from vt240.cpp
void ms0515_state::irq_encoder(int irq, int state)
{
	if (state == ASSERT_LINE)
		m_irqs |= (1 << irq);
	else
		m_irqs &= ~(1 << irq);

	int i;
	for (i = 15; i > 0; i--)
	{
		if (m_irqs & (1 << i)) break;
	}
	m_maincpu->set_input_line(3, (i & 8) ? ASSERT_LINE : CLEAR_LINE);
	m_maincpu->set_input_line(2, (i & 4) ? ASSERT_LINE : CLEAR_LINE);
	m_maincpu->set_input_line(1, (i & 2) ? ASSERT_LINE : CLEAR_LINE);
	m_maincpu->set_input_line(0, (i & 1) ? ASSERT_LINE : CLEAR_LINE);
}

/*
 * interrupts (p. 21-22)
 *
 * IRQ  CPx  Pri Vec Device
 * ---  ---  --- --- ------
 * 11   LHLL 6   100 timer
 * 9    LHHL 6   110 serial RX
 * 8    LHHH 6   114 serial TX
 * 5    HLHL 5   130 7004 keyboard
 * 3    HHLL 4   060 7007 keyboard
 * 2    HHLH 4   064 vblank
 */

WRITE_LINE_MEMBER(ms0515_state::irq2_w)
{
	irq_encoder(2, state);
}

WRITE_LINE_MEMBER(ms0515_state::irq5_w)
{
	irq_encoder(5, state);
}

WRITE_LINE_MEMBER(ms0515_state::irq8_w)
{
	irq_encoder(8, state);
}

WRITE_LINE_MEMBER(ms0515_state::irq9_w)
{
	irq_encoder(9, state);
}

WRITE_LINE_MEMBER(ms0515_state::irq11_w)
{
	irq_encoder(11, state);
}

MACHINE_CONFIG_START(ms0515_state::ms0515)
	/* basic machine hardware */
	MCFG_CPU_ADD("maincpu", T11, XTAL(15'000'000) / 2) // actual CPU is T11 clone, KR1807VM1
	MCFG_T11_INITIAL_MODE(0xf2ff)
	MCFG_CPU_PROGRAM_MAP(ms0515_mem)

	/* video hardware -- 50 Hz refresh rate */
	MCFG_SCREEN_ADD("screen", RASTER)
	MCFG_SCREEN_RAW_PARAMS( XTAL(15'000'000), 958,0,640, 313,0,200 )
	MCFG_SCREEN_UPDATE_DRIVER(ms0515_state, screen_update_ms0515)
	MCFG_SCREEN_VBLANK_CALLBACK(WRITELINE(ms0515_state, screen_vblank))
	MCFG_SCREEN_PALETTE("palette")
	MCFG_DEFAULT_LAYOUT(layout_ms0515)

	MCFG_PALETTE_ADD("palette", 16)
	MCFG_PALETTE_INIT_OWNER(ms0515_state, ms0515)

	MCFG_DEVICE_ADD("vg93", KR1818VG93, 1000000)
	MCFG_FLOPPY_DRIVE_ADD("vg93:0", ms0515_floppies, "525qd", ms0515_state::floppy_formats)
	MCFG_FLOPPY_DRIVE_SOUND(true)
	MCFG_FLOPPY_DRIVE_ADD("vg93:1", ms0515_floppies, "525qd", ms0515_state::floppy_formats)
	MCFG_FLOPPY_DRIVE_SOUND(true)

	MCFG_DEVICE_ADD("ppi8255_1", I8255, 0)
	MCFG_I8255_OUT_PORTA_CB(WRITE8(ms0515_state, ms0515_porta_w))
	MCFG_I8255_IN_PORTB_CB(READ8(ms0515_state, ms0515_portb_r))
	MCFG_I8255_OUT_PORTC_CB(WRITE8(ms0515_state, ms0515_portc_w))

	// serial connection to printer
	MCFG_DEVICE_ADD( "i8251line", I8251, 0)
	MCFG_I8251_TXD_HANDLER(DEVWRITELINE("rs232", rs232_port_device, write_txd))
	MCFG_I8251_RXRDY_HANDLER(WRITELINE(ms0515_state, irq9_w))
	MCFG_I8251_TXRDY_HANDLER(WRITELINE(ms0515_state, irq8_w))

	MCFG_RS232_PORT_ADD("rs232", default_rs232_devices, nullptr)
	MCFG_RS232_RXD_HANDLER(DEVWRITELINE("i8251line", i8251_device, write_rxd))
	MCFG_RS232_CTS_HANDLER(DEVWRITELINE("i8251line", i8251_device, write_cts))
	MCFG_RS232_DSR_HANDLER(DEVWRITELINE("i8251line", i8251_device, write_dsr))

//  MCFG_DEVICE_ADD("line_clock", CLOCK, 4800*16) // 8251 is set to /16 on the clock input
//  MCFG_CLOCK_SIGNAL_HANDLER(WRITELINE(ms0515_state, write_line_clock))

	// serial connection to MS7004 keyboard
	MCFG_DEVICE_ADD("i8251kbd", I8251, 0)
	MCFG_I8251_RXRDY_HANDLER(WRITELINE(ms0515_state, irq5_w))
	MCFG_I8251_TXD_HANDLER(DEVWRITELINE("ms7004", ms7004_device, write_rxd))

	MCFG_DEVICE_ADD("ms7004", MS7004, 0)
	MCFG_MS7004_TX_HANDLER(DEVWRITELINE("i8251kbd", i8251_device, write_rxd))
	MCFG_MS7004_RTS_HANDLER(DEVWRITELINE("i8251kbd", i8251_device, write_cts))

	// baud rate is supposed to be 4800 but keyboard is slightly faster
	MCFG_DEVICE_ADD("keyboard_clock", CLOCK, 4960*16)
	MCFG_CLOCK_SIGNAL_HANDLER(WRITELINE(ms0515_state, write_keyboard_clock))

	MCFG_DEVICE_ADD("pit8253", PIT8253, 0)
	MCFG_PIT8253_CLK0(XTAL(2'000'000))
//  MCFG_PIT8253_OUT0_HANDLER(WRITELINE(ms0515_state, write_keyboard_clock))
	MCFG_PIT8253_CLK1(XTAL(2'000'000))
	MCFG_PIT8253_OUT0_HANDLER(WRITELINE(ms0515_state, write_line_clock))
	MCFG_PIT8253_CLK2(XTAL(2'000'000))
	MCFG_PIT8253_OUT2_HANDLER(WRITELINE(ms0515_state, pit8253_out2_changed))

	MCFG_SPEAKER_STANDARD_MONO("mono")
	MCFG_SOUND_ADD("speaker", SPEAKER_SOUND, 0)
	MCFG_SOUND_ROUTE(ALL_OUTPUTS, "mono", 0.45)

	/* internal ram */
	MCFG_RAM_ADD(RAM_TAG)
	MCFG_RAM_DEFAULT_SIZE("128K")
MACHINE_CONFIG_END

/* ROM definition */
ROM_START( ms0515 )
	ROM_REGION( 0x10000, "maincpu", ROMREGION_ERASEFF )
	ROM_DEFAULT_BIOS( "b" )
	ROM_SYSTEM_BIOS( 0, "a", "Version A" )
	ROMX_LOAD( "7004l.bin", 0xc000, 0x2000, CRC(b08b3b73) SHA1(c12fd4672598cdf499656dcbb4118d787769d589), ROM_SKIP(1) | ROM_BIOS(1))
	ROMX_LOAD( "7004h.bin", 0xc001, 0x2000, CRC(515dcf99) SHA1(edd34300fd642c89ce321321e1b12493cd16b7a5), ROM_SKIP(1) | ROM_BIOS(1))
	ROM_SYSTEM_BIOS( 1, "b", "Version B" )
	ROMX_LOAD( "0515L.rf4", 0xc000, 0x2000, CRC(85b608a4) SHA1(5b1bb0586d8f7a8a21de69200b08e0b28a318999), ROM_SKIP(1) | ROM_BIOS(2))
	ROMX_LOAD( "0515H.rf4", 0xc001, 0x2000, CRC(e3ff6da9) SHA1(3febccf40abc2e3ca7db3f6f3884be117722dd8b), ROM_SKIP(1) | ROM_BIOS(2))
ROM_END

/* Driver */

//    YEAR  NAME    PARENT  COMPAT   MACHINE    INPUT    STATE         INIT  COMPANY        FULLNAME   FLAGS
COMP( 1990, ms0515, 0,      0,       ms0515,    ms0515,  ms0515_state, 0,    "Elektronika", "MS 0515", 0 )
