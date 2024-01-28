#include "peanut_gb.h"

/**
 * Internal function used to read bytes.
 */
uint8_t __gb_read(struct gb_s *gb, const uint_fast16_t addr)
{
	switch(addr >> 12)
	{
	case 0x0:

	/* TODO: BIOS support. */
	case 0x1:
	case 0x2:
	case 0x3:
		return gb->gb_rom_read(gb, addr);

	case 0x4:
	case 0x5:
	case 0x6:
	case 0x7:
		if(gb->mbc == 1 && gb->cart_mode_select)
			return gb->gb_rom_read(gb,
					       addr + ((gb->selected_rom_bank & 0x1F) - 1) * ROM_BANK_SIZE);
		else
			return gb->gb_rom_read(gb, addr + (gb->selected_rom_bank - 1) * ROM_BANK_SIZE);

	case 0x8:
	case 0x9:
		return gb->vram[addr - VRAM_ADDR];

	case 0xA:
	case 0xB:
		if(gb->cart_ram && gb->enable_cart_ram)
		{
			if(gb->mbc == 3 && gb->cart_ram_bank >= 0x08)
				return gb->cart_rtc[gb->cart_ram_bank - 0x08];
			else if((gb->cart_mode_select || gb->mbc != 1) &&
					gb->cart_ram_bank < gb->num_ram_banks)
			{
				return gb->gb_cart_ram_read(gb, addr - CART_RAM_ADDR +
							    (gb->cart_ram_bank * CRAM_BANK_SIZE));
			}
			else
				return gb->gb_cart_ram_read(gb, addr - CART_RAM_ADDR);
		}

		return 0xFF;

	case 0xC:
		return gb->wram[addr - WRAM_0_ADDR];

	case 0xD:
		return gb->wram[addr - WRAM_1_ADDR + WRAM_BANK_SIZE];

	case 0xE:
		return gb->wram[addr - ECHO_ADDR];

	case 0xF:
		if(addr < OAM_ADDR)
			return gb->wram[addr - ECHO_ADDR];

		if(addr < UNUSED_ADDR)
			return gb->oam[addr - OAM_ADDR];

		/* Unusable memory area. Reading from this area returns 0.*/
		if(addr < IO_ADDR)
			return 0xFF;

		/* HRAM */
		if(HRAM_ADDR <= addr && addr < INTR_EN_ADDR)
			return gb->hram[addr - IO_ADDR];

		/* APU registers. */
		if((addr >= 0xFF10) && (addr <= 0xFF3F))
		{
			if(gb->direct.sound_enabled) {
				return audio_read(addr);
			}
			else {
				static const uint8_t ortab[] = {
					0x80, 0x3f, 0x00, 0xff, 0xbf,
					0xff, 0x3f, 0x00, 0xff, 0xbf,
					0x7f, 0xff, 0x9f, 0xff, 0xbf,
					0xff, 0xff, 0x00, 0x00, 0xbf,
					0x00, 0x00, 0x70,
					0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
					0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
				};
				return gb->hram[addr - IO_ADDR] | ortab[addr - IO_ADDR];
			}
		}

		/* IO and Interrupts. */
		switch(addr & 0xFF)
		{
		/* IO Registers */
		case 0x00:
			return 0xC0 | gb->gb_reg.P1;

		case 0x01:
			return gb->gb_reg.SB;

		case 0x02:
			return gb->gb_reg.SC;

		/* Timer Registers */
		case 0x04:
			return gb->gb_reg.DIV;

		case 0x05:
			return gb->gb_reg.TIMA;

		case 0x06:
			return gb->gb_reg.TMA;

		case 0x07:
			return gb->gb_reg.TAC;

		/* Interrupt Flag Register */
		case 0x0F:
			return gb->gb_reg.IF;

		/* LCD Registers */
		case 0x40:
			return gb->gb_reg.LCDC;

		case 0x41:
			return (gb->gb_reg.STAT & STAT_USER_BITS) |
			       (gb->gb_reg.LCDC & LCDC_ENABLE ? gb->lcd_mode : LCD_VBLANK);

		case 0x42:
			return gb->gb_reg.SCY;

		case 0x43:
			return gb->gb_reg.SCX;

		case 0x44:
			return gb->gb_reg.LY;

		case 0x45:
			return gb->gb_reg.LYC;

		/* DMA Register */
		case 0x46:
			return gb->gb_reg.DMA;

		/* DMG Palette Registers */
		case 0x47:
			return gb->gb_reg.BGP;

		case 0x48:
			return gb->gb_reg.OBP0;

		case 0x49:
			return gb->gb_reg.OBP1;

		/* Window Position Registers */
		case 0x4A:
			return gb->gb_reg.WY;

		case 0x4B:
			return gb->gb_reg.WX;

		/* Interrupt Enable Register */
		case 0xFF:
			return gb->gb_reg.IE;

		/* Unused registers return 1 */
		default:
			return 0xFF;
		}
	}

	(gb->gb_error)(gb, GB_INVALID_READ, addr);
	return 0xFF;
}

/**
 * Internal function used to write bytes.
 */
