/*
AppleWin : An Apple //e emulator for Windows

Copyright (C) 1994-1996, Michael O'Brien
Copyright (C) 1999-2001, Oliver Schmidt
Copyright (C) 2002-2005, Tom Charlesworth
Copyright (C) 2006-2019, Tom Charlesworth, Michael Pohoreski, Nick Westgate

AppleWin is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

AppleWin is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with AppleWin; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/* Description: Remote Control Manager
 *
 * Handles remote control and interfaces with GameLink and potentially other interfaces
 *
 * Author: Henri Asseily
 *
 */

#include "StdAfx.h"
#include <Windows.h>		// to inject the incoming Gamelink inputs into Applewin
#include "Interface.h"
#include "Windows/Win32Frame.h"
#include "Windows/AppleWin.h"
#include "Memory.h"
#include "FrameBase.h"
#include "Log.h"
#include "Video.h"
#include "MouseInterface.h"	// for Gamelink in and out
#include "CardManager.h"	// for Gamelink in and out
#include "Speaker.h"		// for Gamelink::In()
#include "Mockingboard.h"	// for sound
#include "DiskImageHelper.h"
#include "RemoteControl/RemoteControlManager.h"
#include "Gamelink.h"
#include "zlib.h"
#include <unordered_set>
#include "Configuration/PropertySheet.h"
#include <unordered_map>


#define UNKNOWN_VOLUME_NAME "Unknown Volume"

 // The GameLink I/O structure
struct Gamelink_Block {
	// UINT pitch;	// not implemented here
	GameLink::sSharedMMapInput_R2 input_prev;
	GameLink::sSharedMMapInput_R2 input;
	GameLink::sSharedMMapAudio_R1 audio;
	UINT repeat_last_tick[256];
	bool want_mouse;
};
Gamelink_Block g_gamelink;

struct Info_WOZ {
	std::string Title;
	std::string Subtitle;
	std::string Version;
	UINT32 sig;
};
Info_WOZ g_infoWoz = { std::string(UNKNOWN_VOLUME_NAME), std::string(), std::string("1.0"), 0 };

struct Info_HDV {
	std::string VolumeName;
	UINT32 sig;
};
Info_HDV g_infoHdv = { std::string(UNKNOWN_VOLUME_NAME), 0 };

UINT iCurrentTicks;						// Used to check the repeat interval
UINT8 iOldVolumeLevel;
static std::unordered_set<UINT8> exclusionSet;		// list of VK codes that will not be passed through to Applewin

bool bHardDiskIsLoaded = false;			// If HD is loaded, use it instead of floppy
bool bFloppyIsLoaded = false;

// Private Prototypes
void reverseScanlines(uint8_t* destination, uint8_t* source, uint32_t width, uint32_t height, uint8_t depth);

// ==========================================================================
// Mappings of USB scancodes to Windows scancodes usable to switch to VK

