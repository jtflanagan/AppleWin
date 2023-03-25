#pragma once
#include "Memory.h"
#include "Video.h"

class VidHDSdhr
{
public:
	VidHDSdhr() {
		m_bEnabled = false;
		error_flag = false;
		memset(error_str, sizeof(error_str), 0);
		memset(uploaded_data_region, sizeof(uploaded_data_region), 0);
		memset(tileset_records, sizeof(tileset_records), 0);
		memset(palette_records, sizeof(palette_records), 0);
		memset(windows, sizeof(windows), 0);
	}
	~VidHDSdhr() {
		for (uint16_t i = 0; i < 256; ++i) {
			if (tileset_records[i].tile_data) {
				free(tileset_records[i].tile_data);
			}
			if (windows[i].tilesets) {
				free(windows[i].tilesets);
			}
			if (windows[i].tile_indexes) {
				free(windows[i].tile_indexes);
			}
			if (windows[i].tile_palettes) {
				free(windows[i].tile_palettes);
			}
			if (windows[i].bitmask_tilesets) {
				free(windows[i].bitmask_tilesets);
			}
			if (windows[i].bitmask_tile_indexes) {
				free(windows[i].bitmask_tile_indexes);
			}
		}

	}
	void ToggleSDHR(bool value) {
		m_bEnabled = value;
	}

	bool IsSdhrEnabled(void) {
		return m_bEnabled;
	}

	void SDHRWriteByte(BYTE byte);

	bool ProcessCommands(void);

	bgra_t GetPixel(uint16_t vert, uint16_t horz);

	void CommandError(const char* err);
	bool CheckCommandLength(BYTE* p, BYTE* b, size_t sz);
	uint64_t DataOffset(uint8_t low, uint8_t med, uint8_t high) {
		return (uint64_t)high * 256 * 256 + (uint64_t)med * 256 + low;
	}
	bool DataSizeCheck(uint64_t offset, uint64_t data_size) {
		if (offset + data_size >= sizeof(uploaded_data_region)) {
			CommandError("data not bounded by uploaded data region");
			return false;
		}
		return true;
	}
private:
	bool m_bEnabled;
	struct TilesetRecord {
		uint8_t depth;
		uint64_t xdim;
		uint64_t ydim;
		uint64_t num_entries;
		uint8_t* tile_data = NULL;
	};
	struct PaletteRecord {
		uint8_t red[16];
		uint8_t blue[16];
		uint8_t green[16];
	};

	struct Window {
		uint8_t enabled;
		uint64_t screen_xcount;
		uint64_t screen_ycount;
		int64_t screen_xbegin;
		int64_t screen_ybegin;
		int64_t tile_xbegin;
		int64_t tile_ybegin;
		uint64_t tile_xdim;
		uint64_t tile_ydim;
		uint64_t tile_xcount;
		uint64_t tile_ycount;
		uint8_t* tilesets = NULL;
		uint8_t* tile_indexes = NULL;
		uint8_t* tile_palettes = NULL;
		uint8_t* bitmask_tilesets = NULL;
		uint8_t* bitmask_tile_indexes = NULL;
	};

	static const uint16_t screen_xcount = 640;
	static const uint16_t screen_ycount = 360;
	std::vector<uint8_t> command_buffer;
	bool error_flag;
	char error_str[256];
	uint8_t uploaded_data_region[256 * 256 * 256];
	TilesetRecord tileset_records[256];
	PaletteRecord palette_records[256];
	Window windows[256];
	uint8_t screen_red[screen_xcount * screen_ycount];
	uint8_t screen_green[screen_xcount * screen_ycount];
	uint8_t screen_blue[screen_xcount * screen_ycount];
};