void __gb_write(struct gb_s *gb, const uint_fast16_t addr, const uint8_t val)
{
	switch(addr >> 12)
	{
	case 0x0:
	case 0x1:
		if(gb->mbc == 2 && addr & 0x10)
			return;
		else if(gb->mbc > 0 && gb->cart_ram)
			gb->enable_cart_ram = ((val & 0x0F) == 0x0A);

		return;

	case 0x2:
		if(gb->mbc == 5)
		{
			gb->selected_rom_bank = (gb->selected_rom_bank & 0x100) | val;
			gb->selected_rom_bank =
				gb->selected_rom_bank & gb->num_rom_banks_mask;
			return;
		}

	/* Intentional fall through. */

	case 0x3:
		if(gb->mbc == 1)
		{
			//selected_rom_bank = val & 0x7;
			gb->selected_rom_bank = (val & 0x1F) | (gb->selected_rom_bank & 0x60);

			if((gb->selected_rom_bank & 0x1F) == 0x00)
				gb->selected_rom_bank++;
		}
		else if(gb->mbc == 2 && addr & 0x10)
		{
			gb->selected_rom_bank = val & 0x0F;

			if(!gb->selected_rom_bank)
				gb->selected_rom_bank++;
		}
		else if(gb->mbc == 3)
		{
			gb->selected_rom_bank = val & 0x7F;

			if(!gb->selected_rom_bank)
				gb->selected_rom_bank++;
		}
		else if(gb->mbc == 5)
			gb->selected_rom_bank = (val & 0x01) << 8 | (gb->selected_rom_bank & 0xFF);

		gb->selected_rom_bank = gb->selected_rom_bank & gb->num_rom_banks_mask;
		return;

	case 0x4:
	case 0x5:
		if(gb->mbc == 1)
		{
			gb->cart_ram_bank = (val & 3);
			gb->selected_rom_bank = ((val & 3) << 5) | (gb->selected_rom_bank & 0x1F);
			gb->selected_rom_bank = gb->selected_rom_bank & gb->num_rom_banks_mask;
		}
		else if(gb->mbc == 3)
			gb->cart_ram_bank = val;
		else if(gb->mbc == 5)
			gb->cart_ram_bank = (val & 0x0F);

		return;

	case 0x6:
	case 0x7:
		gb->cart_mode_select = (val & 1);
		return;

	case 0x8:
	case 0x9:
		gb->vram[addr - VRAM_ADDR] = val;
		return;

	case 0xA:
	case 0xB:
		if(gb->cart_ram && gb->enable_cart_ram)
		{
			if(gb->mbc == 3 && gb->cart_ram_bank >= 0x08)
				gb->cart_rtc[gb->cart_ram_bank - 0x08] = val;
			else if(gb->cart_mode_select &&
					gb->cart_ram_bank < gb->num_ram_banks)
			{
				gb->gb_cart_ram_write(gb,
						      addr - CART_RAM_ADDR + (gb->cart_ram_bank * CRAM_BANK_SIZE), val);
			}
			else if(gb->num_ram_banks)
				gb->gb_cart_ram_write(gb, addr - CART_RAM_ADDR, val);
		}

		return;

	case 0xC:
		gb->wram[addr - WRAM_0_ADDR] = val;
		return;

	case 0xD:
		gb->wram[addr - WRAM_1_ADDR + WRAM_BANK_SIZE] = val;
		return;

	case 0xE:
		gb->wram[addr - ECHO_ADDR] = val;
		return;

	case 0xF:
		if(addr < OAM_ADDR)
		{
			gb->wram[addr - ECHO_ADDR] = val;
			return;
		}

		if(addr < UNUSED_ADDR)
		{
			gb->oam[addr - OAM_ADDR] = val;
			return;
		}

		/* Unusable memory area. */
		if(addr < IO_ADDR)
			return;

		if(HRAM_ADDR <= addr && addr < INTR_EN_ADDR)
		{
			gb->hram[addr - IO_ADDR] = val;
			return;
		}

		if((addr >= 0xFF10) && (addr <= 0xFF3F))
		{
			if(gb->direct.sound_enabled) {
				audio_write(addr, val);
			}
			else {
				gb->hram[addr - IO_ADDR] = val;
			}
			return;
		}

		/* IO and Interrupts. */
		switch(addr & 0xFF)
		{
		/* Joypad */
		case 0x00:
			/* Only bits 5 and 4 are R/W.
			 * The lower bits are overwritten later, and the two most
			 * significant bits are unused. */
			gb->gb_reg.P1 = val;

			/* Direction keys selected */
			if((gb->gb_reg.P1 & 0b010000) == 0)
				gb->gb_reg.P1 |= (gb->direct.joypad >> 4);
			/* Button keys selected */
			else
				gb->gb_reg.P1 |= (gb->direct.joypad & 0x0F);

			return;

		/* Serial */
		case 0x01:
			gb->gb_reg.SB = val;
			return;

		case 0x02:
			gb->gb_reg.SC = val;
			return;

		/* Timer Registers */
		case 0x04:
			gb->gb_reg.DIV = 0x00;
			return;

		case 0x05:
			gb->gb_reg.TIMA = val;
			return;

		case 0x06:
			gb->gb_reg.TMA = val;
			return;

		case 0x07:
			gb->gb_reg.TAC = val;
			return;

		/* Interrupt Flag Register */
		case 0x0F:
			gb->gb_reg.IF = (val | 0b11100000);
			return;

		/* LCD Registers */
		case 0x40:
			if(((gb->gb_reg.LCDC & LCDC_ENABLE) == 0) &&
				(val & LCDC_ENABLE))
			{
				gb->counter.lcd_count = 0;
				gb->lcd_blank = 1;
			}

			gb->gb_reg.LCDC = val;

			/* LY fixed to 0 when LCD turned off. */
			if((gb->gb_reg.LCDC & LCDC_ENABLE) == 0)
			{
				/* Do not turn off LCD outside of VBLANK. This may
				 * happen due to poor timing in this emulator. */
				if(gb->lcd_mode != LCD_VBLANK)
				{
					gb->gb_reg.LCDC |= LCDC_ENABLE;
					return;
				}

				gb->gb_reg.STAT = (gb->gb_reg.STAT & ~0x03) | LCD_VBLANK;
				gb->gb_reg.LY = 0;
				gb->counter.lcd_count = 0;
			}

			return;

		case 0x41:
			gb->gb_reg.STAT = (val & 0b01111000);
			return;

		case 0x42:
			gb->gb_reg.SCY = val;
			return;

		case 0x43:
			gb->gb_reg.SCX = val;
			return;

		/* LY (0xFF44) is read only. */
		case 0x45:
			gb->gb_reg.LYC = val;
			return;

		/* DMA Register */
		case 0x46:
			gb->gb_reg.DMA = (val % 0xF1);

			for(uint8_t i = 0; i < OAM_SIZE; i++)
				gb->oam[i] = __gb_read(gb, (gb->gb_reg.DMA << 8) + i);

			return;

		/* DMG Palette Registers */
		case 0x47:
			gb->gb_reg.BGP = val;
			gb->display.bg_palette[0] = (gb->gb_reg.BGP & 0x03);
			gb->display.bg_palette[1] = (gb->gb_reg.BGP >> 2) & 0x03;
			gb->display.bg_palette[2] = (gb->gb_reg.BGP >> 4) & 0x03;
			gb->display.bg_palette[3] = (gb->gb_reg.BGP >> 6) & 0x03;
			return;

		case 0x48:
			gb->gb_reg.OBP0 = val;
			gb->display.sp_palette[0] = (gb->gb_reg.OBP0 & 0x03);
			gb->display.sp_palette[1] = (gb->gb_reg.OBP0 >> 2) & 0x03;
			gb->display.sp_palette[2] = (gb->gb_reg.OBP0 >> 4) & 0x03;
			gb->display.sp_palette[3] = (gb->gb_reg.OBP0 >> 6) & 0x03;
			return;

		case 0x49:
			gb->gb_reg.OBP1 = val;
			gb->display.sp_palette[4] = (gb->gb_reg.OBP1 & 0x03);
			gb->display.sp_palette[5] = (gb->gb_reg.OBP1 >> 2) & 0x03;
			gb->display.sp_palette[6] = (gb->gb_reg.OBP1 >> 4) & 0x03;
			gb->display.sp_palette[7] = (gb->gb_reg.OBP1 >> 6) & 0x03;
			return;

		/* Window Position Registers */
		case 0x4A:
			gb->gb_reg.WY = val;
			return;

		case 0x4B:
			gb->gb_reg.WX = val;
			return;

		/* Turn off boot ROM */
		case 0x50:
			gb->gb_bios_enable = 0;
			return;

		/* Interrupt Enable Register */
		case 0xFF:
			gb->gb_reg.IE = val;
			return;
		}
	}

	(gb->gb_error)(gb, GB_INVALID_WRITE, addr);
}