std::unordered_map<BYTE, WORD> usbUsageIndexToScanCode({
	{0x01, 0x00FF}, // Overrun Error
	{0x02, 0x00FC}, // POST Fail
	{0x04, 0x001E}, // a A
	{0x05, 0x0030}, // b B
	{0x06, 0x002E}, // c C
	{0x07, 0x0020}, // d D
	{0x08, 0x0012}, // e E
	{0x09, 0x0021}, // f F
	{0x0A, 0x0022}, // g G
	{0x0B, 0x0023}, // h H
	{0x0C, 0x0017}, // i I
	{0x0D, 0x0024}, // j J
	{0x0E, 0x0025}, // k K
	{0x0F, 0x0026}, // l L
	{0x10, 0x0032}, // m M
	{0x11, 0x0031}, // n N
	{0x12, 0x0018}, // o O
	{0x13, 0x0019}, // p P
	{0x14, 0x0010}, // q Q
	{0x15, 0x0013}, // r R
	{0x16, 0x001F}, // s S
	{0x17, 0x0014}, // t T
	{0x18, 0x0016}, // u U
	{0x19, 0x002F}, // v V
	{0x1A, 0x0011}, // w W
	{0x1B, 0x002D}, // x X
	{0x1C, 0x0015}, // y Y
	{0x1D, 0x002C}, // z Z
	{0x1E, 0x0002}, // 1 !
	{0x1F, 0x0003}, // 2 @
	{0x20, 0x0004}, // 3 #
	{0x21, 0x0005}, // 4 $
	{0x22, 0x0006}, // 5 %
	{0x23, 0x0007}, // 6 ^
	{0x24, 0x0008}, // 7 &
	{0x25, 0x0009}, // 8 *
	{0x26, 0x000A}, // 9 (
	{0x27, 0x000B}, // 0 )
	{0x28, 0x001C}, // Return
	{0x29, 0x0001}, // Escape
	{0x2A, 0x000E}, // Backspace
	{0x2B, 0x000F}, // Tab
	{0x2C, 0x0039}, // Space
	{0x2D, 0x000C}, // - _
	{0x2E, 0x000D}, // = +
	{0x2F, 0x001A}, // [ {
	{0x30, 0x001B}, // ] }
	{0x31, 0x002B}, // \ |
	{0x32, 0x002B}, // Europe 1 (Note 2)
	{0x33, 0x0027}, // ; :
	{0x34, 0x0028}, // ' "
	{0x35, 0x0029}, // ` ~
	{0x36, 0x0033}, // , <
	{0x37, 0x0034}, // . >
	{0x38, 0x0035}, // / ?
	{0x39, 0x003A}, // Caps Lock
	{0x3A, 0x003B}, // F1
	{0x3B, 0x003C}, // F2
	{0x3C, 0x003D}, // F3
	{0x3D, 0x003E}, // F4
	{0x3E, 0x003F}, // F5
	{0x3F, 0x0040}, // F6
	{0x40, 0x0041}, // F7
	{0x41, 0x0042}, // F8
	{0x42, 0x0043}, // F9
	{0x43, 0x0044}, // F10
	{0x44, 0x0057}, // F11
	{0x45, 0x0058}, // F12
	{0x46, 0xE037}, // Print Screen (Note 1)
	{0x47, 0x0046}, // Scroll Lock
	{0x49, 0xE052}, // Insert (Note 1)
	{0x4A, 0xE047}, // Home (Note 1)
	{0x4B, 0xE049}, // Page Up (Note 1)
	{0x4C, 0xE053}, // Delete (Note 1)
	{0x4D, 0xE04F}, // End (Note 1)
	{0x4E, 0xE051}, // Page Down (Note 1)
	{0x4F, 0xE04D}, // Right Arrow (Note 1)
	{0x50, 0xE04B}, // Left Arrow (Note 1)
	{0x51, 0xE050}, // Down Arrow (Note 1)
	{0x52, 0xE048}, // Up Arrow (Note 1)
	{0x53, 0x0045}, // Num Lock
	{0x54, 0xE035}, // Keypad / (Note 1)
	{0x55, 0x0037}, // Keypad *
	{0x56, 0x004A}, // Keypad -
	{0x57, 0x004E}, // Keypad +
	{0x58, 0xE01C}, // Keypad Enter
	{0x59, 0x004F}, // Keypad 1 End
	{0x5A, 0x0050}, // Keypad 2 Down
	{0x5B, 0x0051}, // Keypad 3 PageDn
	{0x5C, 0x004B}, // Keypad 4 Left
	{0x5D, 0x004C}, // Keypad 5
	{0x5E, 0x004D}, // Keypad 6 Right
	{0x5F, 0x0047}, // Keypad 7 Home
	{0x60, 0x0048}, // Keypad 8 Up
	{0x61, 0x0049}, // Keypad 9 PageUp
	{0x62, 0x0052}, // Keypad 0 Insert
	{0x63, 0x0053}, // Keypad . Delete
	{0x64, 0x0056}, // Europe 2 (Note 2)
	{0x65, 0xE05D}, // App
	{0x67, 0x0059}, // Keypad =
	{0x68, 0x005D}, // F13
	{0x69, 0x005E}, // F14
	{0x6A, 0x005F}, // F15
	{0x85, 0x007E}, // Keypad , (Brazilian Keypad .)
	{0x87, 0x0073}, // Keyboard Int'l 1 ろ (Ro)
	{0x88, 0x0070}, // Keyboard Int'l 2 かたかな ひらがな ローマ字 (Katakana/Hiragana)
	{0x89, 0x007D}, // Keyboard Int'l 3 ￥ (Yen)
	{0x8A, 0x0079}, // Keyboard Int'l 4 前候補 変換 (次候補) 全候補 (Henkan)
	{0x8B, 0x007B}, // Keyboard Int'l 5 無変換 (Muhenkan)
	{0x8C, 0x005C}, // Keyboard Int'l 6 (PC9800 Keypad , )
	{0x90, 0x00F2}, // Keyboard Lang 1 한/영 (Hanguel/English)
	{0x91, 0x00F1}, // Keyboard Lang 2 한자 (Hanja)
	{0x92, 0x0078}, // Keyboard Lang 3 かたかな (Katakana)
	{0x93, 0x0077}, // Keyboard Lang 4 ひらがな (Hiragana)
	{0x94, 0x0076}, // Keyboard Lang 5 半角/全角 (Zenkaku/Hankaku)
	{0xE0, 0x001D}, // Left Control
	{0xE1, 0x002A}, // Left Shift
	{0xE2, 0x0038}, // Left Alt
	{0xE3, 0xE05B}, // Left GUI
	{0xE4, 0xE01D}, // Right Control
	{0xE5, 0x0036}, // Right Shift
	{0xE6, 0xE038}, // Right Alt
	{0xE7, 0xE05C}  // Right GUI
	});
