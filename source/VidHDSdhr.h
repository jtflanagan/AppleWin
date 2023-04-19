#pragma once
#include "Memory.h"
#include "Video.h"
#include "SDHRNetworker.h"

class VidHDSdhr
{
public:
	VidHDSdhr() {
		m_bEnabled = false;
		error_flag = false;
		memset(error_str, sizeof(error_str), 0);
		memset(uploaded_data_region, sizeof(uploaded_data_region), 0);
		memset(tileset_records, sizeof(tileset_records), 0);
		memset(windows, sizeof(windows), 0);
		m_pSDHRNetworker = new SDHRNetworker;
	}
	~VidHDSdhr();
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
	struct ImageAsset {
		void AssignByFilename(VidHDSdhr* owner, const char* filename);
		void AssignByMemory(VidHDSdhr* owner, const uint8_t* buffer, uint64_t size);
		void ExtractTile(VidHDSdhr* owner, uint32_t* tile_p,
			uint16_t tile_xdim, uint16_t tile_ydim, uint64_t xsource, uint64_t ysource);
		// image assets are full 32-bit bitmap files, uploaded from PNG
		uint64_t image_xcount = 0;
		uint64_t image_ycount = 0;
		uint8_t* data = NULL;
	};

	void DefineTileset(uint8_t tileset_index, uint16_t num_entries, uint8_t xdim, uint8_t ydim,
		ImageAsset* asset, uint8_t* offsets);

	bool m_bEnabled;
	struct TilesetRecord {
		uint64_t xdim;
		uint64_t ydim;
		uint64_t num_entries;
		uint32_t* tile_data = NULL;  // tiledata is 32 bit rgba
	};

	struct Window {
		uint8_t enabled;
		uint64_t screen_xcount;  // width in pixels of visible screen area of window
		uint64_t screen_ycount;
		int64_t screen_xbegin;   // pixel xy coordinate where window begins
		int64_t screen_ybegin;
		int64_t tile_xbegin;     // pixel xy coordinate on backing tile array where aperture begins
		int64_t tile_ybegin;
		uint64_t tile_xdim;      // xy dimension, in pixels, of tiles in the window.
		uint64_t tile_ydim;
		uint64_t tile_xcount;    // xy dimension, in tiles, of the tile array
		uint64_t tile_ycount;
		uint8_t* tilesets = NULL;
		uint8_t* tile_indexes = NULL;
	};

	static const uint16_t screen_xcount = 640;
	static const uint16_t screen_ycount = 360;
	std::vector<uint8_t> command_buffer;
	bool error_flag;
	char error_str[256];
	uint8_t uploaded_data_region[256 * 256 * 256];
	ImageAsset image_assets[256];
	TilesetRecord tileset_records[256];
	Window windows[256];
	uint32_t screen_color[screen_xcount * screen_ycount];
	SDHRNetworker* m_pSDHRNetworker;
};

