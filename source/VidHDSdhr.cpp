#include "stdafx.h"
#include "VidHDSdhr.h"
#define STB_IMAGE_IMPLEMENTATION    
#include "stb_image.h"
#include "Log.h"

#include <zlib.h>
#include <fstream>
#include <sstream>

enum ShdrCmd_e {
	SDHR_CMD_UPLOAD_DATA = 1,
	SDHR_CMD_DEFINE_IMAGE_ASSET = 2,
	SDHR_CMD_DEFINE_IMAGE_ASSET_FILENAME = 3,
	SDHR_CMD_DEFINE_TILESET = 4,
	SDHR_CMD_DEFINE_TILESET_IMMEDIATE = 5,
	SDHR_CMD_DEFINE_WINDOW = 6,
	SDHR_CMD_UPDATE_WINDOW_SET_IMMEDIATE = 7,
	//SDHR_CMD_UPDATE_WINDOW_SINGLE_TILESET = 8,
	SDHR_CMD_UPDATE_WINDOW_SHIFT_TILES = 9,
	SDHR_CMD_UPDATE_WINDOW_SET_WINDOW_POSITION = 10,
	SDHR_CMD_UPDATE_WINDOW_ADJUST_WINDOW_VIEW = 11,
	SDHR_CMD_UPDATE_WINDOW_SET_BITMASKS = 12,
	SDHR_CMD_UPDATE_WINDOW_ENABLE = 13,
	SDHR_CMD_READY = 14,
	SDHR_CMD_UPLOAD_DATA_FILENAME = 15,
	SDHR_CMD_UPDATE_WINDOW_SET_UPLOAD = 16,

	SDHR_CMD_CHANGE_RESOLUTION = 50,
	SDHR_CMD_UPDATE_WINDOW_SET_SIZE = 51,
};
#pragma pack(push)
#pragma pack(1)

struct UploadDataCmd {
	uint16_t dest_block;
	uint16_t source_addr;
};

struct UploadDataFilenameCmd {
	uint8_t dest_addr_med;
	uint8_t dest_addr_high;
	uint8_t filename_length;
	uint8_t filename[];
};

struct DefineImageAssetCmd {
	uint8_t asset_index;
	uint16_t block_count;
};

struct DefineImageAssetFilenameCmd {
	uint8_t asset_index;
	uint8_t filename_length;
	uint8_t filename[];  // don't include the trailing null either in the data or counted in the filename_length
};

struct DefineTilesetCmd {
	uint8_t tileset_index;
	uint8_t asset_index;
	uint8_t num_entries;
	uint16_t xdim;
	uint16_t ydim;
	uint16_t block_count;
};

struct DefineTilesetImmediateCmd {
	uint8_t tileset_index;
	uint8_t asset_index;
	uint8_t num_entries;
	uint8_t xdim;
	uint8_t ydim;
	uint8_t data[];  // data is 4-byte records, 16-bit x and y offsets (scaled by x/ydim), from the given asset
};

struct DefineWindowCmd {
	uint8_t window_index;
	uint16_t screen_xcount;		// width in pixels of visible screen area of window
	uint16_t screen_ycount;
	uint16_t tile_xdim;			// xy dimension, in pixels, of tiles in the window.
	uint16_t tile_ydim;
	uint16_t tile_xcount;		// xy dimension, in tiles, of the tile array
	uint16_t tile_ycount;
};

struct UpdateWindowSetImmediateCmd {
	uint8_t window_index;
	uint16_t data_length;
};

struct UpdateWindowSetUploadCmd {
	uint8_t window_index;
	uint16_t block_count;
};

struct UpdateWindowShiftTilesCmd {
	uint8_t window_index;
	int8_t x_dir; // +1 shifts tiles right by 1, negative shifts tiles left by 1, zero no change
	int8_t y_dir; // +1 shifts tiles down by 1, negative shifts tiles up by 1, zero no change
};

