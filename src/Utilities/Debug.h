#pragma once
#include <Windows.h>
#include <cstdio>
#include <cstdarg>

// Minimal file logger.
//
// The log lives next to gamemd.exe (path derived from the module file name, never
// from the working directory) and is flushed after every line so that the tail of
// the file survives a crash.
class Debug
{
public:
	static FILE* LogFile;

	// Opens <game dir>\<pFileName> for writing. Returns false if the file cannot
	// be created; logging then becomes a no-op instead of crashing.
	static bool Init(const char* pFileName);
	static void Release();

	static void Log(const char* pFormat, ...);
	static void LogLine(const char* pFormat, ...);
	static void LogLine();

	// Hex dump, useful when inspecting game memory.
	static void DumpBytes(const void* pData, size_t nSize);
};
