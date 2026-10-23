#pragma once
/**
 * @file        rex/filesystem/open_observer.h
 * @brief       Lets an app see which files the title opens.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

namespace rex::filesystem {

// Called on the opening guest thread with the guest path (for example
// "D:\\UTGame\\CookedXenon\\UTFrontEnd.xxx") of every file the title opens
// successfully through NtCreateFile/NtOpenFile. Keep it short: it runs inside
// the title's I/O. nullptr removes it.
using FileOpenObserver = void (*)(const char* guest_path);
void SetFileOpenObserver(FileOpenObserver observer);

}  // namespace rex::filesystem