struct UpdateWindowSetWindowPositionCmd {
	uint8_t window_index;
	int32_t screen_xbegin;
	int32_t screen_ybegin;
};

struct UpdateWindowAdjustWindowViewCommand {
	uint8_t window_index;
	int32_t tile_xbegin;
	int32_t tile_ybegin;
};

struct UpdateWindowSetWindowSizeCommand {
	uint8_t window_index;
	uint16_t screen_xcount;		// width in pixels of visible screen area of window
	uint16_t screen_ycount;
};

struct UpdateWindowEnableCmd {
	uint8_t window_index;
	uint8_t enabled;
};

#pragma pack(pop)

VidHDSdhr::~VidHDSdhr() {
	NetworkDisable();
	for (uint16_t i = 0; i < 256; ++i) {
		if (image_assets[i].data) {
			stbi_image_free(image_assets[i].data);
		}
		if (tileset_records[i].tile_data) {
			free(tileset_records[i].tile_data);
		}
		if (windows[i].tilesets) {
			free(windows[i].tilesets);
		}
		if (windows[i].tile_indexes) {
			free(windows[i].tile_indexes);
		}
	}
	delete m_pSDHRNetworker;
}

void VidHDSdhr::ImageAsset::AssignByFilename(VidHDSdhr* owner, const char* filename) {
	int width;
	int height;
	int channels;
	data = stbi_load(filename, &width, &height, &channels, 4);
	if (data == NULL) {
		// image failed to load
		owner->error_flag = true;
		return;
	}
	image_xcount = width;
	image_ycount = height;
}

void VidHDSdhr::ImageAsset::AssignByMemory(VidHDSdhr* owner, const uint8_t* buffer, uint64_t size) {
	int width;
	int height;
	int channels;
	data = stbi_load_from_memory(buffer, size, &width, &height, &channels, 4);
	if (data == NULL) {
		owner->error_flag = true;
		return;
	}
	image_xcount = width;
	image_ycount = height;
}

void VidHDSdhr::ImageAsset::ExtractTile(VidHDSdhr* owner, uint32_t* tile_p, uint16_t tile_xdim, uint16_t tile_ydim, uint64_t xsource, uint64_t ysource) {
	uint32_t* dest_p = tile_p;
	if (xsource + tile_xdim > image_xcount ||
		ysource + tile_ydim > image_ycount) {
		owner->CommandError("ExtractTile out of bounds");
		owner->error_flag = true;
		return;
	}
	
	for (uint64_t y = 0; y < tile_ydim; ++y) {
		uint64_t source_yoffset = (ysource + y) * image_xcount * 4;
		for (uint64_t x = 0; x < tile_xdim; ++x) {
			uint64_t pixel_offset = source_yoffset + (xsource + x) * 4;
			uint8_t r = data[pixel_offset];
			uint8_t g = data[pixel_offset + 1];
			uint8_t b = data[pixel_offset + 2];
			uint8_t a = data[pixel_offset + 3];
			uint32_t dest_pixel = (((uint32_t)a) << 24) + (((uint32_t)r) << 16) + (((uint32_t)g) << 8) + b;
			//dest_pixel |= (a & 0x80) ? 0x8000 : 0x0;
			//dest_pixel |= (r >> 3) << 10;
			//dest_pixel |= (g >> 3) << 5;
			//dest_pixel |= (b >> 3);
			*dest_p = dest_pixel;
			++dest_p;
		}
	}
}

