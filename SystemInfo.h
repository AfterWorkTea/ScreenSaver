//System Info
#if !defined(SYSTEMINFO_H_INCLUDED_)
#define SYSTEMINFO_H_INCLUDED_

#include <string.h>
#include "Windows.h"

struct SystemInfoData
{
	SYSTEM_INFO SystemInfo;
	DWORD dwNumberOfProcessors;
	DWORD dwProcessorType;
	DWORD dwVersion;
	DWORD dwMajorVersion;
	DWORD dwMinorVersion;
	DWORD dwBuild;
	char *sUserName;
	char *sComputerName;
	char *sDNSName;
};

class SystemInfo
{
	private:
		SystemInfoData data;
		void readSYSTEM_INFO();
		void readVersion();
		void SystemInfo::readNames();
	public:
		SystemInfo();
		~SystemInfo();
		char *getSystem(char *sText, int len);
		char *getUserName();
		char *getComputerName();
		char *getDNSName();
};

#endif //SYSTEMINFO_H_INCLUDED_