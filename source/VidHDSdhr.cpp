#include "stdafx.h"
#include "VidHDSdhr.h"


enum ShdrCmd_e {
	SDHR_CMD_UPLOAD_DATA = 1,
	SDHR_CMD_DEFINE_TILESET = 2,
	SDHR_CMD_DEFINE_TILESET_IMMEDIATE = 3,
	SDHR_CMD_DEFINE_PALETTE = 4,
	SDHR_CMD_DEFINE_PALETTE_IMMEDIATE = 5,
	SDHR_CMD_DEFINE_WINDOW = 6,
	SDHR_CMD_UPDATE_WINDOW_SET_ALL = 7,
	SDHR_CMD_UPDATE_WINDOW_SINGLE_TILESET = 8,
	SDHR_CMD_UPDATE_WINDOW_SINGLE_PALETTE = 9,
	SDHR_CMD_UPDATE_WINDOW_SINGLE_BOTH = 10,
	SDHR_CMD_UPDATE_WINDOW_SHIFT_TILES = 11,
	SDHR_CMD_UPDATE_WINDOW_SET_WINDOW_POSITION = 12,
	SDHR_CMD_UPDATE_WINDOW_ADJUST_WINDOW_VIEW = 13,
	SDHR_CMD_UPDATE_WINDOW_SET_BITMASKS = 14,
	SDHR_CMD_UPDATE_WINDOW_ENABLE = 15,
	SDHR_CMD_READY = 16,
};
#pragma pack(push)
#pragma pack(1)	
struct UploadDataCmd {
	uint8_t dest_addr_med;
	uint8_t dest_addr_high;
	uint8_t source_addr_med;
	uint8_t num_256b_pages;
};

struct DefineTilesetCmd {
	uint8_t tileset_index;
	uint8_t depth;
	uint8_t num_entries;
	uint8_t xdim;
	uint8_t ydim;
	uint8_t data_low;
	uint8_t data_med;
	uint8_t data_high;
};

struct DefineTilesetImmediateCmd {
	uint8_t tileset_index;
	uint8_t depth;
	uint8_t num_entries;
	uint8_t xdim;
	uint8_t ydim;
	uint8_t data[];
};

struct DefinePaletteCmd {
	uint8_t palette_index;
	uint8_t data_low;
	uint8_t data_med;
	uint8_t data_high;
};

struct DefinePaletteImmediateCmd {
	uint8_t palette_index;
	uint8_t depth;
	uint8_t data[];
};

struct DefineWindowCmd {
	int8_t window_index;
	uint16_t screen_xcount;
	uint16_t screen_ycount;
	int16_t screen_xbegin;
	int16_t screen_xend;
	uint16_t tile_xbegin;
	uint16_t tile_ybegin;
	uint16_t tile_xdim;
	uint16_t tile_ydim;
	uint16_t tile_xcount;
	uint16_t tile_ycount;
};

struct UpdateWindowSetAllCmd {
	int8_t window_index;
	uint16_t tile_xbegin;
	uint16_t tile_ybegin;
	uint16_t tile_xcount;
	uint16_t tile_ycount;
	uint8_t data[];
};

struct UpdateWindowSingleTilesetCmd {
	int8_t window_index;
	uint16_t tile_xbegin;
	uint16_t tile_ybegin;
	uint16_t tile_xcount;
	uint16_t tile_ycount;
	uint8_t tileset_index;
	uint8_t data[];
};

struct UpdateWindowSinglePaletteCmd {
	int8_t window_index;
	uint16_t tile_xbegin;
	uint16_t tile_ybegin;
	uint16_t tile_xcount;
	uint16_t tile_ycount;
	uint8_t palette_index;
	uint8_t data[];
};

struct UpdateWindowSingleBothCmd {
	int8_t window_index;
	uint16_t tile_xbegin;
	uint16_t tile_ybegin;
	uint16_t tile_xcount;
	uint16_t tile_ycount;
	uint8_t tileset_index;
	uint8_t palette_index;
	uint8_t data[];
};

struct UpdateWindowSetBitmasksCmd {
	int8_t window_index;
	uint16_t tile_xcount;
	uint16_t tile_ycount;
	uint8_t data[];
};