/**
 * Tick the internal RTC by one second.
 * This was taken from SameBoy, which is released under MIT Licence.
 */
void gb_tick_rtc(struct gb_s *gb)
{
	/* is timer running? */
	if((gb->cart_rtc[4] & 0x40) == 0)
	{
		if(++gb->rtc_bits.sec == 60)
		{
			gb->rtc_bits.sec = 0;

			if(++gb->rtc_bits.min == 60)
			{
				gb->rtc_bits.min = 0;

				if(++gb->rtc_bits.hour == 24)
				{
					gb->rtc_bits.hour = 0;

					if(++gb->rtc_bits.yday == 0)
					{
						if(gb->rtc_bits.high & 1)  /* Bit 8 of days*/
						{
							gb->rtc_bits.high |= 0x80; /* Overflow bit */
						}

						gb->rtc_bits.high ^= 1;
					}
				}
			}
		}
	}
}

/**
 * Set initial values in RTC.
 * Should be called after gb_init().
 */
void gb_set_rtc(struct gb_s *gb, const struct tm * const time)
{
	gb->cart_rtc[0] = time->tm_sec;
	gb->cart_rtc[1] = time->tm_min;
	gb->cart_rtc[2] = time->tm_hour;
	gb->cart_rtc[3] = time->tm_yday & 0xFF; /* Low 8 bits of day counter. */
	gb->cart_rtc[4] = time->tm_yday >> 8; /* High 1 bit of day counter. */
}

#if ENABLE_LCD
struct sprite_data {
	uint8_t sprite_number;
	uint8_t x;
};

#if PEANUT_GB_HIGH_LCD_ACCURACY
static int compare_sprites(const void *in1, const void *in2)
{
	const struct sprite_data *sd1 = in1, *sd2 = in2;
	int x_res = (int)sd1->x - (int)sd2->x;
	if(x_res != 0)
		return x_res;

	return (int)sd1->sprite_number - (int)sd2->sprite_number;
}
#endif