//===========================================================================
// Global functions

bool RemoteControlManager::isRemoteControlEnabled()
{
	return GameLink::GetGameLinkEnabled();
}

//===========================================================================

void RemoteControlManager::setRemoteControlEnabled(bool bEnabled)
{
	return GameLink::SetGameLinkEnabled(bEnabled);
}

//===========================================================================

bool RemoteControlManager::isTrackOnlyEnabled()
{
	return GameLink::GetTrackOnlyEnabled();
}

//===========================================================================

void RemoteControlManager::setTrackOnlyEnabled(bool bEnabled)
{
	return GameLink::SetTrackOnlyEnabled(bEnabled);
}

//===========================================================================
LPBYTE RemoteControlManager::initializeMem(UINT size)
{
	if (GameLink::GetGameLinkEnabled())
	{
		iOldVolumeLevel = (UINT8)SpkrGetVolume();
		LPBYTE mem = (LPBYTE)GameLink::AllocRAM(size);

		// initialize the gamelink previous input to 0
		memset(&g_gamelink.input_prev, 0, sizeof(GameLink::sSharedMMapInput_R2));

		(GameLink::Init(isTrackOnlyEnabled()));
		setKeypressExclusionList(aDefaultKeyExclusionList, sizeof(aDefaultKeyExclusionList));
		updateRunningProgramInfo();	// Disks might have been loaded before the shm was ready
		return mem;
	}
	return NULL;
}

//===========================================================================

bool RemoteControlManager::destroyMem()
{
	if (GameLink::GetGameLinkEnabled())
	{
		GameLink::Term();
		return true;
	}
	return false;
}

//===========================================================================

void RemoteControlManager::setKeypressExclusionList(UINT8 _exclusionList[], UINT8 length)
{
	exclusionSet.clear();
	for (UINT8 i = 0; i < length; i++)
	{
		exclusionSet.insert(_exclusionList[i]);
	}
}

//===========================================================================

void RemoteControlManager::setLoadedFloppyInfo(ImageInfo* imageInfo)
{
	if (imageInfo)
	{
		bFloppyIsLoaded = true;
		// CRC can be 0, let's just make a signature from the metadata
		g_infoWoz.Title = imageInfo->szTitle;
		g_infoWoz.Subtitle = imageInfo->szSubtitle;
		g_infoWoz.Version = imageInfo->szVersion;
		std::string szForCrc = g_infoWoz.Title + g_infoWoz.Subtitle + g_infoWoz.Version;
		g_infoWoz.sig = crc32(0, (BYTE*)szForCrc.c_str(), szForCrc.length());
	}
	else {
		bFloppyIsLoaded = false;
		g_infoWoz.Title = "";
		g_infoWoz.Subtitle = "";
		g_infoWoz.Version = "";
		g_infoWoz.sig = 0;
	}
}

//===========================================================================