struct UpdateWindowShiftTilesCmd {
	int8_t window_index;
	int8_t x_dir; // +1 shifts tiles right by 1, negative shifts tiles left by 1, zero no change
	int8_t y_dir; // +1 shifts tiles down by 1, negative shifts tiles up by 1, zero no change
};

struct UpdateWindowSetWindowPositionCmd {
	int8_t window_index;
	int16_t screen_xbegin;
	int16_t screen_ybegin;
};

struct UpdateWindowAdjustWindowViewCommand {
	int8_t window_index;
	uint16_t tile_xbegin;
	uint16_t tile_ybegin;
};

struct UpdateWindowEnableCmd {
	int8_t window_index;
	bool enabled;
};

#pragma pack(pop)

void VidHDSdhr::SDHRWriteByte(BYTE value) {
	command_buffer.push_back((uint8_t)value);
}


void VidHDSdhr::CommandError(const char* err) {
	strcpy(error_str, err);
	error_flag = true;
}

bool VidHDSdhr::CheckCommandLength(BYTE* p, BYTE* e, size_t sz) {
	size_t command_length = e - p;
	if (command_length < sz) {
		CommandError("Insufficient buffer space");
		return false;
	}
	return true;
}

bgra_t VidHDSdhr::GetPixel(uint16_t vert, uint16_t horz) {
	uint64_t pixel_offset = (uint64_t)vert * screen_xcount + horz;
	bgra_t rgb = { 0 };
	rgb.r = screen_red[pixel_offset] * 16;
	rgb.g = screen_green[pixel_offset] * 16;
	rgb.b = screen_blue[pixel_offset] * 16;
	rgb.a = ALPHA;
	return rgb;
}

void VidHDSdhr::DefineTileset(uint8_t tileset_index, uint8_t depth, uint8_t num_entries, uint8_t xdim, uint8_t ydim, 
	                          uint64_t source_data_size,uint8_t* source_p) {
	uint64_t store_data_size = (uint64_t)xdim * ydim * num_entries;
	TilesetRecord* r = tileset_records + tileset_index;
	if (r->tile_data) {
		free(r->tile_data);
	}
	memset(r, 0, sizeof(TilesetRecord));
	r->depth = depth;
	r->xdim = xdim;
	r->ydim = ydim;
	r->num_entries = num_entries;
	r->tile_data = (uint8_t*)malloc(store_data_size);
	memset(r->tile_data, store_data_size, 0);

	uint8_t* dest_p = r->tile_data;
	// regardless of palette bit depth, we expand all pixels to individual bytes 
	// for ease of rendering.  In the hardware, this lets the palette lookups
	// get vectorized.  Here, we are doing it to ease the rendering implementation,
	// being able to treat them all the same.
	uint8_t mask;
	uint8_t mask_shift;
	uint8_t pixel_stride;
	if (depth == 4) {
		mask = 0xf0;
		mask_shift = 4;
		pixel_stride = 2;
	}
	else if (depth == 2) {
		mask = 0xc0;
		mask_shift = 6;
		pixel_stride = 4;
	}
	else {
		mask = 0x80;
		mask_shift = 7;
		pixel_stride = 8;
	}
	uint64_t line_offset = r->ydim * r->xdim + r->xdim;
	for (uint64_t i = 0; i < source_data_size; ++i) {
		uint8_t pixel_byte = source_p[i];
		for (uint8_t j = 0; j < pixel_stride; ++j) {
			uint8_t pixel = pixel_byte << (mask_shift * j);
			pixel &= mask;
			pixel >>= mask_shift;
			*dest_p++ = pixel;
		}
	}
}


