#include "hc_files.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace halfcraft
{
	bool file_deletable(const char* path)
	{
		// delete access with no sharing: refused while anyone has the file open, whatever they share
		HANDLE handle = ::CreateFileA(path, DELETE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (handle == INVALID_HANDLE_VALUE) {
			const DWORD error = ::GetLastError();
			return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
		}
		::CloseHandle(handle);
		return true;
	}

	long long free_bytes(const char* folder)
	{
		ULARGE_INTEGER available{};
		if (!::GetDiskFreeSpaceExA(folder, &available, nullptr, nullptr)) {
			return -1;
		}
		return static_cast<long long>(available.QuadPart);
	}
}
