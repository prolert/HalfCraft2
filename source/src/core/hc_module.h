#pragma once

namespace halfcraft
{
	/// a function another loaded dll of this process exports, by name.
	/// @param module - e.g. "client.dll"
	/// @return the function, or nullptr when the dll or the export isn't there
	void* find_export(const char* module, const char* name);

	/// whether a dll is loaded in this process.
	/// @param module - e.g. "game_shader_generic_halfcraft.dll"
	bool module_loaded(const char* module);
}