void __gb_draw_line(struct gb_s *gb)
{
	if(gb->direct.frame_skip && !gb->display.frame_skip_count)
		return;

	/* If interlaced mode is activated, check if we need to draw the current
	 * line. */
	if(gb->direct.interlace)
	{
		if((gb->display.interlace_count == 0
				&& (gb->gb_reg.LY & 1) == 0)
				|| (gb->display.interlace_count == 1
				    && (gb->gb_reg.LY & 1) == 1))
		{
			/* Compensate for missing window draw if required. */
			if(gb->gb_reg.LCDC & LCDC_WINDOW_ENABLE
					&& gb->gb_reg.LY >= gb->display.WY
					&& gb->gb_reg.WX <= 166)
				gb->display.window_clear++;

			return;
		}
	}
	
	uint8_t* front_pixels = &gb->display.front_fb[gb->gb_reg.LY][0];
	uint8_t* back_pixels = &gb->display.back_fb[gb->gb_reg.LY][0];
	uint8_t* pixels = gb->display.back_fb_enabled ? back_pixels : front_pixels;
	uint8_t pixel = 0;

	/* If background is enabled, draw it. */
	if(gb->gb_reg.LCDC & LCDC_BG_ENABLE)
	{
		/* Calculate current background line to draw. Constant because
		 * this function draws only this one line each time it is
		 * called. */
		const uint8_t bg_y = gb->gb_reg.LY + gb->gb_reg.SCY;

		/* Get selected background map address for first tile
		 * corresponding to current line.
		 * 0x20 (32) is the width of a background tile, and the bit
		 * shift is to calculate the address. */
		const uint16_t bg_map =
			((gb->gb_reg.LCDC & LCDC_BG_MAP) ?
			 VRAM_BMAP_2 : VRAM_BMAP_1)
			+ (bg_y >> 3) * 0x20;

		/* The displays (what the player sees) X coordinate, drawn right
		 * to left. */
		uint8_t disp_x = LCD_WIDTH - 1;

		/* The X coordinate to begin drawing the background at. */
		uint8_t bg_x = disp_x + gb->gb_reg.SCX;

		/* Get tile index for current background tile. */
		uint8_t idx = gb->vram[bg_map + (bg_x >> 3)];
		/* Y coordinate of tile pixel to draw. */
		const uint8_t py = (bg_y & 0x07);
		/* X coordinate of tile pixel to draw. */
		uint8_t px = 7 - (bg_x & 0x07);

		uint16_t tile;

		/* Select addressing mode. */
		if(gb->gb_reg.LCDC & LCDC_TILE_SELECT)
			tile = VRAM_TILES_1 + idx * 0x10;
		else
			tile = VRAM_TILES_2 + ((idx + 0x80) % 0x100) * 0x10;

		tile += 2 * py;

		/* fetch first tile */
		uint8_t t1 = gb->vram[tile] >> px;
		uint8_t t2 = gb->vram[tile + 1] >> px;

		for(; disp_x != 0xFF; disp_x--)
		{
			if(px == 8)
			{
				/* fetch next tile */
				px = 0;
				bg_x = disp_x + gb->gb_reg.SCX;
				idx = gb->vram[bg_map + (bg_x >> 3)];

				if(gb->gb_reg.LCDC & LCDC_TILE_SELECT)
					tile = VRAM_TILES_1 + idx * 0x10;
				else
					tile = VRAM_TILES_2 + ((idx + 0x80) % 0x100) * 0x10;

				tile += 2 * py;
				t1 = gb->vram[tile];
				t2 = gb->vram[tile + 1];
			}

			/* copy background */
			uint8_t c = (t1 & 0x1) | ((t2 & 0x1) << 1);
			pixel = gb->display.bg_palette[c];
			pixel |= LCD_PALETTE_BG;
			pixels[disp_x] = pixel;
			t1 = t1 >> 1;
			t2 = t2 >> 1;
			px++;
		}
	}

	/* draw window */
	if(gb->gb_reg.LCDC & LCDC_WINDOW_ENABLE
			&& gb->gb_reg.LY >= gb->display.WY
			&& gb->gb_reg.WX <= 166)
	{
		/* Calculate Window Map Address. */
		uint16_t win_line = (gb->gb_reg.LCDC & LCDC_WINDOW_MAP) ?
				    VRAM_BMAP_2 : VRAM_BMAP_1;
		win_line += (gb->display.window_clear >> 3) * 0x20;

		uint8_t disp_x = LCD_WIDTH - 1;
		uint8_t win_x = disp_x - gb->gb_reg.WX + 7;

		// look up tile
		uint8_t py = gb->display.window_clear & 0x07;
		uint8_t px = 7 - (win_x & 0x07);
		uint8_t idx = gb->vram[win_line + (win_x >> 3)];

		uint16_t tile;

		if(gb->gb_reg.LCDC & LCDC_TILE_SELECT)
			tile = VRAM_TILES_1 + idx * 0x10;
		else
			tile = VRAM_TILES_2 + ((idx + 0x80) % 0x100) * 0x10;

		tile += 2 * py;

		// fetch first tile
		uint8_t t1 = gb->vram[tile] >> px;
		uint8_t t2 = gb->vram[tile + 1] >> px;

		// loop & copy window
		uint8_t end = (gb->gb_reg.WX < 7 ? 0 : gb->gb_reg.WX - 7) - 1;

		for(; disp_x != end; disp_x--)
		{
			if(px == 8)
			{
				// fetch next tile
				px = 0;
				win_x = disp_x - gb->gb_reg.WX + 7;
				idx = gb->vram[win_line + (win_x >> 3)];

				if(gb->gb_reg.LCDC & LCDC_TILE_SELECT)
					tile = VRAM_TILES_1 + idx * 0x10;
				else
					tile = VRAM_TILES_2 + ((idx + 0x80) % 0x100) * 0x10;

				tile += 2 * py;
				t1 = gb->vram[tile];
				t2 = gb->vram[tile + 1];
			}

			// copy window
			uint8_t c = (t1 & 0x1) | ((t2 & 0x1) << 1);
			pixel = gb->display.bg_palette[c];
			pixel |= LCD_PALETTE_BG;
			pixels[disp_x] = pixel;
			t1 = t1 >> 1;
			t2 = t2 >> 1;
			px++;
		}

		gb->display.window_clear++; // advance window line
	}

	// draw sprites
	if(gb->gb_reg.LCDC & LCDC_OBJ_ENABLE)
	{
#if PEANUT_GB_HIGH_LCD_ACCURACY
		uint8_t number_of_sprites = 0;
		struct sprite_data sprites_to_render[NUM_SPRITES];

		/* Record number of sprites on the line being rendered, limited
		 * to the maximum number sprites that the Game Boy is able to
		 * render on each line (10 sprites). */
		for(uint8_t sprite_number = 0;
				sprite_number < PEANUT_GB_ARRAYSIZE(sprites_to_render);
				sprite_number++)
		{
			/* Sprite Y position. */
			uint8_t OY = gb->oam[4 * sprite_number + 0];
			/* Sprite X position. */
			uint8_t OX = gb->oam[4 * sprite_number + 1];

			/* If sprite isn't on this line, continue. */
			if (gb->gb_reg.LY +
				(gb->gb_reg.LCDC & LCDC_OBJ_SIZE ? 0 : 8) >= OY
					|| gb->gb_reg.LY + 16 < OY)
				continue;


			sprites_to_render[number_of_sprites].sprite_number = sprite_number;
			sprites_to_render[number_of_sprites].x = OX;
			number_of_sprites++;
		}

		/* If maximum number of sprites reached, prioritise X
		 * coordinate and object location in OAM. */
		qsort(&sprites_to_render[0], number_of_sprites,
				sizeof(sprites_to_render[0]), compare_sprites);
		if(number_of_sprites > MAX_SPRITES_LINE)
			number_of_sprites = MAX_SPRITES_LINE;
#endif

		/* Render each sprite, from low priority to high priority. */
#if PEANUT_GB_HIGH_LCD_ACCURACY
		/* Render the top ten prioritised sprites on this scanline. */
		for(uint8_t sprite_number = number_of_sprites - 1;
				sprite_number != 0xFF;
				sprite_number--)
		{
			uint8_t s = sprites_to_render[sprite_number].sprite_number;
#else
		for (uint8_t sprite_number = NUM_SPRITES - 1;
			sprite_number != 0xFF;
			sprite_number--)
		{
			uint8_t s = sprite_number;
#endif
			/* Sprite Y position. */
			uint8_t OY = gb->oam[4 * s + 0];
			/* Sprite X position. */
			uint8_t OX = gb->oam[4 * s + 1];
			/* Sprite Tile/Pattern Number. */
			uint8_t OT = gb->oam[4 * s + 2]
				     & (gb->gb_reg.LCDC & LCDC_OBJ_SIZE ? 0xFE : 0xFF);
			/* Additional attributes. */
			uint8_t OF = gb->oam[4 * s + 3];

#if !PEANUT_GB_HIGH_LCD_ACCURACY
			/* If sprite isn't on this line, continue. */
			if(gb->gb_reg.LY +
					(gb->gb_reg.LCDC & LCDC_OBJ_SIZE ? 0 : 8) >= OY ||
					gb->gb_reg.LY + 16 < OY)
				continue;
#endif

			/* Continue if sprite not visible. */
			if(OX == 0 || OX >= 168)
				continue;

			// y flip
			uint8_t py = gb->gb_reg.LY - OY + 16;

			if(OF & OBJ_FLIP_Y)
				py = (gb->gb_reg.LCDC & LCDC_OBJ_SIZE ? 15 : 7) - py;

			// fetch the tile
			uint8_t t1 = gb->vram[VRAM_TILES_1 + OT * 0x10 + 2 * py];
			uint8_t t2 = gb->vram[VRAM_TILES_1 + OT * 0x10 + 2 * py + 1];

			// handle x flip
			uint8_t dir, start, end, shift;

			if(OF & OBJ_FLIP_X)
			{
				dir = 1;
				start = (OX < 8 ? 0 : OX - 8);
				end = MIN(OX, LCD_WIDTH);
				shift = 8 - OX + start;
			}
			else
			{
				dir = -1;
				start = MIN(OX, LCD_WIDTH) - 1;
				end = (OX < 8 ? 0 : OX - 8) - 1;
				shift = OX - (start + 1);
			}

			// copy tile
			t1 >>= shift;
			t2 >>= shift;

			for(uint8_t disp_x = start; disp_x != end; disp_x += dir)
			{
				uint8_t c = (t1 & 0x1) | ((t2 & 0x1) << 1);
				// check transparency / sprite overlap / background overlap
#if 0

				if(c
						//	&& OX <= fx[disp_x]
						&& !((OF & OBJ_PRIORITY)
						     && ((pixels[disp_x] & 0x3)
							 && fx[disp_x] == 0xFE)))
#else
				if(c && !(OF & OBJ_PRIORITY
						&& pixels[disp_x] & 0x3))
#endif
				{
					/* Set pixel colour. */
					pixel = (OF & OBJ_PALETTE)
							 ? gb->display.sp_palette[c + 4]
							 : gb->display.sp_palette[c];
					/* Set pixel palette (OBJ0 or OBJ1). */
					pixel |= (OF & OBJ_PALETTE);
					/* Deselect BG palette. */
					pixel &= ~LCD_PALETTE_BG;
					pixels[disp_x] = pixel;
				}

				t1 = t1 >> 1;
				t2 = t2 >> 1;
			}
		}
	}

	/* If LCD not initialised by front-end, don't render anything. */
	if(memcmp(front_pixels, back_pixels, LCD_WIDTH) != 0) {
		gb->display.changed_rows[gb->gb_reg.LY] = 1;
		gb->display.changed_row_count++;
	}
}
#endif

void __gb_step(struct gb_s *gb)
{
    /* Handle interrupts */
	if((gb->gb_ime || gb->gb_halt) &&
			(gb->gb_reg.IF & gb->gb_reg.IE & ANY_INTR))
	{
		gb->gb_halt = 0;

		if(gb->gb_ime)
		{
			/* Disable interrupts */
			gb->gb_ime = 0;

			/* Push Program Counter */
			__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
			__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);

			/* Call interrupt handler if required. */
			if(gb->gb_reg.IF & gb->gb_reg.IE & VBLANK_INTR)
			{
				gb->cpu_reg.pc = VBLANK_INTR_ADDR;
				gb->gb_reg.IF ^= VBLANK_INTR;
			}
			else if(gb->gb_reg.IF & gb->gb_reg.IE & LCDC_INTR)
			{
				gb->cpu_reg.pc = LCDC_INTR_ADDR;
				gb->gb_reg.IF ^= LCDC_INTR;
			}
			else if(gb->gb_reg.IF & gb->gb_reg.IE & TIMER_INTR)
			{
				gb->cpu_reg.pc = TIMER_INTR_ADDR;
				gb->gb_reg.IF ^= TIMER_INTR;
			}
			else if(gb->gb_reg.IF & gb->gb_reg.IE & SERIAL_INTR)
			{
				gb->cpu_reg.pc = SERIAL_INTR_ADDR;
				gb->gb_reg.IF ^= SERIAL_INTR;
			}
			else if(gb->gb_reg.IF & gb->gb_reg.IE & CONTROL_INTR)
			{
				gb->cpu_reg.pc = CONTROL_INTR_ADDR;
				gb->gb_reg.IF ^= CONTROL_INTR;
			}
		}
	}
    
	uint8_t inst_cycles = __gb_step_chunked(&gb->cpu_reg);
	
	/* DIV register timing */
	gb->counter.div_count += inst_cycles;

	if(gb->counter.div_count >= DIV_CYCLES)
	{
		gb->gb_reg.DIV++;
		gb->counter.div_count -= DIV_CYCLES;
	}

	/* Check serial transmission. */
	if(gb->gb_reg.SC & SERIAL_SC_TX_START)
	{
		/* If new transfer, call TX function. */
		if(gb->counter.serial_count == 0 && gb->gb_serial_tx != NULL)
			(gb->gb_serial_tx)(gb, gb->gb_reg.SB);

		gb->counter.serial_count += inst_cycles;

		/* If it's time to receive byte, call RX function. */
		if(gb->counter.serial_count >= SERIAL_CYCLES)
		{
			/* If RX can be done, do it. */
			/* If RX failed, do not change SB if using external
			 * clock, or set to 0xFF if using internal clock. */
			uint8_t rx;

			if(gb->gb_serial_rx != NULL &&
				(gb->gb_serial_rx(gb, &rx) ==
					 GB_SERIAL_RX_SUCCESS))
			{
				gb->gb_reg.SB = rx;

				/* Inform game of serial TX/RX completion. */
				gb->gb_reg.SC &= 0x01;
				gb->gb_reg.IF |= SERIAL_INTR;
			}
			else if(gb->gb_reg.SC & SERIAL_SC_CLOCK_SRC)
			{
				/* If using internal clock, and console is not
				 * attached to any external peripheral, shifted
				 * bits are replaced with logic 1. */
				gb->gb_reg.SB = 0xFF;

				/* Inform game of serial TX/RX completion. */
				gb->gb_reg.SC &= 0x01;
				gb->gb_reg.IF |= SERIAL_INTR;
			}
			else
			{
				/* If using external clock, and console is not
				 * attached to any external peripheral, bits are
				 * not shifted, so SB is not modified. */
			}

			gb->counter.serial_count = 0;
		}
	}

	/* TIMA register timing */
	/* TODO: Change tac_enable to struct of TAC timer control bits. */
	if(gb->gb_reg.tac_enable)
	{
		static const uint_fast16_t TAC_CYCLES[4] = {1024, 16, 64, 256};

		gb->counter.tima_count += inst_cycles;

		while(gb->counter.tima_count >= TAC_CYCLES[gb->gb_reg.tac_rate])
		{
			gb->counter.tima_count -= TAC_CYCLES[gb->gb_reg.tac_rate];

			if(++gb->gb_reg.TIMA == 0)
			{
				gb->gb_reg.IF |= TIMER_INTR;
				/* On overflow, set TMA to TIMA. */
				gb->gb_reg.TIMA = gb->gb_reg.TMA;
			}
		}
	}

	/* TODO Check behaviour of LCD during LCD power off state. */
	/* If LCD is off, don't update LCD state. */
	if((gb->gb_reg.LCDC & LCDC_ENABLE) == 0)
		return;

	/* LCD Timing */
	gb->counter.lcd_count += inst_cycles;

	/* New Scanline */
	if(gb->counter.lcd_count > LCD_LINE_CYCLES)
	{
		gb->counter.lcd_count -= LCD_LINE_CYCLES;

		/* LYC Update */
		if(gb->gb_reg.LY == gb->gb_reg.LYC)
		{
			gb->gb_reg.STAT |= STAT_LYC_COINC;

			if(gb->gb_reg.STAT & STAT_LYC_INTR)
				gb->gb_reg.IF |= LCDC_INTR;
		}
		else
			gb->gb_reg.STAT &= 0xFB;

		/* Next line */
		gb->gb_reg.LY = (gb->gb_reg.LY + 1) % LCD_VERT_LINES;

		/* VBLANK Start */
		if(gb->gb_reg.LY == LCD_HEIGHT)
		{
			gb->lcd_mode = LCD_VBLANK;
			gb->gb_frame = 1;
			gb->gb_reg.IF |= VBLANK_INTR;
			gb->lcd_blank = 0;

			if(gb->gb_reg.STAT & STAT_MODE_1_INTR)
				gb->gb_reg.IF |= LCDC_INTR;

#if ENABLE_LCD

			/* If frame skip is activated, check if we need to draw
			 * the frame or skip it. */
			if(gb->direct.frame_skip)
			{
				gb->display.frame_skip_count =
					!gb->display.frame_skip_count;
			}

			/* If interlaced is activated, change which lines get
			 * updated. Also, only update lines on frames that are
			 * actually drawn when frame skip is enabled. */
			if(gb->direct.interlace &&
					(!gb->direct.frame_skip ||
					 gb->display.frame_skip_count))
			{
				gb->display.interlace_count =
					!gb->display.interlace_count;
			}
			
			if(!gb->direct.frame_skip ||
				 !gb->display.frame_skip_count)
			{
					gb->display.back_fb_enabled =
							!gb->display.back_fb_enabled;
			}

#endif
		}
		/* Normal Line */
		else if(gb->gb_reg.LY < LCD_HEIGHT)
		{
			if(gb->gb_reg.LY == 0)
			{
				/* Clear Screen */
				gb->display.WY = gb->gb_reg.WY;
				gb->display.window_clear = 0;
			}

			gb->lcd_mode = LCD_HBLANK;

			if(gb->gb_reg.STAT & STAT_MODE_0_INTR)
				gb->gb_reg.IF |= LCDC_INTR;
		}
	}
	/* OAM access */
	else if(gb->lcd_mode == LCD_HBLANK
			&& gb->counter.lcd_count >= LCD_MODE_2_CYCLES)
	{
		gb->lcd_mode = LCD_SEARCH_OAM;

		if(gb->gb_reg.STAT & STAT_MODE_2_INTR)
			gb->gb_reg.IF |= LCDC_INTR;
	}
	/* Update LCD */
	else if(gb->lcd_mode == LCD_SEARCH_OAM
			&& gb->counter.lcd_count >= LCD_MODE_3_CYCLES)
	{
		gb->lcd_mode = LCD_TRANSFER;
#if ENABLE_LCD
		if(!gb->lcd_blank)
			__gb_draw_line(gb);
#endif
	}
}

