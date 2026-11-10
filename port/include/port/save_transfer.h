// The Saves tab's card files, for the save transfer between devices
// (port/settings/features/save_transfer.cpp), from port/settings/features/
// saves.cpp, which keeps the cards' backups.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

// <user>/<JAP|USA|EUR>/Card A for release 'J', 'E' or 'P'.
std::string portSavesCardDir(char release);

// The card file name (in release's card) copied to the dated backups first,
// then bytes written over it (through a temporary file). false and why; refused
// unless name is release's save file and bytes a save of that release at the
// game's size (port/include/port/save_convert.h portSaveRelease).
bool portSavesWriteFile(char release, const std::string& name, const std::vector<uint8_t>& bytes, std::string& why);
