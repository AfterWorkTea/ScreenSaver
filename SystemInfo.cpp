#include "SystemInfo.h"
#include "stdio.h"
#include "Windows.h"


char *setPtrString(char *sTo, char *sFrom) {
	if(sTo)	GlobalFree((HANDLE)sTo);
	int len = strlen(sFrom) + 1;
	sTo = (char *)GlobalAlloc(GPTR, len);
	strcpy_s(sTo, len, sFrom);
	return sTo;
}

SystemInfo::SystemInfo() {
	readSYSTEM_INFO();
	readVersion();
	readNames();
}

SystemInfo::~SystemInfo() {
	if(data.sComputerName) GlobalFree((HANDLE)data.sComputerName);
	if(data.sDNSName)      GlobalFree((HANDLE)data.sDNSName);
	if(data.sUserName)     GlobalFree((HANDLE)data.sUserName);
}

void SystemInfo::readSYSTEM_INFO() {
	GetSystemInfo(&(data.SystemInfo));
	data.dwNumberOfProcessors = data.SystemInfo.dwNumberOfProcessors;
	data.dwProcessorType = data.SystemInfo.dwProcessorType;
}

char *SystemInfo::getSystem(char *sText, int len) {
	sprintf_s(sText, len, "[Version:%i,%i,%i CPU:%i,%i]",
		data.dwMajorVersion, data.dwMinorVersion, data.dwBuild,
		data.dwProcessorType, data.dwNumberOfProcessors);
	return sText;
}

void SystemInfo::readVersion() {
	data.dwVersion = GetVersion();
	data.dwMajorVersion = (DWORD)(LOBYTE(LOWORD(data.dwVersion)));
	data.dwMinorVersion = (DWORD)(HIBYTE(LOWORD(data.dwVersion)));
	if(data.dwVersion < 0x80000000)
		data.dwBuild = (DWORD)(HIWORD(data.dwVersion));
	else
		data.dwBuild = 0;
}

char *SystemInfo::getUserName() {
	return data.sUserName;
}

char *SystemInfo::getComputerName() {
	return data.sComputerName;
}

char *SystemInfo::getDNSName() {
	return data.sDNSName;
}

void SystemInfo::readNames() {
	char sTempUserName[1000] = { 0 };
	char sTempComputerName[1000] = { 0 };
	char sTempDNSName[1000] = { 0 };
	//UserName
	DWORD dwSize = 999;
	data.sUserName = NULL;
	if(!GetUserName(sTempUserName, &dwSize))
		sprintf_s(sTempUserName, "NoUserName");
	data.sUserName = setPtrString(data.sUserName, sTempUserName);
	//CompName
	dwSize = 999;
	data.sComputerName = NULL;
	if(!GetComputerName(sTempComputerName, &dwSize))
		sprintf_s(sTempComputerName, "NoComputerName");
	data.sComputerName = setPtrString(data.sComputerName, sTempComputerName);
	//DNS Name
	dwSize = 999;
	data.sDNSName = NULL;
	if(!GetComputerNameEx((COMPUTER_NAME_FORMAT)2, sTempDNSName, &dwSize)) //2-DNS domain
		sprintf_s(sTempDNSName, "DNSName");
	data.sDNSName = setPtrString(data.sDNSName, sTempDNSName);
}
