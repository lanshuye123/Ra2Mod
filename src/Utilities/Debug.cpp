#include <Utilities/Debug.h>

#include <cstring>

FILE* Debug::LogFile = nullptr;

namespace
{
	char LogPath[MAX_PATH] = {};

	// Directory of the running executable, with a trailing backslash.
	void GetGameDirectory(char* pBuffer, size_t nSize)
	{
		if (!pBuffer || nSize == 0)
			return;

		pBuffer[0] = '\0';

		const DWORD len = GetModuleFileNameA(nullptr, pBuffer, static_cast<DWORD>(nSize));

		if (len == 0 || len >= nSize)
		{
			pBuffer[0] = '\0';
			return;
		}

		char* pLastSeparator = strrchr(pBuffer, '\\');

		if (pLastSeparator)
			*(pLastSeparator + 1) = '\0';
		else
			pBuffer[0] = '\0';
	}
}

bool Debug::Init(const char* pFileName)
{
	if (LogFile)
		return true;

	if (!pFileName)
		return false;

	char dir[MAX_PATH] {};
	GetGameDirectory(dir, sizeof(dir));

	sprintf_s(LogPath, "%s%s", dir, pFileName);

	if (fopen_s(&LogFile, LogPath, "w") != 0)
		LogFile = nullptr;

	return LogFile != nullptr;
}

void Debug::Release()
{
	if (LogFile)
	{
		fflush(LogFile);
		fclose(LogFile);
		LogFile = nullptr;
	}
}

void Debug::Log(const char* pFormat, ...)
{
	if (!LogFile)
		return;

	va_list args;
	va_start(args, pFormat);
	vfprintf(LogFile, pFormat, args);
	va_end(args);
}

void Debug::LogLine(const char* pFormat, ...)
{
	if (!LogFile)
		return;

	SYSTEMTIME st;
	GetLocalTime(&st);

	fprintf(LogFile, "[%02d:%02d:%02d.%03d] ",
		st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

	va_list args;
	va_start(args, pFormat);
	vfprintf(LogFile, pFormat, args);
	va_end(args);

	fputs("\n", LogFile);
	fflush(LogFile);
}

void Debug::LogLine()
{
	LogLine("%s", "");
}

void Debug::DumpBytes(const void* pData, size_t nSize)
{
	if (!LogFile || !pData)
		return;

	const auto* pBytes = static_cast<const unsigned char*>(pData);
	char line[0x100];
	int written = 0;

	for (size_t i = 0; i < nSize; ++i)
	{
		written += sprintf_s(line + written, sizeof(line) - written, "%02X ", pBytes[i]);

		// 16 bytes per line
		if ((i + 1) % 16 == 0 || i + 1 == nSize)
		{
			line[written] = '\0';
			LogLine("        %s", line);
			written = 0;
		}
	}
}
