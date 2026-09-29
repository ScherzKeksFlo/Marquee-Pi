#pragma once
#include <windows.h>
#include <string>

// Creates a 32-bit top-down DIB of exactly width x height showing the media file
// scaled to fit (letterboxed on a dark background). Images are decoded with WIC;
// MP4 videos show the frame from the middle of the clip via Media Foundation.
// Returns nullptr when the file cannot be decoded; the caller owns the bitmap.
// COM must be initialised on the calling thread.
HBITMAP createThumbnail(const std::wstring& path, int width, int height);