bool VidHDSdhr::ProcessCommands() {
	if (error_flag) {
		return false;
	}
	if (command_buffer.empty()) {
		//nothing to do
		return true;
	}
	BYTE* begin = &command_buffer[0];
	BYTE* end = begin + command_buffer.size();
	BYTE* p = begin;

	while (begin < end) {
		if (!CheckCommandLength(p, end, 2)) {
			return false;
		}
		uint16_t message_length = *((uint16_t*)p);
		if (!CheckCommandLength(p, end, message_length)) return false;
		p += 2;
		BYTE cmd = *p++;
		switch (cmd) {
		case SDHR_CMD_UPLOAD_DATA: {
			if (!CheckCommandLength(p, end, sizeof(UploadDataCmd))) return false;
			UploadDataCmd* cmd = (UploadDataCmd*)p;
			if (cmd->num_256b_pages > 256 - cmd->source_addr_med) {
				CommandError("UploadData attempting to load past top of memory");
				return false;
			}
			uint64_t dest_offset = DataOffset(0, cmd->dest_addr_med, cmd->dest_addr_high);
			uint64_t data_size = (uint64_t) 256 * cmd->num_256b_pages;
			if (!DataSizeCheck(dest_offset, data_size)) {
				return false;
			}
			memcpy(MemGetMainPtr((uint16_t)cmd->source_addr_med * 256), uploaded_data_region + dest_offset, data_size);
		} break;
		case SDHR_CMD_DEFINE_TILESET: {
			if (!CheckCommandLength(p, end, sizeof(DefineTilesetCmd))) return false;
			DefineTilesetCmd* cmd = (DefineTilesetCmd*)p;
			switch (cmd->depth) {
			case 1:
			case 2:
			case 4:
				break;
			default:
				CommandError("invalid tileset depth");
				return false;
			}
			if (cmd->xdim % 4 || cmd->ydim % 4) {
				CommandError("tile dimensions must be multiple of 4");
				return false;
			}
			uint64_t load_data_size;
			load_data_size = (uint64_t)cmd->xdim * cmd->ydim * cmd->num_entries * cmd->depth / 8;
			uint64_t data_region_offset = DataOffset(cmd->data_low, cmd->data_med, cmd->data_high);
			if (!DataSizeCheck(data_region_offset, load_data_size)) {
				return false;
			}
			uint8_t* source_p = uploaded_data_region + data_region_offset;
			DefineTileset(cmd->tileset_index, cmd->depth, cmd->num_entries, cmd->xdim, cmd->ydim, load_data_size, source_p);
		} break;
		case SDHR_CMD_DEFINE_TILESET_IMMEDIATE: {
			if (!CheckCommandLength(p, end, sizeof(DefineTilesetImmediateCmd))) return false;
			DefineTilesetImmediateCmd* cmd = (DefineTilesetImmediateCmd*)p;
			switch (cmd->depth) {
			case 1:
			case 2:
			case 4:
				break;
			default:
				CommandError("invalid tileset depth");
				return false;
			}
			if (cmd->xdim % 4 || cmd->ydim % 4) {
				CommandError("tile dimensions must be multiple of 4");
				return false;
			}
			uint64_t data_size;
			switch (cmd->depth) {
			case 1:
				data_size = (uint64_t)cmd->xdim * cmd->ydim * cmd->num_entries / 8; break;
			case 2:
				data_size = (uint64_t)cmd->xdim * cmd->ydim * cmd->num_entries / 4; break;
			case 4:
				data_size = (uint64_t)cmd->xdim* cmd->ydim* cmd->num_entries / 4; break;
			default:
				CommandError("invalid tileset depth");
				return false;
			}
			if (sizeof(DefineTilesetImmediateCmd) + data_size != message_length) {
				CommandError("TilesetImmediate data size mismatch");
				return false;
			}
			DefineTileset(cmd->tileset_index, cmd->depth, cmd->num_entries, cmd->xdim, cmd->ydim, data_size, cmd->data);
		} break;
		case SDHR_CMD_DEFINE_PALETTE: {
			if (!CheckCommandLength(p, end, sizeof(DefinePaletteCmd))) return false;
			DefinePaletteCmd* cmd = (DefinePaletteCmd*)p;
			uint64_t data_region_offset = DataOffset(cmd->data_low, cmd->data_med, cmd->data_high);
			uint64_t data_size = 48;  //16 entries * 3 bytes RGB
			if (!DataSizeCheck(data_region_offset, data_size)) {
				return false;
			}
			PaletteRecord* r = palette_records + cmd->palette_index;
			uint8_t* source_p = uploaded_data_region + data_region_offset;
			memcpy(r->red, source_p, 16);
			memcpy(r->green, source_p + 16, 16);
			memcpy(r->blue, source_p + 32, 16);
			// entries only get 4 bits of color resolution
			for (uint64_t i = 0; i < 16; ++i) {
				r->red[i] &= 0x0f;
				r->green[i] &= 0x0f;
				r->blue[i] &= 0x0f;
			}
		} break;
		case SDHR_CMD_DEFINE_PALETTE_IMMEDIATE: {
			if (!CheckCommandLength(p, end, sizeof(DefinePaletteImmediateCmd))) return false;
			DefinePaletteImmediateCmd* cmd = (DefinePaletteImmediateCmd*)p;
			uint64_t data_size;
			switch (cmd->depth) {
			case 1:
				data_size = 6; break;
			case 2:
				data_size = 12; break;
			case 4:
				data_size = 48; break;
			default:
				CommandError("invalid tileset depth");
				return false;
			}
			if (sizeof(DefinePaletteImmediateCmd) + data_size != message_length) {
				CommandError("PaletteImmediate data size mismatch");
				return false;
			}
			PaletteRecord* r = palette_records + cmd->palette_index;
			for (uint64_t i = 0; i < 16; ++i) {
				if (i * 3 > data_size) {
					r->red[i] = 0;
					r->green[i] = 0;
					r->blue[i] = 0;
				}
				else {
					// entries only get 4 bits of color resolution
					r->red[i] = cmd->data[i * 3] & 0x0f;
					r->green[i] = cmd->data[i * 3 + 1] & 0x0f;
					r->blue[i] = cmd->data[i * 3 + 2] & 0x0f;
				}
			}
		} break;
		case SDHR_CMD_DEFINE_WINDOW: {
			if (!CheckCommandLength(p, end, sizeof(DefineWindowCmd))) return false;
			DefineWindowCmd* cmd = (DefineWindowCmd*)p;
			Window* r = windows + cmd->window_index;
			if (r->screen_xcount > 640) {
				CommandError("Window exceeds max x resolution");
				return false;
			}
			if (r->screen_ycount > 360) {
				CommandError("Window exceeds max y resolution");
				return false;
			}
			if (r->tile_xdim % 4 || r->tile_ydim % 4) {
				CommandError("tile dimensions must be multiple of 4");
				return false;
			}
			r->enabled = false;
			r->screen_xcount = cmd->screen_xcount;
			r->screen_ycount = cmd->screen_ycount;
			r->screen_xbegin = cmd->screen_xbegin;
			r->screen_xbegin = cmd->screen_xend;
			r->tile_xbegin = cmd->tile_xbegin;
			r->tile_ybegin = cmd->tile_ybegin;
			r->tile_xdim = cmd->tile_xdim;
			r->tile_ydim = cmd->tile_ydim;
			r->tile_xcount = cmd->tile_xcount;
			if (r->bitmask_tilesets) {
				free(r->bitmask_tilesets);
				r->bitmask_tilesets = NULL;
			}
			if (r->bitmask_tile_indexes) {
				free(r->bitmask_tile_indexes);
				r->bitmask_tilesets = NULL;
			}

		} break;
		case SDHR_CMD_UPDATE_WINDOW_SET_ALL: {
			if (!CheckCommandLength(p, end, sizeof(UpdateWindowSetAllCmd))) return false;
			UpdateWindowSetAllCmd* cmd = (UpdateWindowSetAllCmd*)p;
			Window* r = windows + cmd->window_index;
			if ((uint64_t)cmd->tile_xbegin + cmd->tile_xcount > r->tile_xcount ||
				(uint64_t)cmd->tile_ybegin + cmd->tile_ycount > r->tile_ycount) {
				CommandError("tile update region exceeds tile dimensions");
				return false;
			}
			// full tile specification: tileset, index, and palette
			uint64_t data_size = (uint64_t)cmd->tile_xcount * cmd->tile_ycount * 3;
			if (data_size + sizeof(UpdateWindowSetAllCmd) != message_length) {
				CommandError("UpdateWindowSetAll data size mismatch");
				return false;
			}
			uint8_t* sp = cmd->data;
			for (uint64_t tile_y = 0; tile_y < cmd->tile_ycount; ++tile_y) {
				uint64_t line_offset = (uint64_t)(cmd->tile_ybegin + tile_y) * r->tile_xcount + cmd->tile_xbegin;
				for (uint64_t tile_x = 0; tile_x < cmd->tile_xcount; ++tile_x) {
					uint8_t tileset_index = *sp++;
					uint8_t tile_index = *sp++;
					uint8_t palette_index = *sp++;
					if (tileset_records[tileset_index].xdim != r->tile_xdim ||
						tileset_records[tileset_index].ydim != r->tile_ydim ||
						tileset_records[tileset_index].num_entries <= tile_index) {
						CommandError("invalid tile specification");
						return false;
					}
					r->tilesets[line_offset + tile_x] = tileset_index;
					r->tile_indexes[line_offset + tile_x] = tile_index;
					r->tile_palettes[line_offset + tile_x] = palette_index;
				}
			}
		} break;
		case SDHR_CMD_UPDATE_WINDOW_SINGLE_TILESET: {
			if (!CheckCommandLength(p, end, sizeof(UpdateWindowSingleTilesetCmd))) return false;
			UpdateWindowSingleTilesetCmd* cmd = (UpdateWindowSingleTilesetCmd*)p;
			Window* r = windows + cmd->window_index;
			if ((uint64_t)cmd->tile_xbegin + cmd->tile_xcount > r->tile_xcount ||
				(uint64_t)cmd->tile_ybegin + cmd->tile_ycount > r->tile_ycount) {
				CommandError("tile update region exceeds tile dimensions");
				return false;
			}
			// partial tile specification: index and palette, single tileset
			uint64_t data_size = (uint64_t)cmd->tile_xcount * cmd->tile_ycount * 2;
			if (data_size + sizeof(UpdateWindowSingleTilesetCmd) != message_length) {
				CommandError("UpdateWindowSingleTileset data size mismatch");
				return false;
			}
			uint8_t* dp = cmd->data;
			for (uint64_t tile_y = 0; tile_y < cmd->tile_ycount; ++tile_y) {
				uint64_t line_offset = (cmd->tile_ybegin + tile_y) * r->tile_xcount + cmd->tile_xbegin;
				for (uint64_t tile_x = 0; tile_x < cmd->tile_xcount; ++tile_x) {
					uint8_t tile_index = *dp++;
					uint8_t palette_index = *dp++;
					if (tileset_records[cmd->tileset_index].xdim != r->tile_xdim ||
						tileset_records[cmd->tileset_index].ydim != r->tile_ydim ||
						tileset_records[cmd->tileset_index].num_entries <= tile_index) {
						CommandError("invalid tile specification");
						return false;
					}
					r->tilesets[line_offset + tile_x] = cmd->tileset_index;
					r->tile_indexes[line_offset + tile_x] = tile_index;
					r->tile_palettes[line_offset + tile_x] = palette_index;
				}
			}
		} break;
		case SDHR_CMD_UPDATE_WINDOW_SINGLE_PALETTE: {
			if (!CheckCommandLength(p, end, sizeof(UpdateWindowSinglePaletteCmd))) return false;
			UpdateWindowSinglePaletteCmd* cmd = (UpdateWindowSinglePaletteCmd*)p;
			Window* r = windows + cmd->window_index;
			if ((uint64_t)cmd->tile_xbegin + cmd->tile_xcount > r->tile_xcount ||
				(uint64_t)cmd->tile_ybegin + cmd->tile_ycount > r->tile_ycount) {
				CommandError("tile update region exceeds tile dimensions");
				return false;
			}
			// partial tile specification: tileset and index, single palette
			uint64_t data_size = (uint64_t)cmd->tile_xcount * cmd->tile_ycount * 2;
			if (data_size + sizeof(UpdateWindowSinglePaletteCmd) != message_length) {
				CommandError("UpdateWindowSinglePalette data size mismatch");
				return false;
			}
			uint8_t* dp = cmd->data;
			for (uint64_t tile_y = 0; tile_y < cmd->tile_ycount; ++tile_y) {
				uint64_t line_offset = (cmd->tile_ybegin + tile_y) * r->tile_xcount + cmd->tile_xbegin;
				for (uint64_t tile_x = 0; tile_x < cmd->tile_xcount; ++tile_x) {
					uint8_t tileset_index = *dp++;
					uint8_t tile_index = *dp++;
					if (tileset_records[tileset_index].xdim != r->tile_xdim ||
						tileset_records[tileset_index].ydim != r->tile_ydim ||
						tileset_records[tileset_index].num_entries <= tile_index) {
						CommandError("invalid tile specification");
						return false;
					}
					r->tilesets[line_offset + tile_x] = tileset_index;
					r->tile_indexes[line_offset + tile_x] = tile_index;
					r->tile_palettes[line_offset + tile_x] = cmd->palette_index;
				}
			}
		} break;
		case SDHR_CMD_UPDATE_WINDOW_SINGLE_BOTH: {
			if (!CheckCommandLength(p, end, sizeof(UpdateWindowSingleBothCmd))) return false;
			UpdateWindowSingleBothCmd* cmd = (UpdateWindowSingleBothCmd*)p;
			Window* r = windows + cmd->window_index;
			if ((uint64_t)cmd->tile_xbegin + cmd->tile_xcount > r->tile_xcount ||
				(uint64_t)cmd->tile_ybegin + cmd->tile_ycount > r->tile_ycount) {
				CommandError("tile update region exceeds tile dimensions");
				return false;
			}
			// partial tile specification: index only, single tileset and palette
			uint64_t data_size = (uint64_t)cmd->tile_xcount * cmd->tile_ycount;
			if (data_size + sizeof(UpdateWindowSingleBothCmd) != message_length) {
				CommandError("UpdateWindowSingleBoth data size mismatch");
				return false;
			}
			uint8_t* dp = cmd->data;
			for (uint64_t tile_y = 0; tile_y < cmd->tile_ycount; ++tile_y) {
				uint64_t line_offset = (cmd->tile_ybegin + tile_y) * r->tile_xcount + cmd->tile_xbegin;
				for (uint64_t tile_x = 0; tile_x < cmd->tile_xcount; ++tile_x) {
					uint8_t tile_index = *dp++;
					if (tileset_records[cmd->tileset_index].xdim != r->tile_xdim ||
						tileset_records[cmd->tileset_index].ydim != r->tile_ydim ||
						tileset_records[cmd->tileset_index].num_entries <= tile_index) {
						CommandError("invalid tile specification");
						return false;
					}
					r->tilesets[line_offset + tile_x] = cmd->tileset_index;
					r->tile_indexes[line_offset + tile_x] = tile_index;
					r->tile_palettes[line_offset + tile_x] = cmd->palette_index;
				}
			}
		} break;
		case SDHR_CMD_UPDATE_WINDOW_SHIFT_TILES: {
			if (!CheckCommandLength(p, end, sizeof(UpdateWindowShiftTilesCmd))) return false;
			UpdateWindowShiftTilesCmd* cmd = (UpdateWindowShiftTilesCmd*)p;
			Window* r = windows + cmd->window_index;
			if (cmd->x_dir < -1 || cmd->x_dir > 1 || cmd->y_dir < -1 || cmd->y_dir > 1) {
				CommandError("invalid tile shift");
				return false;
			}
			if (r->tile_xcount == 0 || r->tile_ycount == 0) {
				CommandError("invalid window for tile shift");
				return false;
			}
			if (cmd->x_dir == -1) {
				for (uint64_t y_index = 0; y_index < r->tile_ycount; ++y_index) {
					uint64_t line_offset = y_index * r->tile_xcount;
					for (uint64_t x_index = 1; x_index < r->tile_xcount; ++x_index) {
						r->tilesets[line_offset + x_index - 1] = r->tilesets[line_offset + x_index];
						r->tile_indexes[line_offset + x_index - 1] = r->tile_indexes[line_offset + x_index];
						r->tile_palettes[line_offset + x_index - 1] = r->tile_palettes[line_offset + x_index];
					}
				}
			}
			else if (cmd->x_dir == 1) {
				for (uint64_t y_index = 0; y_index < r->tile_ycount; ++y_index) {
					uint64_t line_offset = y_index * r->tile_xcount;
					for (uint64_t x_index = r->tile_xcount - 1; x_index > 0; --x_index) {
						r->tilesets[line_offset + x_index] = r->tilesets[line_offset + x_index - 1];
						r->tile_indexes[line_offset + x_index] = r->tile_indexes[line_offset + x_index - 1];
						r->tile_palettes[line_offset + x_index] = r->tile_palettes[line_offset + x_index - 1];
					}
				}
			}
			if (cmd->y_dir == -1) {
				for (uint64_t y_index = 1; y_index < r->tile_ycount; ++y_index) {
					uint64_t line_offset = y_index * r->tile_xcount;
					uint64_t prev_line_offset = line_offset - r->tile_xcount;
					for (uint64_t x_index = 0; x_index < r->tile_xcount; ++x_index) {
						r->tilesets[prev_line_offset + x_index] = r->tilesets[line_offset + x_index];
						r->tile_indexes[prev_line_offset + x_index] = r->tilesets[line_offset + x_index];
						r->tile_palettes[prev_line_offset + x_index] = r->tilesets[line_offset + x_index];
					}
				}
			}
			else if (cmd->y_dir == 1) {
				for (uint64_t y_index = r->tile_ycount - 1; y_index > 0; --y_index) {
					uint64_t line_offset = y_index * r->tile_xcount;
					uint64_t prev_line_offset = line_offset - r->tile_xcount;
					for (uint64_t x_index = 0; x_index < r->tile_xcount; ++x_index) {
						r->tilesets[line_offset + x_index] = r->tilesets[prev_line_offset + x_index];
						r->tile_indexes[line_offset + x_index] = r->tilesets[prev_line_offset + x_index];
						r->tile_palettes[line_offset + x_index] = r->tilesets[prev_line_offset + x_index];
					}
				}
			}
		} break;
		case SDHR_CMD_UPDATE_WINDOW_SET_WINDOW_POSITION: {
			if (!CheckCommandLength(p, end, sizeof(UpdateWindowSetWindowPositionCmd))) return false;
			UpdateWindowSetWindowPositionCmd* cmd = (UpdateWindowSetWindowPositionCmd*)p;
			Window* r = windows + cmd->window_index;
			r->screen_xbegin = cmd->screen_xbegin;
			r->screen_ybegin = cmd->screen_ybegin;
		} break;
		case SDHR_CMD_UPDATE_WINDOW_ADJUST_WINDOW_VIEW: {
			if (!CheckCommandLength(p, end, sizeof(UpdateWindowAdjustWindowViewCommand))) return false;
			UpdateWindowAdjustWindowViewCommand* cmd = (UpdateWindowAdjustWindowViewCommand*)p;
			Window* r = windows + cmd->window_index;
			if ((uint64_t)cmd->tile_ybegin + r->tile_ycount * r->tile_ydim > r->screen_ycount ||
				(uint64_t)cmd->tile_xbegin + r->tile_xcount * r->tile_xdim > r->screen_xcount) {
				CommandError("cannot scroll a window past displayable tile area");
				return false;
			}
			r->tile_xbegin = cmd->tile_xbegin;
			r->tile_ybegin = cmd->tile_ybegin;
		} break;
		case SDHR_CMD_UPDATE_WINDOW_SET_BITMASKS: {
			if (!CheckCommandLength(p, end, sizeof(UpdateWindowSetBitmasksCmd))) return false;
			UpdateWindowSetBitmasksCmd* cmd = (UpdateWindowSetBitmasksCmd*)p;
			Window* r = windows + cmd->window_index;
			if (cmd->tile_xcount != r->tile_xcount ||
				cmd->tile_ycount != r->tile_ycount) {
				CommandError("tile bitmasks do not match tile dimensions");
				return false;
			}
			if (r->bitmask_tilesets) {
				free(r->bitmask_tilesets);
			}
			if (r->bitmask_tile_indexes) {
				free(r->bitmask_tile_indexes);
			}
			r->bitmask_tilesets = (uint8_t*)malloc(cmd->tile_xcount * cmd->tile_ycount);
			r->bitmask_tile_indexes = (uint8_t*)malloc(cmd->tile_xcount * cmd->tile_ycount);

			uint64_t data_size = (uint64_t)cmd->tile_xcount * cmd->tile_ycount * 2;
			if (data_size + sizeof(UpdateWindowSetBitmasksCmd) != message_length) {
				CommandError("UpdateWindowSetBitmasks data size mismatch");
				return false;
			}
			uint8_t* dp = cmd->data;
			for (uint64_t tile_y = 0; tile_y < cmd->tile_ycount; ++tile_y) {
				uint64_t line_offset = (uint64_t)tile_y * cmd->tile_xcount;
				for (uint64_t tile_x = 0; tile_x < cmd->tile_xcount; ++tile_x) {
					uint8_t tileset_index = *dp++;
					uint8_t tile_index = *dp++;
					if (tileset_records[tileset_index].xdim != r->tile_xdim ||
						tileset_records[tileset_index].ydim != r->tile_ydim ||
						tileset_records[tileset_index].num_entries <= tile_index) {
						CommandError("invalid tile specification");
						return false;
					}
					r->bitmask_tilesets[line_offset + tile_x] = tileset_index;
					r->bitmask_tile_indexes[line_offset + tile_x] = tile_index;
				}
			}
		} break;
		case SDHR_CMD_UPDATE_WINDOW_ENABLE: {
			if (!CheckCommandLength(p, end, sizeof(UpdateWindowEnableCmd))) return false;
			UpdateWindowEnableCmd* cmd = (UpdateWindowEnableCmd*)p;
			Window* r = windows + cmd->window_index;
			if (!r->tile_xcount || !r->tile_ycount) {
				CommandError("cannote enable empty window");
				return false;
			}
			r->enabled = cmd->enabled;
		} break;
		case SDHR_CMD_READY: {
			// rubber meets the road, draw the windows to the screen
			for (uint16_t window_index = 0; window_index < 256; ++window_index) {
				Window* w = windows + window_index;
				if (!w->enabled) {
					continue;
				}
				for (int64_t tile_y = 0; tile_y < w->tile_ydim * w->tile_ycount; ++tile_y) {
					if (tile_y < w->tile_ybegin) continue; // row not visible in window
					if (tile_y > w->tile_ybegin + w->screen_ycount) continue; // row not visible in window
					uint64_t tile_yindex = tile_y / w->tile_ydim;
					uint64_t tile_yoffset = tile_y % w->tile_ydim;
					for (int64_t tile_x = 0; tile_x < w->tile_xdim * w->tile_xcount; ++tile_x) {
						if (tile_x < w->tile_xbegin) continue; // column not visible in window
						if (tile_x > w->tile_xbegin + w->screen_ycount) continue; // column not visible in window
						uint64_t tile_xindex = tile_x / w->tile_xdim;
						uint64_t tile_xoffset = tile_x % w->tile_xdim;
						uint64_t entry_index = tile_yindex * w->tile_xcount + tile_xindex;
						if (w->bitmask_tilesets) {
							TilesetRecord* bt = tileset_records + w->bitmask_tilesets[entry_index];
							uint8_t tile_index = w->bitmask_tile_indexes[entry_index];
							uint8_t palette_value = bt->tile_data[tile_yoffset * bt->xdim + tile_yoffset];
							if (!palette_value) continue;  // 0 palette value on bitmask means "don't draw"
						}
						TilesetRecord* t = tileset_records + w->tilesets[tile_yindex * w->tile_xcount + tile_xindex];
						PaletteRecord* pr = palette_records + w->tile_palettes[tile_yindex * w->tile_xcount + tile_xindex];
						uint8_t tile_index = w->tile_indexes[tile_yindex * w->tile_xcount + tile_xindex];
						uint8_t palette_value = t->tile_data[tile_yoffset * t->xdim + tile_xoffset];
						uint8_t red_val = pr->red[palette_value];
						uint8_t green_val = pr->green[palette_value];
						uint8_t blue_val = pr->blue[palette_value];
						// now, where on the screen to put it?
						int64_t screen_y = tile_y + w->screen_ybegin - w->tile_ybegin;
						int64_t screen_x = tile_x + w->screen_xbegin - w->tile_xbegin;
						int64_t screen_offset = screen_y * screen_xcount + screen_x;
						screen_red[screen_offset] = red_val;
						screen_green[screen_offset] = green_val;
						screen_blue[screen_offset] = blue_val;
					}
				}
			}
			return true;
		} break;
		default:
			CommandError("unrecognized command");
			return false;
		}
	}
	return true;
}