void RemoteControlManager::setLoadedHDInfo(ImageInfo* imageInfo)
{
	// pass in NULL to remove it
	if (imageInfo)
	{
		bHardDiskIsLoaded = true;
		if (imageInfo->szVolumeName.length() == 0)
			g_infoHdv.VolumeName = UNKNOWN_VOLUME_NAME;
		else
			g_infoHdv.VolumeName = imageInfo->szVolumeName;
		g_infoHdv.sig = crc32(0, (BYTE*)(g_infoHdv.VolumeName.c_str()), g_infoHdv.VolumeName.length());
	}
	else {
		bHardDiskIsLoaded = false;
		g_infoHdv.VolumeName = "";
		g_infoHdv.sig = 0;
	}
}

//===========================================================================

void RemoteControlManager::updateRunningProgramInfo()
{
	// Updates which program is running
	// Should only be called on re/boot
	if (bHardDiskIsLoaded)
	{
		g_pProgramName = g_infoHdv.VolumeName;
		GameLink::SetProgramInfo(g_pProgramName, 0, 0, 0, g_infoHdv.sig);
	}
	else if (bFloppyIsLoaded)
	{
		g_pProgramName = g_infoWoz.Title;
		GameLink::SetProgramInfo(g_pProgramName, 0, 0, 0, g_infoWoz.sig);
	}
	else
	{
		g_pProgramName = "";
		GameLink::SetProgramInfo(g_pProgramName, 0, 0, 0, 0);
	}

}