int upload_inflate(const char* source, uint64_t size, std::ostream& dest) {
	static const int CHUNK = 16384;
	int ret;
	unsigned have;
	z_stream strm;
	unsigned char in[CHUNK];
	unsigned char out[CHUNK];

	/* allocate inflate state */
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;
	strm.avail_in = 0;
	strm.next_in = Z_NULL;
	ret = inflateInit2(&strm, (15 + 32));
	if (ret != Z_OK)
		return ret;

	/* decompress until deflate stream ends or end of file */
	uint64_t bytes_read = 0;
	while (bytes_read < size) {
		uint64_t bytes_to_read = min(CHUNK, size - bytes_read);
		memcpy(in, source + bytes_read, bytes_to_read);
		bytes_read += bytes_to_read;
		strm.avail_in = bytes_to_read;
		if (strm.avail_in == 0)
			break;
		strm.next_in = in;

		/* run inflate() on input until output buffer not full */
		do {
			strm.avail_out = CHUNK;
			strm.next_out = out;
			ret = inflate(&strm, Z_NO_FLUSH);
			assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
			switch (ret) {
			case Z_NEED_DICT:
				ret = Z_DATA_ERROR;     /* and fall through */
			case Z_DATA_ERROR:
			case Z_MEM_ERROR:
				(void)inflateEnd(&strm);
				return ret;
			}
			have = CHUNK - strm.avail_out;
			dest.write((char*)out, have);
		} while (strm.avail_out == 0);

		/* done when inflate() says it's done */
	} while (ret != Z_STREAM_END);

	/* clean up and return */
	(void)inflateEnd(&strm);
	return ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;
}

void VidHDSdhr::SDHRWriteByte(BYTE value) {
	command_buffer.push_back((uint8_t)value);
	// Could do this on a byte-by-byte basis
	// But instead we batch it in ProcessCommands()
	/*
	if (m_pSDHRNetworker->IsEnabled())
		m_pSDHRNetworker->BusDataPacket(value);
	*/
}


void VidHDSdhr::CommandError(const char* err) {
	strcpy(error_str, err);
	error_flag = true;
	OutputDebugStringA(error_str);
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
	uint32_t pixel_color = screen_color[pixel_offset];
	rgb.r = (pixel_color & 0x00ff0000) >> 16;
	rgb.g = (pixel_color & 0x0000ff00) >> 8;
	rgb.b = pixel_color & 0x000000ff;
	rgb.a = ALPHA;
	return rgb;
}