void gb_run_frame(struct gb_s *gb)
{
    peanut_exec_gb = gb;
	gb->gb_frame = 0;
	if(gb->display.changed_row_count > 0) {
		memset(gb->display.changed_rows, 0, sizeof(gb->display.changed_rows));
	}
	gb->display.changed_row_count = 0;
	while(!gb->gb_frame)
		__gb_step(gb);
    peanut_exec_gb = NULL;
}

/**
 * Gets the size of the save file required for the ROM.
 */
uint_fast32_t gb_get_save_size(struct gb_s *gb)
{
	const uint_fast16_t ram_size_location = 0x0149;
	const uint_fast32_t ram_sizes[] =
	{
		0x00, 0x800, 0x2000, 0x8000, 0x20000
	};
	uint8_t ram_size = gb->gb_rom_read(gb, ram_size_location);
	return ram_sizes[ram_size];
}



uint8_t gb_colour_hash(struct gb_s *gb)
{
#define ROM_TITLE_START_ADDR	0x0134
#define ROM_TITLE_END_ADDR	0x0143

	uint8_t x = 0;

	for(uint16_t i = ROM_TITLE_START_ADDR; i <= ROM_TITLE_END_ADDR; i++)
		x += gb->gb_rom_read(gb, i);

	return x;
}