//===========================================================================
void RemoteControlManager::getInput()
{
	if (
		GameLink::GetGameLinkEnabled()
		&& GetFrame().g_hFrameWindow != GetFocus()
		&& GameLink::In(&g_gamelink.input, &g_gamelink.audio)
		) {
#ifdef DEBUG
		LogOutput("Mouse dX, dY, WPARAM: %0.2f %0.2f %02X\n", g_gamelink.input.mouse_dx, g_gamelink.input.mouse_dy, g_gamelink.input.mouse_btn);
#endif DEBUG
		// -- Audio input
		UINT iVolMax = GetPropertySheet().GetVolumeMax();
		
		UINT iVolNow;

		if ( g_gamelink.audio.master_vol_l == 0 )
		{
			// 0 = "-Inf" dB ; which maps to iVolMax
			iVolNow = iVolMax;
		}
		else
		{
			float fVolumeLinear, fVolumedB, fVolumeForSpeaker;

			fVolumeLinear = static_cast< float >( g_gamelink.audio.master_vol_l ) / 100.0f;
			fVolumedB = 20.0f * log10f( fVolumeLinear );
			fVolumeForSpeaker = fVolumedB * iVolMax / -48.0f; // note: 1% gives a value of about -40dB, so we scale to fit 0 to iVolMax
			iVolNow = static_cast< UINT >( fVolumeForSpeaker );
		}
		
		if (iVolNow != iOldVolumeLevel)
		{
			SpkrSetVolume(iVolNow, iVolMax);
			GetCardMgr().GetMockingboardCardMgr().SetVolume(iVolNow, iVolMax);

			iOldVolumeLevel = (UINT8)SpkrGetVolume();
		}

		// -- Mouse input
		// Go straight into MouseInterface, it already has support for delta movement
		if (g_gamelink.want_mouse) {
			CMouseInterface* pMouseCard = GetCardMgr().GetMouseCard();
			int iOOBX, iOOBY;	// out of bounds
			pMouseCard->SetPositionRel((long)g_gamelink.input.mouse_dx, (long)g_gamelink.input.mouse_dy, &iOOBX, &iOOBY);
			// Mouse buttons are LEFT, RIGHT, MIDDLE
			// TODO: Check also for CONTROL and SHIFT here?
			// Cache old and new
			const UINT8 old = g_gamelink.input_prev.mouse_btn;
			const UINT8 btn = g_gamelink.input.mouse_btn;
			for (UINT8 i = 0; i <= BUTTON1; i++)
			{
				const UINT8 mask = 1 << i;
				if ((btn & mask) && !(old & mask))
					pMouseCard->SetButton((eBUTTON)i, BUTTON_DOWN);
				if (!(btn & mask) && (old & mask))
					pMouseCard->SetButton((eBUTTON)i, BUTTON_UP);
			}
		}


		// -- Keyboard input

		if (g_gamelink.input.ready)
		{
			// If the input block comes from input_other, the ready flag will be READY_OTHER
			// input_other uses scancodes, not DIK

			// Gamelink sets in shm 8 UINT32s, for a total of $FF bits
			// Using some kid of DIK keycodes.
			// Each bit will state if the scancode at that position is pressed (1) or released (0)
			// We keep a cache of the previous state, so we'll know if a key has changed state
			// and trigger the event
			// This is a map from the custom DIK scancodes to VK codes
			HKL hKeyboardLayout = GetKeyboardLayout(0);
			UINT8 aDIKtoVK[256] = { 0x00, 0x1B, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x30, 0xBD, 0xBB,
									0x08, 0x09, 0x51, 0x57, 0x45, 0x52, 0x54, 0x59, 0x55, 0x49, 0x4F, 0x50, 0xDB, 0xDD, 0x0D,
									0xA2, 0x41, 0x53, 0x44, 0x46, 0x47, 0x48, 0x4A, 0x4B, 0x4C, 0xBA, 0xDE, 0xC0, 0xA0, 0xDC,
									0x5A, 0x58, 0x43, 0x56, 0x42, 0x4E, 0x4D, 0xBC, 0xBE, 0xBF, 0xA1, 0x6A, 0xA4, 0x20, 0x14,
									0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x13, 0x91, 0x24, 0x26, 0x21,
									0x6D, 0x25, 0x0C, 0x27, 0x6B, 0x23, 0x28, 0x22, 0x2D, 0x2E, 0x2C, 0x00, 0xE2, 0x7A, 0x7B,
									0x0C, 0xEE, 0xF1, 0xEA, 0xF9, 0xF5, 0xF3, 0x00, 0x00, 0xFB, 0x2F, 0x7C, 0x7D, 0x7E, 0x7F,
									0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0xED, 0x00, 0xE9, 0x00, 0xC1, 0x00, 0x00, 0x87,
									0x00, 0x00, 0x00, 0x00, 0xEB, 0x09, 0x00, 0xC2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
									0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xB1, 0x00, 0x00, 0x00, 0x00,
									0x00, 0x00, 0x00, 0x00, 0xB0, 0x00, 0x00, 0x0D, 0xA3, 0x00, 0x00, 0xAD, 0xB6, 0xB3, 0x00,
									0xB2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xAE, 0x00, 0xAF, 0x00, 0xB7,
									0x00, 0x00, 0xBF, 0x00, 0x2A, 0xA5, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
									0x00, 0x00, 0x00, 0x90, 0x00, 0x24, 0x26, 0x21, 0x00, 0x25, 0x00, 0x27, 0x00, 0x23, 0x28,
									0x22, 0x2D, 0x2E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5B, 0x5C, 0x5D, 0x00, 0x5F,
									0x00, 0x00, 0x00, 0x00, 0x00, 0xAA, 0xAB, 0xA8, 0xA9, 0xA7, 0xA6, 0xAC, 0xB4, 0xB5, 0x00,
									0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
									0x00, 0x00 };

			// We need to handle long keypresses
			// We set up a do-nothing timer for each key so that the first repeat isn't too fast

			iCurrentTicks = GetTickCount();
			for (UINT8 blk = 0; blk < 8; ++blk)
			{
				const UINT old = g_gamelink.input_prev.keyb_state[blk];
				const UINT key = g_gamelink.input.keyb_state[blk];
				WORD scancode;
				UINT32 mask;
				UINT iKeyState;
				WORD iVK_Code;
				LPARAM lparam;
				UINT16 repeat;
				bool bCD, bPD;	// is current key down? is previous key down?

				for (UINT8 bit = 0; bit < 32; ++bit)
				{
					scancode = static_cast<UINT8>((blk * 32) + bit);
					mask = 1 << bit;
					bCD = (key & mask);	// key is down (bCD)
					bPD = (old & mask);	// key was down previously (bPD)
					if ((!bCD) && (!bPD))
						continue;	// the key was neither pressed previously nor now
					if (bCD && bPD)
					{
						// it's a repeat key
						if ((iCurrentTicks - g_gamelink.repeat_last_tick[scancode]) < kMinRepeatInterval)
							continue;	// drop repeat messages within kMinRepeatInterval ms
					}
					if (!bPD)	// This is the first time we're pressing the key, set the no-repeat interval
						g_gamelink.repeat_last_tick[scancode] = iCurrentTicks;
					else		// The key is already in repeat mode. Let it repeat as fast as it wants
						g_gamelink.repeat_last_tick[scancode] = 0;
					// Set up message
					iKeyState = bCD ? WM_KEYDOWN : WM_KEYUP;
					// if READY_OTHER, the scancode is a usb scancode, not a DIK
					// We need to convert it to a regular scancode, and then to a VK
					if (g_gamelink.input.ready == GameLink::sSharedMMapInput_R2::READY_OTHER)
					{
						iVK_Code = MapVirtualKey(usbUsageIndexToScanCode.at(scancode), MAPVK_VSC_TO_VK);
						if (usbUsageIndexToScanCode.at(scancode) >= 0x100)	// for arrow keys support
							scancode = MapVirtualKey(iVK_Code, MAPVK_VK_TO_VSC) + 0x100;
						else
							scancode = MapVirtualKey(iVK_Code, MAPVK_VK_TO_VSC);
					}
					else
					{
						iVK_Code = aDIKtoVK[scancode];
					}
					repeat = 1;		// Managed with ticks above
					{	// set up lparam
						lparam = repeat;
						lparam = lparam | (LPARAM)(scancode << 16);				// scancode
						lparam = lparam | (LPARAM)((scancode > 0x7F) << 24);	// extended
						lparam = lparam | (LPARAM)(bPD << 30);				// previous key state
						lparam = lparam | (LPARAM)(!bCD << 31);		// transition state (1 for keyup)
					}
					// With PostMessage, the message goes to the highest level of AppleWin, hence controlling
					// all of AppleWin's behavior, including opening popups, etc...
					// It's not at all ideal, so we filter which keystrokes to pass in with a configurable exclusion list
					if (!exclusionSet.count(iVK_Code))
					{
						PostMessageW(GetFrame().g_hFrameWindow, iKeyState, iVK_Code, lparam);
#ifdef _DEBUG
						LogOutput("SCANCODE, iVK, LPARAM: %04X, %04X, %04X\n", scancode, iVK_Code, lparam);
#endif _DEBUG
					}
				}
			}
			// We're done parsing the input. Store it as the previous state
			g_gamelink.input.ready = 0;
			memcpy(&g_gamelink.input_prev, &g_gamelink.input, sizeof(GameLink::sSharedMMapInput_R2));
		}
	}
}


