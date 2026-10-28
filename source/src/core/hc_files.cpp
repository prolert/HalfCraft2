#include "hc_files.h"

#include <string>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace halfcraft
{
	namespace
	{
		std::wstring widened(const char* text, UINT code_page)
		{
			const DWORD flags = code_page == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0;
			const int   length = ::MultiByteToWideChar(code_page, flags, text, -1, nullptr, 0);
			if (length <= 0) {
				return {};
			}
			std::wstring wide(static_cast<std::size_t>(length), L'\0');
			::MultiByteToWideChar(code_page, flags, text, -1, wide.data(), length);
			wide.resize(static_cast<std::size_t>(length) - 1);
			return wide;
		}

		/// the path as windows names something that's there: read as utf-8, else as the ansi code page; empty when
		/// neither reading finds anything
		std::wstring existing(const char* path)
		{
			for (const UINT code_page : { static_cast<UINT>(CP_UTF8), static_cast<UINT>(CP_ACP) }) {
				const std::wstring wide = widened(path, code_page);
				if (!wide.empty() && ::GetFileAttributesW(wide.c_str()) != INVALID_FILE_ATTRIBUTES) {
					return wide;
				}
			}
			return {};
		}
	}

	Deletable file_deletable(const char* path)
	{
		const std::wstring wide = path ? existing(path) : std::wstring();
		if (wide.empty()) {
			return Deletable::kUnknown;
		}
		// delete access with no sharing: refused while anyone has the file open, whatever they share
		HANDLE handle = ::CreateFileW(wide.c_str(), DELETE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (handle == INVALID_HANDLE_VALUE) {
			const DWORD error = ::GetLastError();
			return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? Deletable::kUnknown : Deletable::kNo;
		}
		::CloseHandle(handle);
		return Deletable::kYes;
	}

	long long free_bytes(const char* folder)
	{
		const std::wstring wide = folder ? existing(folder) : std::wstring();
		ULARGE_INTEGER     available{};
		if (wide.empty() || !::GetDiskFreeSpaceExW(wide.c_str(), &available, nullptr, nullptr)) {
			return -1;
		}
		return static_cast<long long>(available.QuadPart);
	}
}