/**
 * Resets the context, and initialises startup values.
 */
void gb_reset(struct gb_s *gb)
{
	gb->gb_halt = 0;
	gb->gb_ime = 1;
	gb->gb_bios_enable = 0;
	gb->lcd_mode = LCD_HBLANK;

	/* Initialise MBC values. */
	gb->selected_rom_bank = 1;
	gb->cart_ram_bank = 0;
	gb->enable_cart_ram = 0;
	gb->cart_mode_select = 0;

	/* Initialise CPU registers as though a DMG. */
	__cpu_init(gb);

	gb->counter.lcd_count = 0;
	gb->counter.div_count = 0;
	gb->counter.tima_count = 0;
	gb->counter.serial_count = 0;

	gb->gb_reg.TIMA      = 0x00;
	gb->gb_reg.TMA       = 0x00;
	gb->gb_reg.TAC       = 0xF8;
	gb->gb_reg.DIV       = 0xAC;

	gb->gb_reg.IF        = 0xE1;

	gb->gb_reg.LCDC      = 0x91;
	gb->gb_reg.SCY       = 0x00;
	gb->gb_reg.SCX       = 0x00;
	gb->gb_reg.LYC       = 0x00;

	/* Appease valgrind for invalid reads and unconditional jumps. */
	gb->gb_reg.SC = 0x7E;
	gb->gb_reg.STAT = 0;
	gb->gb_reg.LY = 0;

	__gb_write(gb, 0xFF47, 0xFC);    // BGP
	__gb_write(gb, 0xFF48, 0xFF);    // OBJP0
	__gb_write(gb, 0xFF49, 0x0F);    // OBJP1
	gb->gb_reg.WY        = 0x00;
	gb->gb_reg.WX        = 0x00;
	gb->gb_reg.IE        = 0x00;

	gb->direct.joypad = 0xFF;
	gb->gb_reg.P1 = 0xCF;

	memset(gb->vram, 0x00, VRAM_SIZE);
}