void VidHDSdhr::DefineTileset(uint8_t tileset_index, uint16_t num_entries, uint8_t xdim, uint8_t ydim, 
	                          ImageAsset* asset, uint8_t* offsets) {
	uint64_t store_data_size = (uint64_t)xdim * ydim * 4 * num_entries;
	TilesetRecord* r = tileset_records + tileset_index;
	if (r->tile_data) {
		free(r->tile_data);
	}
	memset(r, 0, sizeof(TilesetRecord));
	r->xdim = xdim;
	r->ydim = ydim;
	r->num_entries = num_entries;
	r->tile_data = (uint32_t*)malloc(store_data_size);

	uint8_t* offset_p = offsets;
	uint32_t* dest_p = r->tile_data;
	for (uint64_t i = 0; i < num_entries; ++i) {
		uint64_t xoffset = *((uint16_t*)offset_p);
		offset_p += 2;
		uint64_t yoffset = *((uint16_t*)offset_p);
		offset_p += 2;
		uint64_t asset_xoffset = xoffset * xdim;
		uint64_t asset_yoffset = yoffset * xdim;
		asset->ExtractTile(this, dest_p, xdim, ydim, asset_xoffset, asset_yoffset);
		dest_p += (uint64_t)xdim * ydim;
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

	bool isSdhrNetworked = m_pSDHRNetworker->IsEnabled();
	if (isSdhrNetworked)
	{
		if (!m_pSDHRNetworker->IsConnected())
			m_pSDHRNetworker->Connect();
		isSdhrNetworked = m_pSDHRNetworker->IsConnected();
	}

	BYTE* begin = &command_buffer[0];
	BYTE* end = begin + command_buffer.size();
	BYTE* p = begin;
	BYTE* p_start = p;	// p value at the beginning of the command

	while (p < end) {
		DBGPRINT(L"PROCESSING COMMAND %d, actual length: %d, stated length: %d\n", p[2], (size_t)(end - p), *((uint16_t*)p));
		if (!CheckCommandLength(p, end, 2)) {
			return false;
		}
		uint16_t message_length = *((uint16_t*)p);
		if (!CheckCommandLength(p, end, message_length)) return false;
		p_start = p;
		p += 2;
		BYTE cmd = *p++;

		switch (cmd) {
		case SDHR_CMD_UPLOAD_DATA: {
			LogFileOutput("UploadData\n");
			if (!CheckCommandLength(p, end, sizeof(UploadDataCmd))) return false;
			UploadDataCmd* cmd = (UploadDataCmd*)p;
			uint64_t dest_offset = (uint64_t)cmd->dest_block * 512;
			uint64_t data_size = (uint64_t)512;
			memcpy(uploaded_data_region + dest_offset, MemGetMainPtr(cmd->source_addr), data_size);
			if (isSdhrNetworked)
				m_pSDHRNetworker->BusDataMemoryStream(MemGetMainPtr(cmd->source_addr), cmd->source_addr, data_size);
		} break;
		case SDHR_CMD_DEFINE_IMAGE_ASSET: {
			LogFileOutput("DefineImageAsset\n");
			if (!CheckCommandLength(p, end, sizeof(DefineImageAssetCmd))) return false;
			DefineImageAssetCmd* cmd = (DefineImageAssetCmd*)p;
			uint64_t upload_start_addr = 0;
			uint64_t upload_data_size = (uint64_t)cmd->block_count * 512;
			ImageAsset* r = image_assets + cmd->asset_index;

			if (r->data != NULL) {
				stbi_image_free(r->data);
			}
			r->AssignByMemory(this, uploaded_data_region + upload_start_addr, upload_data_size);
			if (error_flag) {
				return false;
			}
		} break;
		case SDHR_CMD_DEFINE_TILESET: {
			LogFileOutput("DefineTileset\n");
			if (!CheckCommandLength(p, end, sizeof(DefineTilesetCmd))) return false;
			DefineTilesetCmd* cmd = (DefineTilesetCmd*)p;
			uint16_t num_entries = cmd->num_entries;
			if (num_entries == 0) {
				num_entries = 256;
			}
			uint64_t required_data_size = num_entries * 4;
			if (cmd->block_count * 512 < required_data_size) {
				CommandError("Insufficient data space for tileset");
			}
			ImageAsset* asset = image_assets + cmd->asset_index;
			DefineTileset(cmd->tileset_index, num_entries, cmd->xdim, cmd->ydim, asset, uploaded_data_region);
		} break;
		case SDHR_CMD_DEFINE_TILESET_IMMEDIATE: {
			LogFileOutput("DefineTilesetImmediate\n");
			if (!CheckCommandLength(p, end, sizeof(DefineTilesetImmediateCmd))) return false;
			DefineTilesetImmediateCmd* cmd = (DefineTilesetImmediateCmd*)p;
			uint16_t num_entries = cmd->num_entries;
			if (num_entries == 0) {
				num_entries = 256;
			}
			uint64_t load_data_size;
			load_data_size = (uint64_t)num_entries * 4;
			if (message_length != sizeof(DefineTilesetImmediateCmd) + load_data_size + 3) {	// 3 is cmd_len and cmd_id
				CommandError("DefineTilesetImmediate data size mismatch");
				return false;
			}
			ImageAsset* asset = image_assets + cmd->asset_index;
			DefineTileset(cmd->tileset_index, num_entries, cmd->xdim, cmd->ydim, asset, cmd->data);
		} break;
		case SDHR_CMD_DEFINE_WINDOW: {
			LogFileOutput("DefineWindow\n");
			if (!CheckCommandLength(p, end, sizeof(DefineWindowCmd))) return false;
			DefineWindowCmd* cmd = (DefineWindowCmd*)p;
			Window* r = windows + cmd->window_index;
/*
			if (r->screen_xcount > screen_xcount) {
				CommandError("Window exceeds max x resolution");
				return false;
			}
			if (r->screen_ycount > screen_ycount) {
				CommandError("Window exceeds max y resolution");
				return false;
			}
*/
			r->enabled = false;
			r->screen_xcount = cmd->screen_xcount;
			r->screen_ycount = cmd->screen_ycount;
			r->screen_xbegin = 0;
			r->screen_ybegin = 0;
			r->tile_xbegin = 0;
			r->tile_ybegin = 0;
			r->tile_xdim = cmd->tile_xdim;
			r->tile_ydim = cmd->tile_ydim;
			r->tile_xcount = cmd->tile_xcount;
			r->tile_ycount = cmd->tile_ycount;
			if (r->tilesets) {
				free(r->tilesets);
			}
			r->tilesets = (uint8_t*)malloc(r->tile_xcount * r->tile_ycount);
			if (r->tile_indexes) {
				free(r->tile_indexes);
			}
			r->tile_indexes = (uint8_t*)malloc(r->tile_xcount * r->tile_ycount);
		} break;
		case SDHR_CMD_UPDATE_WINDOW_SET_IMMEDIATE: {
			LogFileOutput("UpdateWindowSetImmediate\n");
			size_t cmd_sz = sizeof(UpdateWindowSetImmediateCmd);
			if (!CheckCommandLength(p, end, cmd_sz)) return false;
			UpdateWindowSetImmediateCmd* cmd = (UpdateWindowSetImmediateCmd*)p;
			Window* r = windows + cmd->window_index;

			// full tile specification: tileset and index
			uint64_t required_data_size = (uint64_t)r->tile_xcount * r->tile_ycount * 2;
			if (required_data_size != cmd->data_length) {
				CommandError("UpdateWindowSetImmediate data size mismatch");
				return false;
			}
			if (!CheckCommandLength(p, end, cmd_sz + cmd->data_length)) return false;
			uint8_t* sp = p + cmd_sz;
			for (uint64_t i = 0; i < cmd->data_length / 2; ++i) {
				uint8_t tileset_index = sp[i * 2];
				uint8_t tile_index = sp[i * 2 + 1];
/*
				if (tileset_records[tileset_index].xdim != r->tile_xdim ||
					tileset_records[tileset_index].ydim != r->tile_ydim ||
					tileset_records[tileset_index].num_entries <= tile_index) {
					CommandError("invalid tile specification");
					return false;
				}*/
				r->tilesets[i] = tileset_index;
				r->tile_indexes[i] = tile_index;
			}
			// WARNING:
			// This command is unique in that it has variable sized data appended to it
			// Let's cheat here and move p forward by the data size. It'll be moved by the command
			// size at the end of the switch
			p += cmd->data_length;
		} break;
		case SDHR_CMD_UPDATE_WINDOW_SET_UPLOAD: {
			LogFileOutput("UpdateWindowSetUpload\n");
			if (!CheckCommandLength(p, end, sizeof(UpdateWindowSetUploadCmd))) return false;
			UpdateWindowSetUploadCmd* cmd = (UpdateWindowSetUploadCmd*)p;
			Window* r = windows + cmd->window_index;
			// full tile specification: tileset and index
			uint64_t data_size = (uint64_t)cmd->block_count * 512;
			std::stringstream ss;
			upload_inflate((const char*)uploaded_data_region, data_size, ss);
			std::string s = ss.str();
			if (s.length() != r->tile_xcount * r->tile_ycount * 2) {
				CommandError("UploadWindowSetUpload data insufficient to define window tiles");
			}
			uint8_t* sp = (uint8_t*)s.c_str();
			for (uint64_t tile_y = 0; tile_y < r->tile_ycount; ++tile_y) {
				uint64_t line_offset = (uint64_t)tile_y * r->tile_xcount;
				for (uint64_t tile_x = 0; tile_x < r->tile_xcount; ++tile_x) {
					uint8_t tileset_index = *sp++;
					uint8_t tile_index = *sp++;
					if (tileset_records[tileset_index].xdim != r->tile_xdim ||
						tileset_records[tileset_index].ydim != r->tile_ydim ||
						tileset_records[tileset_index].num_entries <= tile_index) {
						CommandError("invalid tile specification");
						return false;
					}
					r->tilesets[line_offset + tile_x] = tileset_index;
					r->tile_indexes[line_offset + tile_x] = tile_index;
				}
			}
		} break;
		case SDHR_CMD_UPDATE_WINDOW_SHIFT_TILES: {
			LogFileOutput("UpdateWindowShiftTiles\n");
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
					}
				}
			}
			else if (cmd->x_dir == 1) {
				for (uint64_t y_index = 0; y_index < r->tile_ycount; ++y_index) {
					uint64_t line_offset = y_index * r->tile_xcount;
					for (uint64_t x_index = r->tile_xcount - 1; x_index > 0; --x_index) {
						r->tilesets[line_offset + x_index] = r->tilesets[line_offset + x_index - 1];
						r->tile_indexes[line_offset + x_index] = r->tile_indexes[line_offset + x_index - 1];
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
					}
				}
			}
		} break;
		case SDHR_CMD_UPDATE_WINDOW_SET_WINDOW_POSITION: {
			LogFileOutput("UpdateWindowSetWindowPosition\n");
			if (!CheckCommandLength(p, end, sizeof(UpdateWindowSetWindowPositionCmd))) return false;
			UpdateWindowSetWindowPositionCmd* cmd = (UpdateWindowSetWindowPositionCmd*)p;
			Window* r = windows + cmd->window_index;
			r->screen_xbegin = cmd->screen_xbegin;
			r->screen_ybegin = cmd->screen_ybegin;
		} break;
		case SDHR_CMD_UPDATE_WINDOW_ADJUST_WINDOW_VIEW: {
			LogFileOutput("UpdateWindowAdjustWindowView\n");
			if (!CheckCommandLength(p, end, sizeof(UpdateWindowAdjustWindowViewCommand))) return false;
			UpdateWindowAdjustWindowViewCommand* cmd = (UpdateWindowAdjustWindowViewCommand*)p;
			Window* r = windows + cmd->window_index;
			r->tile_xbegin = cmd->tile_xbegin;
			r->tile_ybegin = cmd->tile_ybegin;
		} break;
		case SDHR_CMD_UPDATE_WINDOW_ENABLE: {
			LogFileOutput("UpdateWindowEnable\n");
			if (!CheckCommandLength(p, end, sizeof(UpdateWindowEnableCmd))) return false;
			UpdateWindowEnableCmd* cmd = (UpdateWindowEnableCmd*)p;
			Window* r = windows + cmd->window_index;
			if (!r->tile_xcount || !r->tile_ycount) {
				CommandError("cannote enable empty window");
				return false;
			}
			r->enabled = cmd->enabled;
		} break;
		case SDHR_CMD_UPDATE_WINDOW_SET_SIZE: {
			LogFileOutput("UpdateWindowSetWindowSize\n");
			if (!CheckCommandLength(p, end, sizeof(UpdateWindowSetWindowSizeCommand))) return false;
			UpdateWindowSetWindowSizeCommand* cmd = (UpdateWindowSetWindowSizeCommand*)p;
			Window* r = windows + cmd->window_index;
			r->screen_xcount = cmd->screen_xcount;
			r->screen_ycount = cmd->screen_ycount;
		} break;
		case SDHR_CMD_CHANGE_RESOLUTION: {
			LogFileOutput("ChangeResolution\n");
		} break;
		default:
			CommandError("unrecognized command");
			return false;
		}
		p += message_length - 3;
		if (isSdhrNetworked)
			m_pSDHRNetworker->BusDataCommandStream(p_start, p - p_start);
	}
	if (p == end) {
		if (isSdhrNetworked)
			m_pSDHRNetworker->BusCtrlPacket(SDHRCtrl_e::SDHR_CTRL_PROCESS);
		// rubber meets the road, draw the windows to the screen
		/*
		for (uint16_t window_index = 0; window_index < 256; ++window_index) {
			Window* w = windows + window_index;
			if (!w->enabled) {
				continue;
			}
			for (int64_t tile_y = w->tile_ybegin; tile_y < w->tile_ybegin + w->screen_ycount; ++tile_y) {
				int64_t adj_tile_y = tile_y;
				int64_t tile_yspan = (int64_t)w->tile_ycount * w->tile_ydim;
				while (adj_tile_y < 0) adj_tile_y += tile_yspan;
				while (adj_tile_y >= tile_yspan) adj_tile_y -= tile_yspan;
				uint64_t tile_yindex = adj_tile_y / w->tile_ydim;
				uint64_t tile_yoffset = adj_tile_y % w->tile_ydim;
				for (int64_t tile_x = w->tile_xbegin; tile_x < w->tile_xbegin + w->screen_xcount; ++tile_x) {
					int64_t adj_tile_x = tile_x;
					int64_t tile_xspan = (int64_t)w->tile_xcount * w->tile_xdim;
					while (adj_tile_x < 0) adj_tile_x += tile_xspan;
					while (adj_tile_x >= tile_xspan) adj_tile_x -= tile_xspan;
					uint64_t tile_xindex = adj_tile_x / w->tile_xdim;
					uint64_t tile_xoffset = adj_tile_x % w->tile_xdim;
					uint64_t entry_index = tile_yindex * w->tile_xcount + tile_xindex;
					uint32_t pixel_color;
					TilesetRecord* t = tileset_records + w->tilesets[entry_index];
					uint64_t tile_index = w->tile_indexes[entry_index];
					pixel_color = t->tile_data[tile_index * t->xdim * t->ydim + tile_yoffset * t->xdim + tile_xoffset];
					if ((pixel_color & 0xff000000) == 0) {
						continue; // zero alpha, don'd draw
					}
					// now, where on the screen to put it?
					int64_t screen_y = tile_y + w->screen_ybegin - w->tile_ybegin;
					int64_t screen_x = tile_x + w->screen_xbegin - w->tile_xbegin;
					if (screen_x < 0 || screen_y < 0 || screen_x > screen_xcount || screen_y > screen_ycount) {
						// destination pixel offscreen, do not draw
						continue;
					}
					int64_t screen_offset = screen_y * screen_xcount + screen_x;
					screen_color[screen_offset] = pixel_color;
				}
			}
		}
		*/
		command_buffer.clear();
	}
	else {
		CommandError("misaligned on command buffer");
	}
	return false;
}

void VidHDSdhr::NetworkEnable() {
	if (m_pSDHRNetworker->IsEnabled())
		if (!m_pSDHRNetworker->IsConnected())
			m_pSDHRNetworker->Connect();
	if (m_pSDHRNetworker->IsConnected())
		m_pSDHRNetworker->BusCtrlPacket(SDHRCtrl_e::SDHR_CTRL_ENABLE);
}

void VidHDSdhr::NetworkDisable()
{
	if (m_pSDHRNetworker->IsConnected())
	{
		m_pSDHRNetworker->BusCtrlPacket(SDHRCtrl_e::SDHR_CTRL_DISABLE);
	}
}

void VidHDSdhr::NetworkReset()
{
	if (m_pSDHRNetworker->IsEnabled())
		if (!m_pSDHRNetworker->IsConnected())
			m_pSDHRNetworker->Connect();
	if (m_pSDHRNetworker->IsConnected())
		m_pSDHRNetworker->BusCtrlPacket(SDHRCtrl_e::SDHR_CTRL_RESET);
}
