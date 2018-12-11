// license:BSD-3-Clause
// copyright-holders:David Shah
/*****************************************************************************

  MAME/MESS VTxx APU CORE

 *****************************************************************************/

#include "nes_vt_apu.h"
#include "emu.h"

DEFINE_DEVICE_TYPE(NES_VT_APU, nesapu_vt_device, "nesapu_vt", "VTxx APU")
DEFINE_DEVICE_TYPE(NES_VT_APU_SLAVE, nesapu_vt_slave_device, "nesapu_vt_slave", "VTxx APU (slave)")

nesapu_vt_device::nesapu_vt_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: nesapu_device(mconfig, tag, NES_VT_APU, owner, clock),
	  m_xop2(*this, "nesapu_vt_slave"),
		m_rom_read_cb(*this)
{

}

void nesapu_vt_device::device_start()
{
	nesapu_device::device_start();
	if(!m_xop2->started())
		throw device_missing_dependencies();
	for (int i = 0; i < 2; i++)
	{
		save_item(NAME(m_apu_vt.vt33_pcm[i].regs));
		save_item(NAME(m_apu_vt.vt33_pcm[i].address));
		save_item(NAME(m_apu_vt.vt33_pcm[i].volume));
		save_item(NAME(m_apu_vt.vt33_pcm[i].enabled));
		save_item(NAME(m_apu_vt.vt33_pcm[i].playing));
	}

	save_item(NAME(m_apu_vt.vt03_pcm.regs));
	save_item(NAME(m_apu_vt.vt03_pcm.address));
	save_item(NAME(m_apu_vt.vt03_pcm.length));
	save_item(NAME(m_apu_vt.vt03_pcm.output_vol));
	save_item(NAME(m_apu_vt.vt03_pcm.enabled));
	save_item(NAME(m_apu_vt.vt03_pcm.vol));

	save_item(NAME(m_apu_vt.extra_regs));
	save_item(NAME(m_apu_vt.vt3x_sel_channel));
	save_item(NAME(m_apu_vt.use_vt03_pcm));
	save_item(NAME(m_apu_vt.use_vt3x_pcm));

}

nesapu_vt_slave_device::nesapu_vt_slave_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: nesapu_device(mconfig, tag, NES_VT_APU_SLAVE, owner, clock)
{

}

void nesapu_vt_slave_device::sound_stream_update(sound_stream &stream, stream_sample_t **inputs, stream_sample_t **outputs, int samples)
{
	//TODO: any tweaks needed here:
	nesapu_device::sound_stream_update(stream, inputs, outputs, samples);
}

void nesapu_vt_device::sound_stream_update(sound_stream &stream, stream_sample_t **inputs, stream_sample_t **outputs, int samples)
{
	std::unique_ptr<stream_sample_t[]> pbuf = std::make_unique<stream_sample_t[]>(samples);
	std::unique_ptr<stream_sample_t[]> sbuf = std::make_unique<stream_sample_t[]>(samples);

	stream_sample_t *pptr = pbuf.get(), *sptr = sbuf.get();

	// Dual legacy sound generators
	nesapu_device::sound_stream_update(stream, inputs, &pptr, samples);
	m_xop2->sound_stream_update(stream, inputs, &sptr, samples);

	int accum;
	memset( outputs[0], 0, samples*sizeof(*outputs[0]) );

	for(int i = 0; i < samples; i++)
	{
		accum = 0;
		accum += pbuf[i] >> 8;
		accum += sbuf[i] >> 8; //TODO: mixing between generators?
		accum += vt03_pcm(&m_apu_vt.vt03_pcm);
		accum += vt3x_pcm(&(m_apu_vt.vt33_pcm[0]));
		accum += vt3x_pcm(&(m_apu_vt.vt33_pcm[1]));

		/* 8-bit clamps */
		if (accum > 127)
			accum = 127;
		else if (accum < -128)
			accum = -128;

		*(outputs[0]++)=accum<<8;

	}
}

void nesapu_vt_device::vt_apu_write(uint8_t address, uint8_t data) {

	if(address == 0x35 && !m_apu_vt.use_vt3x_pcm)
	{
		//When VT3x PCM disabled, 4035 controls XOP2
		m_xop2->write(0x15, data & 0x0F);
	} else if(address >= 0x30 && address <= 0x36) {
		m_apu_vt.extra_regs[address - 0x30] = data;
	} else if (address == 0x15) {
		uint8_t nes_val = data;
		if(m_apu_vt.use_vt03_pcm || m_apu_vt.use_vt3x_pcm)
		{
			nes_val &= 0x0F;
		}
		nesapu_device::write(0x15, nes_val);
	}
	vt_apu_regwrite(address, data);
}

uint8_t nesapu_vt_device::vt_apu_read(uint8_t address) {
	if(address >= 0x30 && address <= 0x36)
	{
		return m_apu_vt.extra_regs[address - 0x30];
	} else if (address >= 0x10 && address <= 0x13) {
		return m_apu_vt.vt03_pcm.regs[address - 0x10];
	} else if (address == 0x15) {
		uint8_t base = nesapu_device::read(0x15);
		if(m_apu_vt.use_vt03_pcm)
		{
			base &= 0x4F;
			base |= (m_apu_vt.vt03_pcm.enabled << 4);
			// TODO: IRQ status
		}
		return base;
	}
	return 0x00;
}



u8 nesapu_vt_device::read(offs_t address)
{
	if (address <= 0x0F) {
		return nesapu_device::read(address);
	} else if (address >= 0x10 && address <= 0x13) {
		if(!m_apu_vt.use_vt03_pcm)
			return nesapu_device::read(address);
		else
			return vt_apu_read(address);
	} else if (address >= 0x20 && address <= 0x2F) {
		return m_xop2->read(address - 0x20);
	} else if (address == 0x15 || (address >= 0x30 && address <= 0x36)) {
		return vt_apu_read(address);
	} else {
		logerror("nesapu_vt read %04x\n", 0x4000 + address);
		return 0x00;
	}
}

void nesapu_vt_device::write(offs_t address, u8 value)
{
	if (address <= 0x0F) {
		nesapu_device::write(address, value);
	} else if (address >= 0x10 && address <= 0x13) {
		//PCM registers affect both new and legacy APU
		if (m_apu_vt.use_vt03_pcm || m_apu_vt.use_vt3x_pcm)
			vt_apu_write(address, value);
		else
			nesapu_device::write(address, value);
	} else if (address >= 0x20 && address <= 0x2F) {
		m_xop2->write(address - 0x20, value);
	} else if (address == 0x15 || (address >= 0x30 && address <= 0x36)) {
		vt_apu_write(address, value);
	} else {
		logerror("nesapu_vt write %04x %02x\n", 0x4000 + address, value);
	}
}