/**
 * Initialise the emulator context. gb_reset() is also called to initialise
 * the CPU.
 */
enum gb_init_error_e gb_init(
	struct gb_s *gb,
	uint8_t (*gb_rom_read)(struct gb_s*, const uint_fast32_t),
	uint8_t (*gb_cart_ram_read)(struct gb_s*, const uint_fast32_t),
	void (*gb_cart_ram_write)(struct gb_s*, const uint_fast32_t, const uint8_t),
	void (*gb_error)(struct gb_s*, const enum gb_error_e, const uint16_t),
	void *priv
)
{
	const uint16_t mbc_location = 0x0147;
	const uint16_t bank_count_location = 0x0148;
	const uint16_t ram_size_location = 0x0149;
	/**
	 * Table for cartridge type (MBC). -1 if invalid.
	 * TODO: MMM01 is untested.
	 * TODO: MBC6 is untested.
	 * TODO: MBC7 is unsupported.
	 * TODO: POCKET CAMERA is unsupported.
	 * TODO: BANDAI TAMA5 is unsupported.
	 * TODO: HuC3 is unsupported.
	 * TODO: HuC1 is unsupported.
	 **/
	const uint8_t cart_mbc[] =
	{
		0, 1, 1, 1, -1, 2, 2, -1, 0, 0, -1, 0, 0, 0, -1, 3,
		3, 3, 3, 3, -1, -1, -1, -1, -1, 5, 5, 5, 5, 5, 5, -1
	};
	const uint8_t cart_ram[] =
	{
		0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0,
		1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0
	};
	const uint16_t num_rom_banks_mask[] =
	{
		2, 4, 8, 16, 32, 64, 128, 256, 512
	};
	const uint8_t num_ram_banks[] = { 0, 1, 1, 4, 16, 8 };

	gb->gb_rom_read = gb_rom_read;
	gb->gb_cart_ram_read = gb_cart_ram_read;
	gb->gb_cart_ram_write = gb_cart_ram_write;
	gb->gb_error = gb_error;
	gb->direct.priv = priv;

	/* Initialise serial transfer function to NULL. If the front-end does
	 * not provide serial support, Peanut-GB will emulate no cable connected
	 * automatically. */
	gb->gb_serial_tx = NULL;
	gb->gb_serial_rx = NULL;

	/* Check valid ROM using checksum value. */
	{
		uint8_t x = 0;

		for(uint16_t i = 0x0134; i <= 0x014C; i++)
			x = x - gb->gb_rom_read(gb, i) - 1;

		if(x != gb->gb_rom_read(gb, ROM_HEADER_CHECKSUM_LOC))
			return GB_INIT_INVALID_CHECKSUM;
	}

	/* Check if cartridge type is supported, and set MBC type. */
	{
		const uint8_t mbc_value = gb->gb_rom_read(gb, mbc_location);

		if(mbc_value > sizeof(cart_mbc) - 1 ||
				(gb->mbc = cart_mbc[mbc_value]) == 255u)
			return GB_INIT_CARTRIDGE_UNSUPPORTED;
	}

	gb->cart_ram = cart_ram[gb->gb_rom_read(gb, mbc_location)];
	gb->num_rom_banks_mask = num_rom_banks_mask[gb->gb_rom_read(gb, bank_count_location)] - 1;
	gb->num_ram_banks = num_ram_banks[gb->gb_rom_read(gb, ram_size_location)];

	gb->lcd_blank = 0;
	gb->display.lcd_line_changed = NULL;

	gb_reset(gb);

	return GB_INIT_NO_ERROR;
}

