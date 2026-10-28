#pragma once

// what windows says about files that the engine's file system doesn't: whether one could be deleted right now, and the
// room left on a disk. paths as the engine gives them (RelativePathToFullPath). engine-agnostic.

namespace halfcraft
{
	/// whether the file could be deleted now: nothing has it open (true when it isn't there).
	bool file_deletable(const char* path);

	/// the bytes this user may still write to the disk a folder is on; -1 when windows won't say.
	long long free_bytes(const char* folder);
}