//===========================================================================

void RemoteControlManager::sendOutput(LPBITMAPINFO g_pFramebufferinfo, UINT8 *g_pFramebufferbits)
{
	if (GameLink::GetGameLinkEnabled()) {
		// here send the last drawn frame to GameLink
		// We could efficiently send to GameLink g_pFramebufferbits with GetFrameBufferWidth/GetFrameBufferHeight, but the scanlines are reversed
		// We instead memcpy each scanline of the bitmap of the frame in reverse into another buffer, and pass that to GameLink.
		// When GridCartographer/GameLink allows to pass in flags specifying the x/y/w/h etc...,

		if (g_pFramebufferinfo == NULL)
		{
			// Don't send out video, just handle out-of-band commands
			GameLink::Out(MemGetBankPtr(0));
			return;
		}

		CMouseInterface* pMouseCard = GetCardMgr().GetMouseCard();
		// Do not use pMouseCard->isEnabled() or equivalent, since it'll return false
		// when Applewin is not in focus, and that's exactly what it'll be.
		g_gamelink.want_mouse = (bool)pMouseCard;

		GameLink::Out(
			(UINT16)g_pFramebufferinfo->bmiHeader.biWidth,
			(UINT16)g_pFramebufferinfo->bmiHeader.biHeight,
			1.0,								// image ratio
			g_gamelink.want_mouse,
			(const UINT8*)g_pFramebufferbits,
			MemGetBankPtr(0));					// Main memory pointer
	}
}


//===========================================================================

// --------------------------------------------
// Utility
// --------------------------------------------

// The framebuffer has its scanlines inverted, from bottom to top
// To send a correct bitmap out to a 3rd party program we need to reverse the scanlines
static void reverseScanlines(uint8_t* destination, uint8_t* source, uint32_t width, uint32_t height, uint8_t depth)
{
	uint32_t linesize = width * depth;
	uint8_t* loln = source;
	uint8_t* hiln = destination + (height - 1) * linesize;	// first pixel of the last line
	for (size_t i = 0; i < height; i++)
	{
		memcpy(hiln, loln, linesize);
		loln = loln + linesize;
		hiln = hiln - linesize;
	}
}