/**
 * Returns the title of ROM.
 *
 * \param gb		Initialised context.
 * \param title_str	Allocated string at least 16 characters.
 * \returns		Pointer to start of string, null terminated.
 */
const char* gb_get_rom_name(struct gb_s* gb, char *title_str)
{
	uint_fast16_t title_loc = 0x134;
	/* End of title may be 0x13E for newer games. */
	const uint_fast16_t title_end = 0x143;
	const char* title_start = title_str;

	for(; title_loc <= title_end; title_loc++)
	{
		const char title_char = gb->gb_rom_read(gb, title_loc);

		if(title_char >= ' ' && title_char <= '_')
		{
			*title_str = title_char;
			title_str++;
		}
		else
			break;
	}

	*title_str = '\0';
	return title_start;
}

#if ENABLE_LCD
void gb_init_lcd(
    struct gb_s *gb,
    void (*lcd_line_changed)(struct gb_s *gb,
    const uint_fast8_t line)
)
{
	gb->display.lcd_line_changed = lcd_line_changed;

	gb->direct.interlace = 0;
	gb->display.interlace_count = 0;
	gb->direct.frame_skip = 0;
	gb->display.frame_skip_count = 0;

	gb->display.window_clear = 0;
	gb->display.WY = 0;
	
	gb->display.back_fb_enabled = 0;
	
	memset(gb->display.front_fb, 0, sizeof(gb->display.front_fb));
	memset(gb->display.back_fb, 0, sizeof(gb->display.back_fb));

	return;
}
#endif