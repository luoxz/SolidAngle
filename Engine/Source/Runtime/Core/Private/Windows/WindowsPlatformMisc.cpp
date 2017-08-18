#include "Windows/WindowsPlatformMisc.h"
#include "Misc/DateTime.h"
#include "Misc/AssertionMacros.h"
#include "Logging/LogMacros.h"
#include "Misc/OutputDevice.h"
#include "HAL/PlatformStackWalk.h"
#include "HAL/PlatformProcess.h"
#include "HAL/UnrealMemory.h"
#include "Templates/UnrealTemplate.h"
#include "CoreGlobals.h"
#include "HAL/FileManager.h"
#include "Misc/CString.h"
#include "Misc/Parse.h"
#include "Misc/MessageDialog.h"
#include "Containers/StringConv.h"
#include "Containers/SolidAngleString.h"
#include "CoreGlobals.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Internationalization/Text.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Math/Color.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/OutputDeviceFile.h"
#include "Misc/OutputDeviceError.h"
#include "Misc/FeedbackContext.h"
#include "Misc/CoreDelegates.h"
#include "Misc/App.h"
#include "HAL/ExceptionHandling.h"
#include "Misc/SecureHash.h"
#include "Windows/WindowsApplication.h"
#include "Misc/EngineVersion.h"
#include "GenericPlatform/GenericPlatformCrashContext.h"
#include "Windows/WindowsPlatformCrashContext.h"
#include "HAL/PlatformOutputDevices.h"

#include "GenericPlatform/GenericPlatformChunkInstall.h"
#include "GenericPlatform/GenericPlatformDriver.h"
#include "HAL/ThreadHeartBeat.h"

// Resource includes.
#include "Runtime/Launch/Resources/Windows/Resource.h"

#include "Windows/AllowWindowsPlatformTypes.h"
#include <time.h>
#include <mmsystem.h>
#include <rpcsal.h>
#include <gameux.h>
#include <ShlObj.h>
#include <IntShCut.h>
#include <shellapi.h>
#include <IPHlpApi.h>
#include <VersionHelpers.h>
#include "Windows/HideWindowsPlatformTypes.h"

#include "Modules/ModuleManager.h"

#if !FORCE_ANSI_ALLOCATOR
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Psapi.h>
#include "Windows/HideWindowsPlatformTypes.h"
#pragma comment(lib, "psapi.lib")
#endif

#include <fcntl.h>
#include <io.h>

#include "Windows/AllowWindowsPlatformTypes.h"

// This might not be defined by Windows when maintaining backwards-compatibility to pre-Win8 builds
#ifndef SM_CONVERTIBLESLATEMODE
#define SM_CONVERTIBLESLATEMODE			0x2003
#endif

// this cvar can be removed once we have a single method that works well
static TAutoConsoleVariable<int32> CVarDriverDetectionMethod(
	TEXT("r.DriverDetectionMethod"),
	4,
	TEXT("Defined which implementation is used to detect the GPU driver (to check for old drivers and for logs and statistics)\n")
	TEXT("  0: Iterate available drivers in registry and choose the one with the same name, if in question use next method (happens)\n")
	TEXT("  1: Get the driver of the primary adpater (might not be correct when dealing with multiple adapters)\n")
	TEXT("  2: Use DirectX LUID (would be the best, not yet implemented)\n")
	TEXT("  3: Use Windows functions, use the primary device (might be wrong when API is using another adapter)\n")
	TEXT("  4: Use Windows functions, use the one names like the DirectX Device (newest, most promising)"),
	ECVF_RenderThreadSafe);

typedef HRESULT(STDAPICALLTYPE *GetDpiForMonitorProc)(HMONITOR Monitor, int32 DPIType, uint32 *DPIX, uint32 *DPIY);
static GetDpiForMonitorProc GetDpiForMonitor;

namespace
{
	/**
	* According to MSDN GetVersionEx without special targeting works to 6.2
	* version only. To retrive proper version for later version we can check
	* version of system libraries e.g. kernel32.dll.
	*/
	int32 GetWindowsGT62Versions(bool bIsWorkstation, YString& out_OSVersionLabel)
	{
		const int BufferSize = 256;
		TCHAR Buffer[BufferSize];

		if (!GetSystemDirectory(Buffer, BufferSize))
		{
			return (int32)YWindowsOSVersionHelper::ERROR_GETWINDOWSGT62VERSIONS_FAILED;
		}

		YString SystemDir(Buffer);
		YString KernelPath = YPaths::Combine(*SystemDir, TEXT("kernel32.dll"));

		DWORD Size = GetFileVersionInfoSize(*KernelPath, nullptr);

		if (Size <= 0)
		{
			return (int32)YWindowsOSVersionHelper::ERROR_GETWINDOWSGT62VERSIONS_FAILED;
		}

		TArray<uint8> VerBlock;
		VerBlock.Reserve(Size);

		if (!GetFileVersionInfo(*KernelPath, 0, Size, VerBlock.GetData()))
		{
			return (int32)YWindowsOSVersionHelper::ERROR_GETWINDOWSGT62VERSIONS_FAILED;
		}

		VS_FIXEDFILEINFO* FileInfo = nullptr;
		uint32 Len;

		if (!VerQueryValue(VerBlock.GetData(), TEXT("\\"), (void **)&FileInfo, &Len))
		{
			return (int32)YWindowsOSVersionHelper::ERROR_GETWINDOWSGT62VERSIONS_FAILED;
		}

		int Major = FileInfo->dwProductVersionMS >> 16;
		int Minor = FileInfo->dwProductVersionMS & 0xFFFF;

		switch (Major)
		{
		case 6:
			switch (Minor)
			{
			case 3:
				if (bIsWorkstation)
				{
					out_OSVersionLabel = TEXT("Windows 8.1");
				}
				else
				{
					out_OSVersionLabel = TEXT("Windows Server 2012 R2");
				}
				break;
			case 2:
				if (bIsWorkstation)
				{
					out_OSVersionLabel = TEXT("Windows 8");
				}
				else
				{
					out_OSVersionLabel = TEXT("Windows Server 2012");
				}
				break;
			default:
				return (int32)YWindowsOSVersionHelper::ERROR_UNKNOWNVERSION;
			}
			break;
		case 10:
			switch (Minor)
			{
			case 0:
				if (bIsWorkstation)
				{
					out_OSVersionLabel = TEXT("Windows 10");
				}
				else
				{
					out_OSVersionLabel = TEXT("Windows Server Technical Preview");
				}
				break;
			default:
				return (int32)YWindowsOSVersionHelper::ERROR_UNKNOWNVERSION;
			}
			break;
		default:
			return (int32)YWindowsOSVersionHelper::ERROR_UNKNOWNVERSION;
		}

		return (int32)YWindowsOSVersionHelper::SUCCEEDED;
	}
}

int32 YWindowsOSVersionHelper::GetOSVersions(YString& out_OSVersionLabel, YString& out_OSSubVersionLabel)
{
	int32 ErrorCode = (int32)SUCCEEDED;

	// Get system info
	SYSTEM_INFO SystemInfo;
	if (YPlatformMisc::Is64bitOperatingSystem())
	{
		GetNativeSystemInfo(&SystemInfo);
	}
	else
	{
		GetSystemInfo(&SystemInfo);
	}

	OSVERSIONINFOEX OsVersionInfo = { 0 };
	OsVersionInfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
	out_OSVersionLabel = TEXT("Windows (unknown version)");
	out_OSSubVersionLabel = YString();
#pragma warning(push)
#pragma warning(disable : 4996) // 'function' was declared deprecated
	CA_SUPPRESS(28159)
		if (GetVersionEx((LPOSVERSIONINFO)&OsVersionInfo))
#pragma warning(pop)
		{
			bool bIsInvalidVersion = false;

			switch (OsVersionInfo.dwMajorVersion)
			{
			case 5:
				switch (OsVersionInfo.dwMinorVersion)
				{
				case 0:
					out_OSVersionLabel = TEXT("Windows 2000");
					if (OsVersionInfo.wProductType == VER_NT_WORKSTATION)
					{
						out_OSSubVersionLabel = TEXT("Professional");
					}
					else
					{
						if (OsVersionInfo.wSuiteMask & VER_SUITE_DATACENTER)
						{
							out_OSSubVersionLabel = TEXT("Datacenter Server");
						}
						else if (OsVersionInfo.wSuiteMask & VER_SUITE_ENTERPRISE)
						{
							out_OSSubVersionLabel = TEXT("Advanced Server");
						}
						else
						{
							out_OSSubVersionLabel = TEXT("Server");
						}
					}
					break;
				case 1:
					out_OSVersionLabel = TEXT("Windows XP");
					if (OsVersionInfo.wSuiteMask & VER_SUITE_PERSONAL)
					{
						out_OSSubVersionLabel = TEXT("Home Edition");
					}
					else
					{
						out_OSSubVersionLabel = TEXT("Professional");
					}
					break;
				case 2:
					if (GetSystemMetrics(SM_SERVERR2))
					{
						out_OSVersionLabel = TEXT("Windows Server 2003 R2");
					}
					else if (OsVersionInfo.wSuiteMask & VER_SUITE_STORAGE_SERVER)
					{
						out_OSVersionLabel = TEXT("Windows Storage Server 2003");
					}
					else if (OsVersionInfo.wSuiteMask & VER_SUITE_WH_SERVER)
					{
						out_OSVersionLabel = TEXT("Windows Home Server");
					}
					else if (OsVersionInfo.wProductType == VER_NT_WORKSTATION && SystemInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64)
					{
						out_OSVersionLabel = TEXT("Windows XP");
						out_OSSubVersionLabel = TEXT("Professional x64 Edition");
					}
					else
					{
						out_OSVersionLabel = TEXT("Windows Server 2003");
					}
					break;
				default:
					ErrorCode |= (int32)ERROR_UNKNOWNVERSION;
				}
				break;
			case 6:
				switch (OsVersionInfo.dwMinorVersion)
				{
				case 0:
					if (OsVersionInfo.wProductType == VER_NT_WORKSTATION)
					{
						out_OSVersionLabel = TEXT("Windows Vista");
					}
					else
					{
						out_OSVersionLabel = TEXT("Windows Server 2008");
					}
					break;
				case 1:
					if (OsVersionInfo.wProductType == VER_NT_WORKSTATION)
					{
						out_OSVersionLabel = TEXT("Windows 7");
					}
					else
					{
						out_OSVersionLabel = TEXT("Windows Server 2008 R2");
					}
					break;
				case 2:
					ErrorCode |= GetWindowsGT62Versions(OsVersionInfo.wProductType == VER_NT_WORKSTATION, out_OSVersionLabel);
					break;
				default:
					ErrorCode |= (int32)ERROR_UNKNOWNVERSION;
				}

				{
#pragma warning( push )
#pragma warning( disable: 4191 )	// unsafe conversion from 'type of expression' to 'type required'
					typedef BOOL(WINAPI *LPFN_GETPRODUCTINFO)(DWORD, DWORD, DWORD, DWORD, PDWORD);
					LPFN_GETPRODUCTINFO fnGetProductInfo = (LPFN_GETPRODUCTINFO)GetProcAddress(GetModuleHandle(TEXT("kernel32.dll")), "GetProductInfo");
#pragma warning( pop )
					if (fnGetProductInfo != NULL)
					{
						DWORD Type;
						fnGetProductInfo(OsVersionInfo.dwMajorVersion, OsVersionInfo.dwMinorVersion, 0, 0, &Type);

						switch (Type)
						{
						case PRODUCT_ULTIMATE:
							out_OSSubVersionLabel = TEXT("Ultimate Edition");
							break;
						case PRODUCT_PROFESSIONAL:
							out_OSSubVersionLabel = TEXT("Professional");
							break;
						case PRODUCT_HOME_PREMIUM:
							out_OSSubVersionLabel = TEXT("Home Premium Edition");
							break;
						case PRODUCT_HOME_BASIC:
							out_OSSubVersionLabel = TEXT("Home Basic Edition");
							break;
						case PRODUCT_ENTERPRISE:
							out_OSSubVersionLabel = TEXT("Enterprise Edition");
							break;
						case PRODUCT_BUSINESS:
							out_OSSubVersionLabel = TEXT("Business Edition");
							break;
						case PRODUCT_STARTER:
							out_OSSubVersionLabel = TEXT("Starter Edition");
							break;
						case PRODUCT_CLUSTER_SERVER:
							out_OSSubVersionLabel = TEXT("Cluster Server Edition");
							break;
						case PRODUCT_DATACENTER_SERVER:
							out_OSSubVersionLabel = TEXT("Datacenter Edition");
							break;
						case PRODUCT_DATACENTER_SERVER_CORE:
							out_OSSubVersionLabel = TEXT("Datacenter Edition (core installation)");
							break;
						case PRODUCT_ENTERPRISE_SERVER:
							out_OSSubVersionLabel = TEXT("Enterprise Edition");
							break;
						case PRODUCT_ENTERPRISE_SERVER_CORE:
							out_OSSubVersionLabel = TEXT("Enterprise Edition (core installation)");
							break;
						case PRODUCT_ENTERPRISE_SERVER_IA64:
							out_OSSubVersionLabel = TEXT("Enterprise Edition for Itanium-based Systems");
							break;
						case PRODUCT_SMALLBUSINESS_SERVER:
							out_OSSubVersionLabel = TEXT("Small Business Server");
							break;
						case PRODUCT_SMALLBUSINESS_SERVER_PREMIUM:
							out_OSSubVersionLabel = TEXT("Small Business Server Premium Edition");
							break;
						case PRODUCT_STANDARD_SERVER:
							out_OSSubVersionLabel = TEXT("Standard Edition");
							break;
						case PRODUCT_STANDARD_SERVER_CORE:
							out_OSSubVersionLabel = TEXT("Standard Edition (core installation)");
							break;
						case PRODUCT_WEB_SERVER:
							out_OSSubVersionLabel = TEXT("Web Server Edition");
							break;
						}
					}
					else
					{
						out_OSSubVersionLabel = TEXT("(type unknown)");
						ErrorCode |= (int32)ERROR_GETPRODUCTINFO_FAILED;
					}
				}
				break;
			default:
				ErrorCode |= ERROR_UNKNOWNVERSION;
			}

#if 0
			// THIS BIT ADDS THE SERVICE PACK INFO TO THE EDITION STRING
			// Append service pack info
			if (OsVersionInfo.szCSDVersion[0] != 0)
			{
				OSSubVersionLabel += YString::Printf(TEXT(" (%s)"), OsVersionInfo.szCSDVersion);
			}
#else
			// THIS BIT USES SERVICE PACK INFO ONLY
			out_OSSubVersionLabel = OsVersionInfo.szCSDVersion;
#endif
		}
		else
		{
			ErrorCode |= ERROR_GETVERSIONEX_FAILED;
		}

	return ErrorCode;
}
#include "Windows/HideWindowsPlatformTypes.h"

/**
* Whether support for integrating into the firewall is there
*/
#define WITH_FIREWALL_SUPPORT	0

extern "C"
{
	CORE_API Windows::HINSTANCE hInstance = NULL;
}


/** Original C- Runtime pure virtual call handler that is being called in the (highly likely) case of a double fault */
_purecall_handler DefaultPureCallHandler;

/**
* Our own pure virtual function call handler, set by appPlatformPreInit. Falls back
* to using the default C- Runtime handler in case of double faulting.
*/
static void PureCallHandler()
{
	static bool bHasAlreadyBeenCalled = false;
	YPlatformMisc::DebugBreak();
	if (bHasAlreadyBeenCalled)
	{
		// Call system handler if we're double faulting.
		if (DefaultPureCallHandler)
		{
			DefaultPureCallHandler();
		}
	}
	else
	{
		bHasAlreadyBeenCalled = true;
		if (GIsRunning)
		{
			FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("Core", "PureVirtualFunctionCalledWhileRunningApp", "Pure virtual function being called while application was running (GIsRunning == 1)."));
		}
		UE_LOG(LogWindows, Fatal, TEXT("Pure virtual function being called"));
	}
}

/*-----------------------------------------------------------------------------
SHA-1 functions.
-----------------------------------------------------------------------------*/

/**
* Get the hash values out of the executable hash section
*
* NOTE: hash keys are stored in the executable, you will need to put a line like the
*		 following into your PCLaunch.rc settings:
*			ID_HASHFILE RCDATA "../../../../GameName/Build/Hashes.sha"
*
*		 Then, use the -sha option to the cooker (must be from commandline, not
*       frontend) to generate the hashes for .ini, loc, startup packages, and .usf shader files
*
*		 You probably will want to make and checkin an empty file called Hashses.sha
*		 into your source control to avoid linker warnings. Then for testing or final
*		 final build ONLY, use the -sha command and relink your executable to put the
*		 hashes for the current files into the executable.
*/
static void InitSHAHashes()
{
	uint32 SectionSize = 0;
	void* SectionData = NULL;
	// find the resource for the file hash in the exe by ID
	HRSRC HashFileFindResH = FindResource(NULL, MAKEINTRESOURCE(ID_HASHFILE), RT_RCDATA);
	if (HashFileFindResH)
	{
		// load it
		HGLOBAL HashFileLoadResH = LoadResource(NULL, HashFileFindResH);
		if (!HashFileLoadResH)
		{
			FMessageDialog::ShowLastError();
		}
		else
		{
			// get size
			SectionSize = SizeofResource(NULL, HashFileFindResH);
			// get the data. no need to unlock it
			SectionData = (uint8*)LockResource(HashFileLoadResH);
		}
	}

	// there may be a dummy byte for platforms that can't handle empty files for linking
	if (SectionSize <= 1)
	{
		return;
	}

	// look for the hash section
	if (SectionData)
	{
		FSHA1::InitializeFileHashesFromBuffer((uint8*)SectionData, SectionSize);
	}
}

/**
*	Sets process memory limit using the job object, may fail under some situation like when Program Compatibility Assistant is enabled.
*	Debugging purpose only.
*/
static void SetProcessMemoryLimit(SIZE_T ProcessMemoryLimitMB)
{
	HANDLE JobObject = ::CreateJobObject(nullptr, TEXT("UE4-JobObject"));
	check(JobObject);
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION JobLimitInfo;
	FMemory::Memzero(JobLimitInfo);
	JobLimitInfo.ProcessMemoryLimit = 1024 * 1024 * ProcessMemoryLimitMB;
	JobLimitInfo.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_PROCESS_MEMORY;
	const BOOL bSetJob = ::SetInformationJobObject(JobObject, JobObjectExtendedLimitInformation, &JobLimitInfo, sizeof(JobLimitInfo));

	const BOOL bAssign = ::AssignProcessToJobObject(JobObject, GetCurrentProcess());
}

void YWindowsPlatformMisc::SetHighDPIMode()
{
	if (FParse::Param(FCommandLine::Get(), TEXT("enablehighdpi")))
	{
		if (void* ShCoreDll = FPlatformProcess::GetDllHandle(TEXT("shcore.dll")))
		{
			typedef HRESULT(STDAPICALLTYPE *SetProcessDpiAwarenessProc)(int32 Value);
			SetProcessDpiAwarenessProc SetProcessDpiAwareness = (SetProcessDpiAwarenessProc)FPlatformProcess::GetDllExport(ShCoreDll, TEXT("SetProcessDpiAwareness"));
			GetDpiForMonitor = (GetDpiForMonitorProc)FPlatformProcess::GetDllExport(ShCoreDll, TEXT("GetDpiForMonitor"));
			FPlatformProcess::FreeDllHandle(ShCoreDll);

			if (SetProcessDpiAwareness)
			{
				SetProcessDpiAwareness(2); // PROCESS_PER_MONITOR_DPI_AWARE_VALUE
			}
		}
		else if (void* User32Dll = FPlatformProcess::GetDllHandle(TEXT("user32.dll")))
		{
			typedef BOOL(WINAPI *SetProcessDpiAwareProc)(void);
			SetProcessDpiAwareProc SetProcessDpiAware = (SetProcessDpiAwareProc)FPlatformProcess::GetDllExport(User32Dll, TEXT("SetProcessDPIAware"));
			FPlatformProcess::FreeDllHandle(User32Dll);

			if (SetProcessDpiAware)
			{
				SetProcessDpiAware();
			}
		}
	}

}

float YWindowsPlatformMisc::GetDPIScaleFactorAtPoint(float X, float Y)
{
	if (FParse::Param(FCommandLine::Get(), TEXT("enablehighdpi")))
	{
		if (GetDpiForMonitor)
		{
			POINT Position = { (LONG)X, (LONG)Y };
			HMONITOR Monitor = MonitorFromPoint(Position, MONITOR_DEFAULTTONEAREST);
			if (Monitor)
			{
				uint32 DPIX = 0;
				uint32 DPIY = 0;
				return SUCCEEDED(GetDpiForMonitor(Monitor, 0/*MDT_EFFECTIVE_DPI_VALUE*/, &DPIX, &DPIY)) ? DPIX / 96.0f : 1.0f;
			}
		}
		else
		{
			HDC Context = GetDC(nullptr);
			const float DPIScaleFactor = GetDeviceCaps(Context, LOGPIXELSX) / 96.0f;
			ReleaseDC(nullptr, Context);
			return DPIScaleFactor;
		}
	}
	return 1.0f;
}

void YWindowsPlatformMisc::PlatformPreInit()
{
	//SetProcessMemoryLimit( 92 );

	YGenericPlatformMisc::PlatformPreInit();

	// Use our own handler for pure virtuals being called.
	DefaultPureCallHandler = _set_purecall_handler(PureCallHandler);

	const int32 MinResolution[] = { 640,480 };
	if (::GetSystemMetrics(SM_CXSCREEN) < MinResolution[0] || ::GetSystemMetrics(SM_CYSCREEN) < MinResolution[1])
	{
		FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("Launch", "Error_ResolutionTooLow", "The current resolution is too low to run this game."));
		YPlatformMisc::RequestExit(false);
	}

	// initialize the file SHA hash mapping
	InitSHAHashes();

	SetHighDPIMode();
}


void YWindowsPlatformMisc::PlatformInit()
{
#if defined(_MSC_VER) && _MSC_VER == 1800 && PLATFORM_64BITS
	// Work around bug in the VS 2013 math libraries in 64bit on certain windows versions. http://connect.microsoft.com/VisualStudio/feedback/details/811093 has details, remove this when runtime libraries are fixed
	_set_FMA3_enable(0);
#endif

	// Set granularity of sleep and such to 1 ms.
	timeBeginPeriod(1);

	// Identity.
	UE_LOG(LogInit, Log, TEXT("Computer: %s"), FPlatformProcess::ComputerName());
	UE_LOG(LogInit, Log, TEXT("User: %s"), FPlatformProcess::UserName());

	// Get CPU info.
	const YPlatformMemoryConstants& MemoryConstants = YPlatformMemory::GetConstants();
	UE_LOG(LogInit, Log, TEXT("CPU Page size=%i, Cores=%i"), MemoryConstants.PageSize, YPlatformMisc::NumberOfCores());

	// Timer resolution.
	UE_LOG(LogInit, Log, TEXT("High frequency timer resolution =%f MHz"), 0.000001 / FPlatformTime::GetSecondsPerCycle());

	// Register on the game thread.
	FWindowsPlatformStackWalk::RegisterOnModulesChanged();
}


void YWindowsPlatformMisc::PreventScreenSaver()
{
	INPUT Input = { 0 };
	Input.type = INPUT_MOUSE;
	Input.mi.dx = 0;
	Input.mi.dy = 0;
	Input.mi.mouseData = 0;
	Input.mi.dwFlags = MOUSEEVENTF_MOVE;
	Input.mi.time = 0;
	Input.mi.dwExtraInfo = 0;
	SendInput(1, &Input, sizeof(INPUT));
}



/**
* Handler called for console events like closure, CTRL-C, ...
*
* @param Type	unused
*/
static BOOL WINAPI ConsoleCtrlHandler(::DWORD /*Type*/)
{
	// make sure as much data is written to disk as possible
	if (GLog)
	{
		GLog->Flush();
	}
	if (GWarn)
	{
		GWarn->Flush();
	}
	if (GError)
	{
		GError->Flush();
	}

	// if we are running commandlet we want the application to exit immediately on control-c press
	if (!GIsRequestingExit && !IsRunningCommandlet())
	{
		PostQuitMessage(0);
		GIsRequestingExit = 1;
	}
	else
	{
		// User has pressed Ctrl-C twice and we should forcibly terminate the application.
		// ExitProcess would run global destructors, possibly causing assertions.
		TerminateProcess(GetCurrentProcess(), 0);
	}
	return true;
}

GenericApplication* YWindowsPlatformMisc::CreateApplication()
{
	HICON AppIconHandle = LoadIcon(hInstance, MAKEINTRESOURCE(GetAppIcon()));
	if (AppIconHandle == NULL)
	{
		AppIconHandle = LoadIcon((HINSTANCE)NULL, IDI_APPLICATION);
	}

	return FWindowsApplication::CreateWindowsApplication(hInstance, AppIconHandle);
}

void YWindowsPlatformMisc::SetGracefulTerminationHandler()
{
	// Set console control handler so we can exit if requested.
	SetConsoleCtrlHandler(ConsoleCtrlHandler, true);
}

void YWindowsPlatformMisc::GetEnvironmentVariable(const TCHAR* VariableName, TCHAR* Result, int32 ResultLength)
{
	uint32 Error = ::GetEnvironmentVariableW(VariableName, Result, ResultLength);
	if (Error <= 0)
	{
		*Result = 0;
	}
}

void YWindowsPlatformMisc::SetEnvironmentVar(const TCHAR* VariableName, const TCHAR* Value)
{
	uint32 Error = ::SetEnvironmentVariable(VariableName, Value);
	if (Error == 0)
	{
		UE_LOG(LogWindows, Warning, TEXT("Failed to set EnvironmentVariable: %s to : %s"), VariableName, Value);
	}
}

TArray<uint8> YWindowsPlatformMisc::GetMacAddress()
{
	TArray<uint8> Result;
	IP_ADAPTER_INFO IpAddresses[16];
	ULONG OutBufferLength = sizeof(IP_ADAPTER_INFO) * 16;
	// Read the adapters
	uint32 RetVal = GetAdaptersInfo(IpAddresses, &OutBufferLength);
	if (RetVal == NO_ERROR)
	{
		PIP_ADAPTER_INFO AdapterList = IpAddresses;
		// Walk the set of addresses copying each one
		while (AdapterList)
		{
			// If there is an address to read
			if (AdapterList->AddressLength > 0)
			{
				// Copy the data and say we did
				Result.AddZeroed(AdapterList->AddressLength);
				FMemory::Memcpy(Result.GetData(), AdapterList->Address, AdapterList->AddressLength);
				break;
			}
			AdapterList = AdapterList->Next;
		}
	}
	return Result;
}

/**
* We need to see if we are doing AutomatedPerfTesting and we are -unattended if we are then we have
* crashed in some terrible way and we need to make certain we can kill -9 the devenv process /
* vsjitdebugger.exe and any other processes that are still running
*/
static void HardKillIfAutomatedTesting()
{
	// so here 
	int32 FromCommandLine = 0;
	FParse::Value(FCommandLine::Get(), TEXT("AutomatedPerfTesting="), FromCommandLine);
	if ((FApp::IsUnattended() == true) && (FromCommandLine != 0) && (FParse::Param(FCommandLine::Get(), TEXT("KillAllPopUpBlockingWindows")) == true))
	{

		UE_LOG(LogWindows, Warning, TEXT("Attempting to run KillAllPopUpBlockingWindows"));

		TCHAR KillAllBlockingWindows[] = TEXT("KillAllPopUpBlockingWindows.bat");
		// .bat files never seem to launch correctly with FPlatformProcess::CreateProc so we just use the FPlatformProcess::LaunchURL which will call ShellExecute
		// we don't really care about the return code in this case 
		FPlatformProcess::LaunchURL(TEXT("KillAllPopUpBlockingWindows.bat"), NULL, NULL);
	}
}


void YWindowsPlatformMisc::SubmitErrorReport(const TCHAR* InErrorHist, EErrorReportMode::Type InMode)
{
	if ((!YPlatformMisc::IsDebuggerPresent() || GAlwaysReportCrash) && !FParse::Param(FCommandLine::Get(), TEXT("CrashForUAT")))
	{
		if (GUseCrashReportClient)
		{
			HardKillIfAutomatedTesting();
			return;
		}

		const uint32 MAX_STRING_LEN = 256;

		TCHAR ReportDumpVersion[] = TEXT("3");

		YString ReportDumpPath;
		{
			const TCHAR ReportDumpFilename[] = TEXT("UnrealAutoReportDump");
			ReportDumpPath = YPaths::CreateTempFilename(*YPaths::GameLogDir(), ReportDumpFilename, TEXT(".txt"));
		}

		FArchive * AutoReportFile = IFileManager::Get().CreateFileWriter(*ReportDumpPath, FILEWRITE_EvenIfReadOnly);
		if (AutoReportFile != NULL)
		{
			TCHAR CompName[MAX_STRING_LEN];
			FCString::Strncpy(CompName, FPlatformProcess::ComputerName(), MAX_STRING_LEN);
			TCHAR UserName[MAX_STRING_LEN];
			FCString::Strncpy(UserName, FPlatformProcess::UserName(), MAX_STRING_LEN);
			TCHAR GameName[MAX_STRING_LEN];
			FCString::Strncpy(GameName, *YString::Printf(TEXT("%s %s"), *FApp::GetBranchName(), FApp::GetGameName()), MAX_STRING_LEN);
			TCHAR PlatformName[MAX_STRING_LEN];
#if PLATFORM_64BITS
			FCString::Strncpy(PlatformName, TEXT("PC 64-bit"), MAX_STRING_LEN);
#else	//PLATFORM_64BITS
			FCString::Strncpy(PlatformName, TEXT("PC 32-bit"), MAX_STRING_LEN);
#endif	//PLATFORM_64BITS
			TCHAR CultureName[MAX_STRING_LEN];
			FCString::Strncpy(CultureName, *FInternationalization::Get().GetDefaultCulture()->GetName(), MAX_STRING_LEN);
			TCHAR SystemTime[MAX_STRING_LEN];
			FCString::Strncpy(SystemTime, *FDateTime::Now().ToString(), MAX_STRING_LEN);
			TCHAR EngineVersionStr[MAX_STRING_LEN];
			FCString::Strncpy(EngineVersionStr, *FEngineVersion::Current().ToString(), 256);

			TCHAR ChangelistVersionStr[MAX_STRING_LEN];
			int32 ChangelistFromCommandLine = 0;
			const bool bFoundAutomatedBenchMarkingChangelist = FParse::Value(FCommandLine::Get(), TEXT("-gABC="), ChangelistFromCommandLine);
			if (bFoundAutomatedBenchMarkingChangelist == true)
			{
				FCString::Strncpy(ChangelistVersionStr, *YString::FromInt(ChangelistFromCommandLine), MAX_STRING_LEN);
			}
			// we are not passing in the changelist to use so use the one that was stored in the ObjectVersion
			else
			{
				FCString::Strncpy(ChangelistVersionStr, *YString::FromInt(FEngineVersion::Current().GetChangelist()), MAX_STRING_LEN);
			}

			TCHAR CmdLine[2048];
			FCString::Strncpy(CmdLine, FCommandLine::Get(), ARRAY_COUNT(CmdLine));
			TCHAR BaseDir[260];
			FCString::Strncpy(BaseDir, FPlatformProcess::BaseDir(), ARRAY_COUNT(BaseDir));
			TCHAR separator = 0;

			TCHAR EngineMode[MAX_STRING_LEN];
			if (IsRunningCommandlet())
			{
				FCString::Strncpy(EngineMode, TEXT("Commandlet"), MAX_STRING_LEN);
			}
			else if (GIsEditor)
			{
				FCString::Strncpy(EngineMode, TEXT("Editor"), MAX_STRING_LEN);
			}
			else if (IsRunningDedicatedServer())
			{
				FCString::Strncpy(EngineMode, TEXT("Server"), MAX_STRING_LEN);
			}
			else
			{
				FCString::Strncpy(EngineMode, TEXT("Game"), MAX_STRING_LEN);
			}

			//build the report dump file
			AutoReportFile->Serialize(ReportDumpVersion, FCString::Strlen(ReportDumpVersion) * sizeof(TCHAR));
			AutoReportFile->Serialize(&separator, sizeof(TCHAR));
			AutoReportFile->Serialize(CompName, FCString::Strlen(CompName) * sizeof(TCHAR));
			AutoReportFile->Serialize(&separator, sizeof(TCHAR));
			AutoReportFile->Serialize(UserName, FCString::Strlen(UserName) * sizeof(TCHAR));
			AutoReportFile->Serialize(&separator, sizeof(TCHAR));
			AutoReportFile->Serialize(GameName, FCString::Strlen(GameName) * sizeof(TCHAR));
			AutoReportFile->Serialize(&separator, sizeof(TCHAR));
			AutoReportFile->Serialize(PlatformName, FCString::Strlen(PlatformName) * sizeof(TCHAR));
			AutoReportFile->Serialize(&separator, sizeof(TCHAR));
			AutoReportFile->Serialize(CultureName, FCString::Strlen(CultureName) * sizeof(TCHAR));
			AutoReportFile->Serialize(&separator, sizeof(TCHAR));
			AutoReportFile->Serialize(SystemTime, FCString::Strlen(SystemTime) * sizeof(TCHAR));
			AutoReportFile->Serialize(&separator, sizeof(TCHAR));
			AutoReportFile->Serialize(EngineVersionStr, FCString::Strlen(EngineVersionStr) * sizeof(TCHAR));
			AutoReportFile->Serialize(&separator, sizeof(TCHAR));
			AutoReportFile->Serialize(ChangelistVersionStr, FCString::Strlen(ChangelistVersionStr) * sizeof(TCHAR));
			AutoReportFile->Serialize(&separator, sizeof(TCHAR));
			AutoReportFile->Serialize(CmdLine, FCString::Strlen(CmdLine) * sizeof(TCHAR));
			AutoReportFile->Serialize(&separator, sizeof(TCHAR));
			AutoReportFile->Serialize(BaseDir, FCString::Strlen(BaseDir) * sizeof(TCHAR));
			AutoReportFile->Serialize(&separator, sizeof(TCHAR));

			TCHAR* NonConstErrorHist = const_cast< TCHAR* >(InErrorHist);
			AutoReportFile->Serialize(NonConstErrorHist, FCString::Strlen(NonConstErrorHist) * sizeof(TCHAR));

			AutoReportFile->Serialize(&separator, sizeof(TCHAR));
			AutoReportFile->Serialize(EngineMode, FCString::Strlen(EngineMode) * sizeof(TCHAR));
			AutoReportFile->Serialize(&separator, sizeof(TCHAR));
			AutoReportFile->Close();

			if (!GIsBuildMachine)
			{
				TCHAR AutoReportExe[] = TEXT("../../../Engine/Binaries/DotNET/AutoReporter.exe");

				YString IniDumpPath;
				if (!FApp::IsGameNameEmpty())
				{
					const TCHAR IniDumpFilename[] = TEXT("UnrealAutoReportIniDump");
					IniDumpPath = YPaths::CreateTempFilename(*YPaths::GameLogDir(), IniDumpFilename, TEXT(".txt"));
					//build the ini dump
					YOutputDeviceFile AutoReportIniFile(*IniDumpPath);
					GConfig->Dump(AutoReportIniFile);
					AutoReportIniFile.Flush();
					AutoReportIniFile.TearDown();
				}

				YString CrashVideoPath = YPaths::GameLogDir() + TEXT("CrashVideo.avi");

				//get the paths that the files will actually have been saved to
				YString UserIniDumpPath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(*IniDumpPath);
				YString LogDirectory = YPlatformOutputDevices::GetAbsoluteLogFilename();
				TCHAR CommandlineLogFile[MAX_SPRINTF] = TEXT("");

				YString UserLogFile = IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(*LogDirectory);
				YString UserReportDumpPath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(*ReportDumpPath);
				YString UserCrashVideoPath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(*CrashVideoPath);

				//start up the auto reporting app, passing the report dump file path, the games' log file,
				// the ini dump path, the minidump path, and the crashvideo path
				//protect against spaces in paths breaking them up on the commandline
				YString CallingCommandLine = YString::Printf(TEXT("%d \"%s\" \"%s\" \"%s\" \"%s\" \"%s\""),
					(uint32)(GetCurrentProcessId()), *UserReportDumpPath, *UserLogFile, *UserIniDumpPath,
					MiniDumpFilenameW, *UserCrashVideoPath);

				switch (InMode)
				{
				case EErrorReportMode::Unattended:
					CallingCommandLine += TEXT(" -unattended");
					break;

				case EErrorReportMode::Balloon:
					CallingCommandLine += TEXT(" -balloon");
					break;

				case EErrorReportMode::Interactive:
					break;
				}

				if (!FPlatformProcess::CreateProc(AutoReportExe, *CallingCommandLine, true, false, false, NULL, 0, NULL, NULL).IsValid())
				{
					UE_LOG(LogWindows, Warning, TEXT("Couldn't start up the Auto Reporting process!"));
					YPlatformMemory::DumpStats(*GWarn);
					FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(InErrorHist));
				}
			}

			HardKillIfAutomatedTesting();
		}
	}
}

#if !UE_BUILD_SHIPPING
bool YWindowsPlatformMisc::IsDebuggerPresent()
{
	return !GIgnoreDebugger && !!::IsDebuggerPresent();
}
#endif // UE_BUILD_SHIPPING

static void WinPumpMessages()
{
	{
		MSG Msg;
		while (PeekMessage(&Msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&Msg);
			DispatchMessage(&Msg);
		}
	}
}

static void WinPumpSentMessages()
{
	MSG Msg;
	PeekMessage(&Msg, NULL, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
}

void YWindowsPlatformMisc::PumpMessages(bool bFromMainLoop)
{
	if (!bFromMainLoop)
	{
		TGuardValue<bool> PumpMessageGuard(GPumpingMessagesOutsideOfMainLoop, true);
		// Process pending windows messages, which is necessary to the rendering thread in some rare cases where D3D
		// sends window messages (from IDXGISwapChain::Present) to the main thread owned viewport window.
		WinPumpSentMessages();
		return;
	}

	GPumpingMessagesOutsideOfMainLoop = false;
	WinPumpMessages();

	// Determine if application has focus
	bool HasFocus = FApp::UseVRFocus() ? FApp::HasVRFocus() : FWindowsPlatformProcess::IsThisApplicationForeground();

	// If editor thread doesn't have the focus, don't suck up too much CPU time.
	if (GIsEditor)
	{
		static bool HadFocus = 1;
		if (HadFocus && !HasFocus)
		{
			// Drop our priority to speed up whatever is in the foreground.
			SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
		}
		else if (HasFocus && !HadFocus)
		{
			// Boost our priority back to normal.
			SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
		}
		if (!HasFocus)
		{
			// Sleep for a bit to not eat up all CPU time.
			FPlatformProcess::Sleep(0.005f);
		}
		HadFocus = HasFocus;
	}

	// if its our window, allow sound, otherwise apply multiplier
	FApp::SetVolumeMultiplier(HasFocus ? 1.0f : FApp::GetUnfocusedVolumeMultiplier());
}

uint32 YWindowsPlatformMisc::GetCharKeyMap(uint32* KeyCodes, YString* KeFNames, uint32 MaxMappings)
{
	return YGenericPlatformMisc::GetStandardPrintableKeyMap(KeyCodes, KeFNames, MaxMappings, true, false);
}

uint32 YWindowsPlatformMisc::GetKeyMap(uint32* KeyCodes, YString* KeFNames, uint32 MaxMappings)
{
#define ADDKEYMAP(KeyCode, KeFName)		if (NumMappings<MaxMappings) { KeyCodes[NumMappings]=KeyCode; KeFNames[NumMappings]=KeFName; ++NumMappings; };

	uint32 NumMappings = 0;

	if (KeyCodes && KeFNames && (MaxMappings > 0))
	{
		ADDKEYMAP(VK_LBUTTON, TEXT("LeftMouseButton"));
		ADDKEYMAP(VK_RBUTTON, TEXT("RightMouseButton"));
		ADDKEYMAP(VK_MBUTTON, TEXT("MiddleMouseButton"));

		ADDKEYMAP(VK_XBUTTON1, TEXT("ThumbMouseButton"));
		ADDKEYMAP(VK_XBUTTON2, TEXT("ThumbMouseButton2"));

		ADDKEYMAP(VK_BACK, TEXT("BackSpace"));
		ADDKEYMAP(VK_TAB, TEXT("Tab"));
		ADDKEYMAP(VK_RETURN, TEXT("Enter"));
		ADDKEYMAP(VK_PAUSE, TEXT("Pause"));

		ADDKEYMAP(VK_CAPITAL, TEXT("CapsLock"));
		ADDKEYMAP(VK_ESCAPE, TEXT("Escape"));
		ADDKEYMAP(VK_SPACE, TEXT("SpaceBar"));
		ADDKEYMAP(VK_PRIOR, TEXT("PageUp"));
		ADDKEYMAP(VK_NEXT, TEXT("PageDown"));
		ADDKEYMAP(VK_END, TEXT("End"));
		ADDKEYMAP(VK_HOME, TEXT("Home"));

		ADDKEYMAP(VK_LEFT, TEXT("Left"));
		ADDKEYMAP(VK_UP, TEXT("Up"));
		ADDKEYMAP(VK_RIGHT, TEXT("Right"));
		ADDKEYMAP(VK_DOWN, TEXT("Down"));

		ADDKEYMAP(VK_INSERT, TEXT("Insert"));
		ADDKEYMAP(VK_DELETE, TEXT("Delete"));

		ADDKEYMAP(VK_NUMPAD0, TEXT("NumPadZero"));
		ADDKEYMAP(VK_NUMPAD1, TEXT("NumPadOne"));
		ADDKEYMAP(VK_NUMPAD2, TEXT("NumPadTwo"));
		ADDKEYMAP(VK_NUMPAD3, TEXT("NumPadThree"));
		ADDKEYMAP(VK_NUMPAD4, TEXT("NumPadFour"));
		ADDKEYMAP(VK_NUMPAD5, TEXT("NumPadFive"));
		ADDKEYMAP(VK_NUMPAD6, TEXT("NumPadSix"));
		ADDKEYMAP(VK_NUMPAD7, TEXT("NumPadSeven"));
		ADDKEYMAP(VK_NUMPAD8, TEXT("NumPadEight"));
		ADDKEYMAP(VK_NUMPAD9, TEXT("NumPadNine"));

		ADDKEYMAP(VK_MULTIPLY, TEXT("Multiply"));
		ADDKEYMAP(VK_ADD, TEXT("Add"));
		ADDKEYMAP(VK_SUBTRACT, TEXT("Subtract"));
		ADDKEYMAP(VK_DECIMAL, TEXT("Decimal"));
		ADDKEYMAP(VK_DIVIDE, TEXT("Divide"));

		ADDKEYMAP(VK_F1, TEXT("F1"));
		ADDKEYMAP(VK_F2, TEXT("F2"));
		ADDKEYMAP(VK_F3, TEXT("F3"));
		ADDKEYMAP(VK_F4, TEXT("F4"));
		ADDKEYMAP(VK_F5, TEXT("F5"));
		ADDKEYMAP(VK_F6, TEXT("F6"));
		ADDKEYMAP(VK_F7, TEXT("F7"));
		ADDKEYMAP(VK_F8, TEXT("F8"));
		ADDKEYMAP(VK_F9, TEXT("F9"));
		ADDKEYMAP(VK_F10, TEXT("F10"));
		ADDKEYMAP(VK_F11, TEXT("F11"));
		ADDKEYMAP(VK_F12, TEXT("F12"));

		ADDKEYMAP(VK_NUMLOCK, TEXT("NumLock"));

		ADDKEYMAP(VK_SCROLL, TEXT("ScrollLock"));

		ADDKEYMAP(VK_LSHIFT, TEXT("LeftShift"));
		ADDKEYMAP(VK_RSHIFT, TEXT("RightShift"));
		ADDKEYMAP(VK_LCONTROL, TEXT("LeftControl"));
		ADDKEYMAP(VK_RCONTROL, TEXT("RightControl"));
		ADDKEYMAP(VK_LMENU, TEXT("LeftAlt"));
		ADDKEYMAP(VK_RMENU, TEXT("RightAlt"));
		ADDKEYMAP(VK_LWIN, TEXT("LeftCommand"));
		ADDKEYMAP(VK_RWIN, TEXT("RightCommand"));

		TMap<uint32, uint32> ScanToVKMap;
#define MAP_OEM_VK_TO_SCAN(KeyCode) { const uint32 CharCode = MapVirtualKey(KeyCode,2); if (CharCode != 0) { ScanToVKMap.Add(CharCode,KeyCode); } }
		MAP_OEM_VK_TO_SCAN(VK_OEM_1);
		MAP_OEM_VK_TO_SCAN(VK_OEM_2);
		MAP_OEM_VK_TO_SCAN(VK_OEM_3);
		MAP_OEM_VK_TO_SCAN(VK_OEM_4);
		MAP_OEM_VK_TO_SCAN(VK_OEM_5);
		MAP_OEM_VK_TO_SCAN(VK_OEM_6);
		MAP_OEM_VK_TO_SCAN(VK_OEM_7);
		MAP_OEM_VK_TO_SCAN(VK_OEM_8);
		MAP_OEM_VK_TO_SCAN(VK_OEM_PLUS);
		MAP_OEM_VK_TO_SCAN(VK_OEM_COMMA);
		MAP_OEM_VK_TO_SCAN(VK_OEM_MINUS);
		MAP_OEM_VK_TO_SCAN(VK_OEM_PERIOD);
		MAP_OEM_VK_TO_SCAN(VK_OEM_102);
#undef  MAP_OEM_VK_TO_SCAN

		static const uint32 MAX_KEY_MAPPINGS(256);
		uint32 CharCodes[MAX_KEY_MAPPINGS];
		YString CharKeFNames[MAX_KEY_MAPPINGS];
		const int32 CharMappings = GetCharKeyMap(CharCodes, CharKeFNames, MAX_KEY_MAPPINGS);

		for (int32 MappingIndex = 0; MappingIndex < CharMappings; ++MappingIndex)
		{
			ScanToVKMap.Remove(CharCodes[MappingIndex]);
		}

		for (auto It(ScanToVKMap.CreateConstIterator()); It; ++It)
		{
			ADDKEYMAP(It.Value(), YString::Chr(It.Key()));
		}
	}

	check(NumMappings < MaxMappings);
	return NumMappings;

#undef ADDKEYMAP
}

void YWindowsPlatformMisc::SetUTF8Output()
{
	CA_SUPPRESS(6031)
		_setmode(_fileno(stdout), _O_U8TEXT);
}

void YWindowsPlatformMisc::LocalPrint(const TCHAR *Message)
{
	OutputDebugString(Message);
}

void YWindowsPlatformMisc::RequestExit(bool Force)
{
	UE_LOG(LogWindows, Log, TEXT("YPlatformMisc::RequestExit(%i)"), Force);

	FCoreDelegates::ApplicationWillTerminateDelegate.Broadcast();

	if (Force)
	{
		// Force immediate exit. In case of an error set the exit code to 3.
		// Dangerous because config code isn't flushed, global destructors aren't called, etc.
		// Suppress abort message and MS reports.
		//_set_abort_behavior( 0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT );
		//abort();

		// Make sure the log is flushed.
		if (GLog)
		{
			// This may be called from other thread, so set this thread as the master.
			GLog->SetCurrentThreadAsMasterThread();
			GLog->TearDown();
		}

		TerminateProcess(GetCurrentProcess(), GIsCriticalError ? 3 : 0);
	}
	else
	{
		// Tell the platform specific code we want to exit cleanly from the main loop.
		PostQuitMessage(0);
		GIsRequestingExit = 1;
	}
}

void YWindowsPlatformMisc::RequestMinimize()
{
	::ShowWindow(::GetActiveWindow(), SW_MINIMIZE);
}

const TCHAR* YWindowsPlatformMisc::GetSystemErrorMessage(TCHAR* OutBuffer, int32 BufferCount, int32 Error)
{
	check(OutBuffer && BufferCount);
	*OutBuffer = TEXT('\0');
	if (Error == 0)
	{
		Error = GetLastError();
	}
	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, Error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), OutBuffer, BufferCount, NULL);
	TCHAR* Found = FCString::Strchr(OutBuffer, TEXT('\r'));
	if (Found)
	{
		*Found = TEXT('\0');
	}
	Found = FCString::Strchr(OutBuffer, TEXT('\n'));
	if (Found)
	{
		*Found = TEXT('\0');
	}
	return OutBuffer;
}

// Disabling optimizations helps to reduce the frequency of OpenClipboard failing with error code 0. It still happens
// though only with really large text buffers and we worked around this by changing the editor to use an intermediate
// text buffer for internal operations.
PRAGMA_DISABLE_OPTIMIZATION

void YWindowsPlatformMisc::ClipboardCopy(const TCHAR* Str)
{
	if (OpenClipboard(GetActiveWindow()))
	{
		verify(EmptyClipboard());
		HGLOBAL GlobalMem;
		int32 StrLen = FCString::Strlen(Str);
		GlobalMem = GlobalAlloc(GMEM_MOVEABLE, sizeof(TCHAR)*(StrLen + 1));
		check(GlobalMem);
		TCHAR* Data = (TCHAR*)GlobalLock(GlobalMem);
		FCString::Strcpy(Data, (StrLen + 1), Str);
		GlobalUnlock(GlobalMem);
		if (SetClipboardData(CF_UNICODETEXT, GlobalMem) == NULL)
			UE_LOG(LogWindows, Fatal, TEXT("SetClipboardData failed with error code %i"), (uint32)GetLastError());
		verify(CloseClipboard());
	}
}

void YWindowsPlatformMisc::ClipboardPaste(class YString& Result)
{
	if (OpenClipboard(GetActiveWindow()))
	{
		HGLOBAL GlobalMem = NULL;
		bool Unicode = 0;
		GlobalMem = GetClipboardData(CF_UNICODETEXT);
		Unicode = 1;
		if (!GlobalMem)
		{
			GlobalMem = GetClipboardData(CF_TEXT);
			Unicode = 0;
		}
		if (!GlobalMem)
		{
			Result = TEXT("");
		}
		else
		{
			void* Data = GlobalLock(GlobalMem);
			check(Data);
			if (Unicode)
				Result = (TCHAR*)Data;
			else
			{
				ANSICHAR* ACh = (ANSICHAR*)Data;
				int32 i;
				for (i = 0; ACh[i]; i++);
				TArray<TCHAR> Ch;
				Ch.AddUninitialized(i + 1);
				for (i = 0; i<Ch.Num(); i++)
					Ch[i] = CharCast<TCHAR>(ACh[i]);
				Result.GetCharArray() = MoveTemp(Ch);
			}
			GlobalUnlock(GlobalMem);
		}
		verify(CloseClipboard());
	}
	else
	{
		Result = TEXT("");
	}
}

PRAGMA_ENABLE_OPTIMIZATION

void YWindowsPlatformMisc::CreateGuid(FGuid& Result)
{
	verify(CoCreateGuid((GUID*)&Result) == S_OK);
}


#define HOTKEY_YES			100
#define HOTKEY_NO			101
#define HOTKEY_CANCEL		102

/**
* Helper global variables, used in MessageBoxDlgProc for set message text.
*/
static TCHAR* GMessageBoxText = NULL;
static TCHAR* GMessageBoxCaption = NULL;
/**
* Used by MessageBoxDlgProc to indicate whether a 'Cancel' button is present and
* thus 'Esc should be accepted as a hotkey.
*/
static bool GCancelButtonEnabled = false;

/**
* Calculates button position and size, localize button text.
* @param HandleWnd handle to dialog window
* @param Text button text to localize
* @param DlgItemId dialog item id
* @param PositionX current button position (x coord)
* @param PositionY current button position (y coord)
* @return true if succeeded
*/
static bool SetDlgItem(HWND HandleWnd, const TCHAR* Text, int32 DlgItemId, int32* PositionX, int32* PositionY)
{
	SIZE SizeButton;

	HDC DC = CreateCompatibleDC(NULL);
	GetTextExtentPoint32(DC, Text, wcslen(Text), &SizeButton);
	DeleteDC(DC);
	DC = NULL;

	SizeButton.cx += 14;
	SizeButton.cy += 8;

	HWND Handle = GetDlgItem(HandleWnd, DlgItemId);
	if (Handle)
	{
		*PositionX -= (SizeButton.cx + 5);
		SetWindowPos(Handle, HWND_TOP, *PositionX, *PositionY - SizeButton.cy, SizeButton.cx, SizeButton.cy, 0);
		SetDlgItemText(HandleWnd, DlgItemId, Text);

		return true;
	}

	return false;
}

/**
* Callback for MessageBoxExt dialog (allowing for Yes to all / No to all )
* @return		One of EAppReturnType::Yes, EAppReturnType::YesAll, EAppReturnType::No, EAppReturnType::NoAll, EAppReturnType::Cancel.
*/
PTRINT CALLBACK MessageBoxDlgProc(HWND HandleWnd, uint32 Message, WPARAM WParam, LPARAM LParam)
{
	switch (Message)
	{
	case WM_INITDIALOG:
	{
		// Sets most bottom and most right position to begin button placement
		RECT Rect;
		POINT Point;

		GetWindowRect(HandleWnd, &Rect);
		Point.x = Rect.right;
		Point.y = Rect.bottom;
		ScreenToClient(HandleWnd, &Point);

		int32 PositionX = Point.x - 8;
		int32 PositionY = Point.y - 10;

		// Localize dialog buttons, sets position and size.
		YString CancelString;
		YString NoToAllString;
		YString NoString;
		YString YesToAllString;
		YString YesString;

		// The Localize* functions will return the Key if a dialog is presented before the config system is initialized.
		// Instead, we use hard-coded strings if config is not yet initialized.
		if (!GConfig)
		{
			CancelString = TEXT("Cancel");
			NoToAllString = TEXT("No to All");
			NoString = TEXT("No");
			YesToAllString = TEXT("Yes to All");
			YesString = TEXT("Yes");
		}
		else
		{
			CancelString = NSLOCTEXT("UnrealEd", "Cancel", "Cancel").ToString();
			NoToAllString = NSLOCTEXT("UnrealEd", "NoToAll", "No to All").ToString();
			NoString = NSLOCTEXT("UnrealEd", "No", "No").ToString();
			YesToAllString = NSLOCTEXT("UnrealEd", "YesToAll", "Yes to All").ToString();
			YesString = NSLOCTEXT("UnrealEd", "Yes", "Yes").ToString();
		}
		SetDlgItem(HandleWnd, *CancelString, IDC_CANCEL, &PositionX, &PositionY);
		SetDlgItem(HandleWnd, *NoToAllString, IDC_NOTOALL, &PositionX, &PositionY);
		SetDlgItem(HandleWnd, *NoString, IDC_NO_B, &PositionX, &PositionY);
		SetDlgItem(HandleWnd, *YesToAllString, IDC_YESTOALL, &PositionX, &PositionY);
		SetDlgItem(HandleWnd, *YesString, IDC_YES, &PositionX, &PositionY);

		SetDlgItemText(HandleWnd, IDC_MESSAGE, GMessageBoxText);
		SetWindowText(HandleWnd, GMessageBoxCaption);

		// If parent window exist, get it handle and make it foreground.
		HWND ParentWindow = GetTopWindow(HandleWnd);
		if (ParentWindow)
		{
			SetWindowPos(ParentWindow, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		}

		SetForegroundWindow(HandleWnd);
		SetWindowPos(HandleWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

		RegisterHotKey(HandleWnd, HOTKEY_YES, 0, 'Y');
		RegisterHotKey(HandleWnd, HOTKEY_NO, 0, 'N');
		if (GCancelButtonEnabled)
		{
			RegisterHotKey(HandleWnd, HOTKEY_CANCEL, 0, VK_ESCAPE);
		}

		// Windows are foreground, make them not top most.
		SetWindowPos(HandleWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		if (ParentWindow)
		{
			SetWindowPos(ParentWindow, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		}

		return true;
	}
	case WM_DESTROY:
	{
		UnregisterHotKey(HandleWnd, HOTKEY_YES);
		UnregisterHotKey(HandleWnd, HOTKEY_NO);
		if (GCancelButtonEnabled)
		{
			UnregisterHotKey(HandleWnd, HOTKEY_CANCEL);
		}
		return true;
	}
	case WM_COMMAND:
		switch (LOWORD(WParam))
		{
		case IDC_YES:
			EndDialog(HandleWnd, EAppReturnType::Yes);
			break;
		case IDC_YESTOALL:
			EndDialog(HandleWnd, EAppReturnType::YesAll);
			break;
		case IDC_NO_B:
			EndDialog(HandleWnd, EAppReturnType::No);
			break;
		case IDC_NOTOALL:
			EndDialog(HandleWnd, EAppReturnType::NoAll);
			break;
		case IDC_CANCEL:
			if (GCancelButtonEnabled)
			{
				EndDialog(HandleWnd, EAppReturnType::Cancel);
			}
			break;
		}
		break;
	case WM_HOTKEY:
		switch (WParam)
		{
		case HOTKEY_YES:
			EndDialog(HandleWnd, EAppReturnType::Yes);
			break;
		case HOTKEY_NO:
			EndDialog(HandleWnd, EAppReturnType::No);
			break;
		case HOTKEY_CANCEL:
			if (GCancelButtonEnabled)
			{
				EndDialog(HandleWnd, EAppReturnType::Cancel);
			}
			break;
		}
		break;
	default:
		return false;
	}
	return true;
}

/**
* Displays extended message box allowing for YesAll/NoAll
* @return 3 - YesAll, 4 - NoAll, -1 for Fail
*/
int MessageBoxExtInternal(EAppMsgType::Type MsgType, HWND HandleWnd, const TCHAR* Text, const TCHAR* Caption)
{
	GMessageBoxText = (TCHAR *)Text;
	GMessageBoxCaption = (TCHAR *)Caption;

	if (MsgType == EAppMsgType::YesNoYesAllNoAll)
	{
		GCancelButtonEnabled = false;
		return DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_YESNO2ALL), HandleWnd, MessageBoxDlgProc);
	}
	else if (MsgType == EAppMsgType::YesNoYesAllNoAllCancel)
	{
		GCancelButtonEnabled = true;
		return DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_YESNO2ALLCANCEL), HandleWnd, MessageBoxDlgProc);
	}

	return -1;
}




EAppReturnType::Type YWindowsPlatformMisc::MessageBoxExt(EAppMsgType::Type MsgType, const TCHAR* Text, const TCHAR* Caption)
{
	FSlowHeartBeatScope SuspendHeartBeat;

	HWND ParentWindow = (HWND)NULL;
	switch (MsgType)
	{
	case EAppMsgType::YesNo:
	{
		int32 Return = MessageBox(ParentWindow, Text, Caption, MB_YESNO | MB_SYSTEMMODAL);
		return Return == IDYES ? EAppReturnType::Yes : EAppReturnType::No;
	}
	case EAppMsgType::OkCancel:
	{
		int32 Return = MessageBox(ParentWindow, Text, Caption, MB_OKCANCEL | MB_SYSTEMMODAL);
		return Return == IDOK ? EAppReturnType::Ok : EAppReturnType::Cancel;
	}
	case EAppMsgType::YesNoCancel:
	{
		int32 Return = MessageBox(ParentWindow, Text, Caption, MB_YESNOCANCEL | MB_ICONQUESTION | MB_SYSTEMMODAL);
		return Return == IDYES ? EAppReturnType::Yes : (Return == IDNO ? EAppReturnType::No : EAppReturnType::Cancel);
	}
	case EAppMsgType::CancelRetryContinue:
	{
		int32 Return = MessageBox(ParentWindow, Text, Caption, MB_CANCELTRYCONTINUE | MB_ICONQUESTION | MB_DEFBUTTON2 | MB_SYSTEMMODAL);
		return Return == IDCANCEL ? EAppReturnType::Cancel : (Return == IDTRYAGAIN ? EAppReturnType::Retry : EAppReturnType::Continue);
	}
	break;
	case EAppMsgType::YesNoYesAllNoAll:
		return (EAppReturnType::Type)MessageBoxExtInternal(EAppMsgType::YesNoYesAllNoAll, ParentWindow, Text, Caption);
		//These return codes just happen to match up with ours.
		// return 0 for No, 1 for Yes, 2 for YesToAll, 3 for NoToAll
		break;
	case EAppMsgType::YesNoYesAllNoAllCancel:
		return (EAppReturnType::Type)MessageBoxExtInternal(EAppMsgType::YesNoYesAllNoAllCancel, ParentWindow, Text, Caption);
		//These return codes just happen to match up with ours.
		// return 0 for No, 1 for Yes, 2 for YesToAll, 3 for NoToAll, 4 for Cancel
		break;
	default:
		MessageBox(ParentWindow, Text, Caption, MB_OK | MB_SYSTEMMODAL);
		break;
	}
	return EAppReturnType::Cancel;
}

static bool HandleGameExplorerIntegration()
{
	// skip this if running on WindowsServer (we get rare crashes that seem to stem from Windows Server builds, where GameExplorer isn't particularly useful)
	if (FPlatformProperties::SupportsWindowedMode() && !IsWindowsServer())
	{
		TCHAR AppPath[MAX_PATH];
		GetModuleFileName(NULL, AppPath, MAX_PATH - 1);

		// Initialize COM. We only want to do this once and not override settings of previous calls.
		if (!YWindowsPlatformMisc::CoInitialize())
		{
			return false;
		}

		// check to make sure we are able to run, based on parental rights
		IGameExplorer* GameExp;
		HRESULT hr = CoCreateInstance(__uuidof(GameExplorer), NULL, CLSCTX_INPROC_SERVER, __uuidof(IGameExplorer), (void**)&GameExp);

		BOOL bHasAccess = 1;
		BSTR AppPathBSTR = SysAllocString(AppPath);

		// @todo: This will allow access if the CoCreateInstance fails, but it should probaly disallow 
		// access if OS is Vista and it fails, succeed for XP
		if (SUCCEEDED(hr) && GameExp)
		{
			GameExp->VerifyAccess(AppPathBSTR, &bHasAccess);
		}


		// Guid for testing GE (un)installation
		static const GUID GEGuid =
		{ 0x7089dd1d, 0xfe97, 0x4cc8,{ 0x8a, 0xac, 0x26, 0x3e, 0x44, 0x1f, 0x3c, 0x42 } };

		// add the game to the game explorer if desired
		if (FParse::Param(FCommandLine::Get(), TEXT("installge")))
		{
			if (bHasAccess && GameExp)
			{
				BSTR AppDirBSTR = SysAllocString(FPlatformProcess::BaseDir());
				GUID Guid = GEGuid;
				hr = GameExp->AddGame(AppPathBSTR, AppDirBSTR, FParse::Param(FCommandLine::Get(), TEXT("allusers")) ? GIS_ALL_USERS : GIS_CURRENT_USER, &Guid);

				bool bWasSuccessful = false;
				// if successful
				if (SUCCEEDED(hr))
				{
					// get location of app local dir
					TCHAR UserPath[MAX_PATH];
					SHGetFolderPath(NULL, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, NULL, SHGFP_TYPE_CURRENT, UserPath);

					// convert guid to a string
					TCHAR GuidDir[MAX_PATH];
					CA_SUPPRESS(6031)
						StringFromGUID2(GEGuid, GuidDir, MAX_PATH - 1);

					// make the base path for all tasks
					YString BaseTaskDirectory = YString(UserPath) + TEXT("\\Microsoft\\Windows\\GameExplorer\\") + GuidDir;

					// make full paths for play and support tasks
					YString PlayTaskDirectory = BaseTaskDirectory + TEXT("\\PlayTasks");
					YString SupportTaskDirectory = BaseTaskDirectory + TEXT("\\SupportTasks");

					// make sure they exist
					IFileManager::Get().MakeDirectory(*PlayTaskDirectory, true);
					IFileManager::Get().MakeDirectory(*SupportTaskDirectory, true);

					// interface for creating a shortcut
					IShellLink* Link;
					hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (void**)&Link);

					// get the persistent file interface of the link
					IPersistFile* LinkFile;
					Link->QueryInterface(IID_IPersistFile, (void**)&LinkFile);

					Link->SetPath(AppPath);

					// create all of our tasks

					// first is just the game
					Link->SetArguments(TEXT(""));
					Link->SetDescription(TEXT("Play"));
					IFileManager::Get().MakeDirectory(*(PlayTaskDirectory + TEXT("\\0")), true);
					LinkFile->Save(*(PlayTaskDirectory + TEXT("\\0\\Play.lnk")), true);

					Link->SetArguments(TEXT("editor"));
					Link->SetDescription(TEXT("Editor"));
					IFileManager::Get().MakeDirectory(*(PlayTaskDirectory + TEXT("\\1")), true);
					LinkFile->Save(*(PlayTaskDirectory + TEXT("\\1\\Editor.lnk")), true);

					LinkFile->Release();
					Link->Release();

					IUniformResourceLocator* InternetLink;
					CA_SUPPRESS(6031)
						CoCreateInstance(CLSID_InternetShortcut, NULL,
							CLSCTX_INPROC_SERVER, IID_IUniformResourceLocator, (LPVOID*)&InternetLink);

					InternetLink->QueryInterface(IID_IPersistFile, (void**)&LinkFile);

					// make an internet shortcut
					InternetLink->SetURL(TEXT("http://www.unrealtournament3.com/"), 0);
					IFileManager::Get().MakeDirectory(*(SupportTaskDirectory + TEXT("\\0")), true);
					LinkFile->Save(*(SupportTaskDirectory + TEXT("\\0\\UT3.url")), true);

					LinkFile->Release();
					InternetLink->Release();
				}

				if (SUCCEEDED(hr))
				{
					FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("WindowsPlatform", "GameExplorerInstallationSuccessful", "GameExplorer installation was successful, quitting now."));
				}
				else
				{
					FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("WindowsPlatform", "GameExplorerInstallationFailed", "GameExplorer installation was a failure, quitting now."));
				}

				SysFreeString(AppDirBSTR);
			}
			else
			{
				FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("WindowsPlatform", "GameExplorerInstallationFailedDoToAccessPermissions", "GameExplorer installation failed because you don't have access (check parental control levels and that you are running XP). You should not need Admin access"));
			}

			// free the string and shutdown COM
			SysFreeString(AppPathBSTR);
			SAFE_RELEASE(GameExp);
			YWindowsPlatformMisc::CoUninitialize();

			return false;
		}
		else if (FParse::Param(FCommandLine::Get(), TEXT("uninstallge")))
		{
			if (GameExp)
			{
				hr = GameExp->RemoveGame(GEGuid);
				if (SUCCEEDED(hr))
				{
					FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("WindowsPlatform", "GameExplorerUninstallationSuccessful", "GameExplorer uninstallation was successful, quitting now."));
				}
				else
				{
					FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("WindowsPlatform", "GameExplorerUninstallationFailed", "GameExplorer uninstallation was a failure, quitting now."));
				}
			}
			else
			{
				FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("WindowsPlatform", "GameExplorerUninstallationFailedDoToNotRunningVista", "GameExplorer uninstallation failed because you are probably not running Vista."));
			}

			// free the string and shutdown COM
			SysFreeString(AppPathBSTR);
			SAFE_RELEASE(GameExp);
			YWindowsPlatformMisc::CoUninitialize();

			return false;
		}

		// free the string and shutdown COM
		SysFreeString(AppPathBSTR);
		SAFE_RELEASE(GameExp);
		YWindowsPlatformMisc::CoUninitialize();

		// if we don't have access, we must quit ASAP after showing a message
		if (!bHasAccess)
		{
			FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("UnrealEd", "Error_ParentalControls", "The current level of parental controls do not allow you to run this game."));
			return false;
		}
	}
	return true;
}

#if WITH_FIREWALL_SUPPORT
/**
* Get the INetFwProfile interface for current profile
*/
INetFwProfile* GetFirewallProfile(void)
{
	HRESULT hr;
	INetFwMgr* pFwMgr = NULL;
	INetFwPolicy* pFwPolicy = NULL;
	INetFwProfile* pFwProfile = NULL;

	// Create an instance of the Firewall settings manager
	hr = CoCreateInstance(__uuidof(NetFwMgr), NULL, CLSCTX_INPROC_SERVER, __uuidof(INetFwMgr), (void**)&pFwMgr);
	if (SUCCEEDED(hr))
	{
		hr = pFwMgr->get_LocalPolicy(&pFwPolicy);
		if (SUCCEEDED(hr))
		{
			pFwPolicy->get_CurrentProfile(&pFwProfile);
		}
	}

	// Cleanup
	if (pFwPolicy)
	{
		pFwPolicy->Release();
	}
	if (pFwMgr)
	{
		pFwMgr->Release();
	}

	return(pFwProfile);
}
#endif

static bool HandleFirewallIntegration()
{
#if WITH_FIREWALL_SUPPORT
	// only do with with the given commandlines
	if (!(FParse::Param(FCommandLine::Get(), TEXT("installfw")) || FParse::Param(FCommandLine::Get(), TEXT("uninstallfw"))))
#endif
	{
		return true; // allow the game to continue;
	}
#if WITH_FIREWALL_SUPPORT

	TCHAR AppPath[MAX_PATH];

	GetModuleFileName(NULL, AppPath, MAX_PATH - 1);
	BSTR bstrGameExeFullPath = SysAllocString(AppPath);
	BSTR bstrFriendlyAppName = SysAllocString(TEXT("Unreal Tournament 3"));

	if (bstrGameExeFullPath && bstrFriendlyAppName)
	{
		HRESULT hr = S_OK;

		if (YWindowsPlatformMisc::CoInitialize())
		{
			INetFwProfile* pFwProfile = GetFirewallProfile();
			if (pFwProfile)
			{
				INetFwAuthorizedApplications* pFwApps = NULL;

				hr = pFwProfile->get_AuthorizedApplications(&pFwApps);
				if (SUCCEEDED(hr) && pFwApps)
				{
					// add the game to the game explorer if desired
					if (FParse::Param(CmdLine, TEXT("installfw")))
					{
						INetFwAuthorizedApplication* pFwApp = NULL;

						// Create an instance of an authorized application.
						hr = CoCreateInstance(__uuidof(NetFwAuthorizedApplication), NULL, CLSCTX_INPROC_SERVER, __uuidof(INetFwAuthorizedApplication), (void**)&pFwApp);
						if (SUCCEEDED(hr) && pFwApp)
						{
							// Set the process image file name.
							hr = pFwApp->put_ProcessImageFileName(bstrGameExeFullPath);
							if (SUCCEEDED(hr))
							{
								// Set the application friendly name.
								hr = pFwApp->put_Name(bstrFriendlyAppName);
								if (SUCCEEDED(hr))
								{
									// Add the application to the collection.
									hr = pFwApps->Add(pFwApp);
								}
							}

							pFwApp->Release();
						}
					}
					else if (FParse::Param(CmdLine, TEXT("uninstallfw")))
					{
						// Remove the application from the collection.
						hr = pFwApps->Remove(bstrGameExeFullPath);
					}

					pFwApps->Release();
				}

				pFwProfile->Release();
			}

			YWindowsPlatformMisc::CoUninitialize();
		}

		SysFreeString(bstrFriendlyAppName);
		SysFreeString(bstrGameExeFullPath);
	}
	return false; // terminate the game
#endif // WITH_FIREWALL_SUPPORT
}

static bool HandleFirstInstall()
{
	if (FParse::Param(FCommandLine::Get(), TEXT("firstinstall")))
	{
		GLog->Flush();

		// Flush config to ensure culture changes are written to disk.
		GConfig->Flush(false);

		return false; // terminate the game
	}
	return true; // allow the game to continue;
}

bool YWindowsPlatformMisc::CommandLineCommands()
{
	return HandleFirstInstall() && HandleGameExplorerIntegration() && HandleFirewallIntegration();
}

/**
* Detects whether we're running in a 64-bit operating system.
*
* @return	true if we're running in a 64-bit operating system
*/
bool YWindowsPlatformMisc::Is64bitOperatingSystem()
{
#if PLATFORM_64BITS
	return true;
#else
#pragma warning( push )
#pragma warning( disable: 4191 )	// unsafe conversion from 'type of expression' to 'type required'
	typedef BOOL(WINAPI *LPFN_ISWOW64PROCESS)(HANDLE, PBOOL);
	LPFN_ISWOW64PROCESS fnIsWow64Process = (LPFN_ISWOW64PROCESS)GetProcAddress(GetModuleHandle(TEXT("kernel32")), "IsWow64Process");
	BOOL bIsWoW64Process = 0;
	if (fnIsWow64Process != NULL)
	{
		if (fnIsWow64Process(GetCurrentProcess(), &bIsWoW64Process) == 0)
		{
			bIsWoW64Process = 0;
		}
	}
#pragma warning( pop )
	return bIsWoW64Process == 1;
#endif
}

int32 YWindowsPlatformMisc::GetAppIcon()
{
	return IDICON_UE4Game;
}

bool YWindowsPlatformMisc::VerifyWindowsVersion(uint32 MajorVersion, uint32 MinorVersion)
{
	OSVERSIONINFOEX Version;
	Version.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
	Version.dwMajorVersion = MajorVersion;
	Version.dwMinorVersion = MinorVersion;
	ULONGLONG ConditionMask = 0;
	return !!VerifyVersionInfo(&Version, VER_MAJORVERSION, VerSetConditionMask(ConditionMask, VER_MAJORVERSION, VER_GREATER_EQUAL));
}

bool YWindowsPlatformMisc::IsValidAbsolutePathFormat(const YString& Path)
{
	bool bIsValid = true;
	const YString OnlyPath = YPaths::GetPath(Path);
	if (OnlyPath.IsEmpty())
	{
		bIsValid = false;
	}

	// Must begin with a drive letter
	if (bIsValid && !FChar::IsAlpha(OnlyPath[0]))
	{
		bIsValid = false;
	}

	// On Windows the path must be absolute, i.e: "D:/" or "D:\\"
	if (bIsValid && !(Path.Find(TEXT(":/")) == 1 || Path.Find(TEXT(":\\")) == 1))
	{
		bIsValid = false;
	}

	// Find any unnamed directory changes
	if (bIsValid && (Path.Find(TEXT("//")) != INDEX_NONE) || (Path.Find(TEXT("\\/")) != INDEX_NONE) || (Path.Find(TEXT("/\\")) != INDEX_NONE) || (Path.Find(TEXT("\\\\")) != INDEX_NONE))
	{
		bIsValid = false;
	}

	// ensure there's no further instances of ':' in the string
	if (bIsValid && !(Path.Find(TEXT(":"), ESearchCase::IgnoreCase, ESearchDir::FromStart, 2) == INDEX_NONE))
	{
		bIsValid = false;
	}

	return bIsValid;
}

int32 YWindowsPlatformMisc::NumberOfCores()
{
	static int32 CoreCount = 0;
	if (CoreCount == 0)
	{
		if (FParse::Param(FCommandLine::Get(), TEXT("usehyperthreading")))
		{
			CoreCount = NumberOfCoresIncludingHyperthreads();
		}
		else
		{
			// Get only physical cores
			PSYSTEM_LOGICAL_PROCESSOR_INFORMATION InfoBuffer = NULL;
			::DWORD BufferSize = 0;

			// Get the size of the buffer to hold processor information.
			::BOOL Result = GetLogicalProcessorInformation(InfoBuffer, &BufferSize);
			check(!Result && GetLastError() == ERROR_INSUFFICIENT_BUFFER);
			check(BufferSize > 0);

			// Allocate the buffer to hold the processor info.
			InfoBuffer = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION)FMemory::Malloc(BufferSize);
			check(InfoBuffer);

			// Get the actual information.
			Result = GetLogicalProcessorInformation(InfoBuffer, &BufferSize);
			check(Result);

			// Count physical cores
			const int32 InfoCount = (int32)(BufferSize / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
			for (int32 Index = 0; Index < InfoCount; ++Index)
			{
				SYSTEM_LOGICAL_PROCESSOR_INFORMATION* Info = &InfoBuffer[Index];
				if (Info->Relationship == RelationProcessorCore)
				{
					CoreCount++;
				}
			}
			FMemory::Free(InfoBuffer);
		}
	}
	return CoreCount;
}

int32 YWindowsPlatformMisc::NumberOfCoresIncludingHyperthreads()
{
	static int32 CoreCount = 0;
	if (CoreCount == 0)
	{
		// Get the number of logical processors, including hyperthreaded ones.
		SYSTEM_INFO SI;
		GetSystemInfo(&SI);
		CoreCount = (int32)SI.dwNumberOfProcessors;
	}
	return CoreCount;
}

void YWindowsPlatformMisc::LoadPreInitModules()
{
	// D3D11 is not supported on WinXP, so in this case we use the OpenGL RHI
	if (YWindowsPlatformMisc::VerifyWindowsVersion(6, 0))
	{
		//#todo-rco: Only try on Win10
		const bool bForceD3D12 = FParse::Param(FCommandLine::Get(), TEXT("d3d12")) || FParse::Param(FCommandLine::Get(), TEXT("dx12"));
		if (bForceD3D12)
		{
			FModuleManager::Get().LoadModule(TEXT("D3D12RHI"));
		}
		FModuleManager::Get().LoadModule(TEXT("D3D11RHI"));
	}
	FModuleManager::Get().LoadModule(TEXT("OpenGLDrv"));
}

void YWindowsPlatformMisc::LoadStartupModules()
{
	FModuleManager::Get().LoadModule(TEXT("XAudio2"));
#if !UE_SERVER
	FModuleManager::Get().LoadModule(TEXT("HeadMountedDisplay"));
#endif // !UE_SERVER

#if WITH_EDITOR
	FModuleManager::Get().LoadModule(TEXT("SourceCodeAccess"));
#endif	//WITH_EDITOR
}

bool YWindowsPlatformMisc::OsExecute(const TCHAR* CommandType, const TCHAR* Command, const TCHAR* CommandLine)
{
	HINSTANCE hApp = ShellExecute(NULL,
		CommandType,
		Command,
		CommandLine,
		NULL,
		SW_SHOWNORMAL);
	bool bSucceeded = hApp > (HINSTANCE)32;
	return bSucceeded;
}

struct FGetMainWindowHandleData
{
	HWND Handle;
	uint32 ProcessId;
};

int32 CALLBACK GetMainWindowHandleCallback(HWND Handle, LPARAM lParam)
{
	FGetMainWindowHandleData& Data = *(FGetMainWindowHandleData*)lParam;

	unsigned long ProcessId = 0;
	{
		::GetWindowThreadProcessId(Handle, &ProcessId);
	}

	if ((Data.ProcessId != ProcessId) || (::GetWindow(Handle, GW_OWNER) != (HWND)0) || !::IsWindowVisible(Handle))
	{
		return 1;
	}

	Data.Handle = Handle;

	return 0;
}

HWND YWindowsPlatformMisc::GetTopLevelWindowHandle(uint32 ProcessId)
{
	FGetMainWindowHandleData Data;
	{
		Data.Handle = 0;
		Data.ProcessId = ProcessId;
	}

	::EnumWindows(GetMainWindowHandleCallback, (LPARAM)&Data);

	return Data.Handle;
}

bool YWindowsPlatformMisc::GetWindowTitleMatchingText(const TCHAR* TitleStartsWith, YString& OutTitle)
{
	bool bWasFound = false;
	WCHAR Buffer[8192];
	// Get the first window so we can start walking the window chain
	HWND hWnd = FindWindowW(NULL, NULL);
	if (hWnd != NULL)
	{
		do
		{
			GetWindowText(hWnd, Buffer, 8192);
			// If this matches, then grab the full text
			if (_tcsnccmp(TitleStartsWith, Buffer, _tcslen(TitleStartsWith)) == 0)
			{
				OutTitle = Buffer;
				hWnd = NULL;
				bWasFound = true;
			}
			else
			{
				// Get the next window to interrogate
				hWnd = GetWindow(hWnd, GW_HWNDNEXT);
			}
		} while (hWnd != NULL);
	}
	return bWasFound;
}

void YWindowsPlatformMisc::RaiseException(uint32 ExceptionCode)
{
	/** This is the last place to gather memory stats before exception. */
	FGenericCrashContext::CrashMemoryStats = YPlatformMemory::GetStats();

	::RaiseException(ExceptionCode, 0, 0, NULL);
}

bool YWindowsPlatformMisc::SetStoredValue(const YString& InStoreId, const YString& InSectionName, const YString& InKeFName, const YString& InValue)
{
	check(!InStoreId.IsEmpty());
	check(!InSectionName.IsEmpty());
	check(!InKeFName.IsEmpty());

	YString FullRegistryKey = YString(TEXT("Software")) / InStoreId / InSectionName;
	FullRegistryKey = FullRegistryKey.Replace(TEXT("/"), TEXT("\\")); // we use forward slashes, but the registry needs back slashes

	HKEY hKey;
	HRESULT Result = ::RegCreateKeyEx(HKEY_CURRENT_USER, *FullRegistryKey, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
	if (Result == ERROR_SUCCESS)
	{
		Result = ::RegSetValueEx(hKey, *InKeFName, 0, REG_SZ, (const BYTE*)*InValue, (InValue.Len() + 1) * sizeof(TCHAR));
		::RegCloseKey(hKey);
	}

	if (Result != ERROR_SUCCESS)
	{
		TCHAR ErrorBuffer[1024];
		::FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, Result, 0, ErrorBuffer, 1024, nullptr);
		GWarn->Logf(TEXT("YWindowsPlatformMisc::SetStoredValue: ERROR: Could not store value for '%s'. Error Code %u: %s"), *InKeFName, Result, ErrorBuffer);
		return false;
	}

	return true;
}

bool YWindowsPlatformMisc::GetStoredValue(const YString& InStoreId, const YString& InSectionName, const YString& InKeFName, YString& OutValue)
{
	check(!InStoreId.IsEmpty());
	check(!InSectionName.IsEmpty());
	check(!InKeFName.IsEmpty());

	YString FullRegistryKey = YString(TEXT("Software")) / InStoreId / InSectionName;
	FullRegistryKey = FullRegistryKey.Replace(TEXT("/"), TEXT("\\")); // we use forward slashes, but the registry needs back slashes

	return QueryRegKey(HKEY_CURRENT_USER, *FullRegistryKey, *InKeFName, OutValue);
}

bool YWindowsPlatformMisc::DeleteStoredValue(const YString& InStoreId, const YString& InSectionName, const YString& InKeFName)
{
	// Deletes values in reg keys and also deletes the owning key if it becomes empty

	check(!InStoreId.IsEmpty());
	check(!InSectionName.IsEmpty());
	check(!InKeFName.IsEmpty());

	YString FullRegistryKey = YString(TEXT("Software")) / InStoreId / InSectionName;
	FullRegistryKey = FullRegistryKey.Replace(TEXT("/"), TEXT("\\")); // we use forward slashes, but the registry needs back slashes

	HKEY hKey;
	HRESULT Result = ::RegOpenKeyEx(HKEY_CURRENT_USER, *FullRegistryKey, 0, KEY_WRITE | KEY_READ, &hKey);
	if (Result == ERROR_SUCCESS)
	{
		Result = ::RegDeleteValue(hKey, *InKeFName);

		// Query for sub-keys in the open key
		TCHAR CheckKeFName[256];
		::DWORD CheckKeFNameLength = sizeof(CheckKeFName) / sizeof(CheckKeFName[0]);
		HRESULT EnumResult = RegEnumKeyEx(hKey, 0, CheckKeFName, &CheckKeFNameLength, NULL, NULL, NULL, NULL);
		bool bZeroSubKeys = EnumResult != ERROR_SUCCESS;

		// Query for a remaining value in the open key
		wchar_t CheckValueName[256];
		::DWORD CheckValueNameLength = sizeof(CheckValueName) / sizeof(CheckValueName[0]);
		EnumResult = RegEnumValue(hKey, 0, CheckValueName, &CheckValueNameLength, NULL, NULL, NULL, NULL);
		bool bZeroValues = EnumResult != ERROR_SUCCESS;

		::RegCloseKey(hKey);

		if (bZeroSubKeys && bZeroValues)
		{
			// No more values - delete the section
			::RegDeleteKey(HKEY_CURRENT_USER, *FullRegistryKey);
		}
	}

	return Result == ERROR_SUCCESS;
}

uint32 YWindowsPlatformMisc::GetLastError()
{
	return (uint32)::GetLastError();
}

bool YWindowsPlatformMisc::CoInitialize()
{
	HRESULT hr = ::CoInitialize(NULL);
	return hr == S_OK || hr == S_FALSE;
}

void YWindowsPlatformMisc::CoUninitialize()
{
	::CoUninitialize();
}

#if !UE_BUILD_SHIPPING
static TCHAR GErrorRemoteDebugPromptMessage[MAX_SPRINTF];

void YWindowsPlatformMisc::PromptForRemoteDebugging(bool bIsEnsure)
{
	if (bShouldPromptForRemoteDebugging)
	{
		if (bIsEnsure && !bPromptForRemoteDebugOnEnsure)
		{
			// Don't prompt on ensures unless overridden
			return;
		}

		if (FApp::IsUnattended())
		{
			// Do not ask if there is no one to show a message
			return;
		}

		if (GIsCriticalError && !GIsGuarded)
		{
			// A fatal error occurred.
			// We have not ability to debug, this does not make sense to ask.
			return;
		}

		// Upload locally compiled files for remote debugging
		FPlatformStackWalk::UploadLocalSymbols();

		FCString::Sprintf(GErrorRemoteDebugPromptMessage,
			TEXT("Have a programmer remote debug this crash?\n")
			TEXT("Hit NO to exit and submit error report as normal.\n")
			TEXT("Otherwise, contact a programmer for remote debugging,\n")
			TEXT("giving him the changelist number below.\n")
			TEXT("Once he confirms he is connected to the machine,\n")
			TEXT("hit YES to allow him to debug the crash.\n")
			TEXT("[Changelist = %d]"),
			FEngineVersion::Current().GetChangelist());
		FSlowHeartBeatScope SuspendHeartBeat;
		if (MessageBox(0, GErrorRemoteDebugPromptMessage, TEXT("CRASHED"), MB_YESNO | MB_SYSTEMMODAL) == IDYES)
		{
			::DebugBreak();
		}
	}
}
#endif	//#if !UE_BUILD_SHIPPING

FLinearColor YWindowsPlatformMisc::GetScreenPixelColor(const YVector2D& InScreenPos, float /*InGamma*/)
{
	COLORREF PixelColorRef = GetPixel(GetDC(HWND_DESKTOP), InScreenPos.X, InScreenPos.Y);

	FColor sRGBScreenColor(
		(PixelColorRef & 0xFF),
		((PixelColorRef & 0xFF00) >> 8),
		((PixelColorRef & 0xFF0000) >> 16),
		255);

	// Assume the screen color is coming in as sRGB space
	return FLinearColor(sRGBScreenColor);
}

/**
* Class that caches __cpuid queried data.
*/
class FCPUIDQueriedData
{
public:
	FCPUIDQueriedData()
		: bHasCPUIDInstruction(CheckForCPUIDInstruction()), Vendor(), CPUInfo(0), CacheLineSize(1)
	{
		if (bHasCPUIDInstruction)
		{
			Vendor = QueryCPUVendor();
			Brand = QueryCPUBrand();
			CPUInfo = QueryCPUInfo();
			CacheLineSize = QueryCacheLineSize();
		}
	}

	/**
	* Checks if this CPU supports __cpuid instruction.
	*
	* @returns True if this CPU supports __cpuid instruction. False otherwise.
	*/
	static bool HasCPUIDInstruction()
	{
		return CPUIDStaticCache.bHasCPUIDInstruction;
	}

	/**
	* Gets pre-cached CPU vendor name.
	*
	* @returns CPU vendor name.
	*/
	static const YString& GetVendor()
	{
		return CPUIDStaticCache.Vendor;
	}

	/**
	* Gets pre-cached CPU brand string.
	*
	* @returns CPU brand string.
	*/
	static const YString& GetBrand()
	{
		return CPUIDStaticCache.Brand;
	}

	/**
	* Gets __cpuid CPU info.
	*
	* @returns CPU info unsigned int queried using __cpuid.
	*/
	static uint32 GetCPUInfo()
	{
		return CPUIDStaticCache.CPUInfo;
	}

	/**
	* Gets cache line size.
	*
	* @returns Cache line size.
	*/
	static int32 GetCacheLineSize()
	{
		return CPUIDStaticCache.CacheLineSize;
	}

private:
	/**
	* Checks if __cpuid instruction is present on current machine.
	*
	* @returns True if this CPU supports __cpuid instruction. False otherwise.
	*/
	static bool CheckForCPUIDInstruction()
	{
#if PLATFORM_SEH_EXCEPTIONS_DISABLED
		return false;
#else
		__try
		{
			int Args[4];
			__cpuid(Args, 0);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
		return true;
#endif
	}

	/**
	* Queries Vendor name using __cpuid instruction.
	*
	* @returns CPU vendor name.
	*/
	static YString QueryCPUVendor()
	{
		union
		{
			char Buffer[12 + 1];
			struct
			{
				int dw0;
				int dw1;
				int dw2;
			} Dw;
		} VendorResult;

		int Args[4];
		__cpuid(Args, 0);

		VendorResult.Dw.dw0 = Args[1];
		VendorResult.Dw.dw1 = Args[3];
		VendorResult.Dw.dw2 = Args[2];
		VendorResult.Buffer[12] = 0;

		return ANSI_TO_TCHAR(VendorResult.Buffer);
	}

	/**
	* Queries brand string using __cpuid instruction.
	*
	* @returns CPU brand string.
	*/
	static YString QueryCPUBrand()
	{
		// @see for more information http://msdn.microsoft.com/en-us/library/vstudio/hskdteyh(v=vs.100).aspx
		ANSICHAR BrandString[0x40] = { 0 };
		int32 CPUInfo[4] = { -1 };
		const SIZE_T CPUInfoSize = sizeof(CPUInfo);

		__cpuid(CPUInfo, 0x80000000);
		const uint32 MaxExtIDs = CPUInfo[0];

		if (MaxExtIDs >= 0x80000004)
		{
			const uint32 FirstBrandString = 0x80000002;
			const uint32 NumBrandStrings = 3;
			for (uint32 Index = 0; Index < NumBrandStrings; Index++)
			{
				__cpuid(CPUInfo, FirstBrandString + Index);
				YPlatformMemory::Memcpy(BrandString + CPUInfoSize * Index, CPUInfo, CPUInfoSize);
			}
		}

		return ANSI_TO_TCHAR(BrandString);
	}

	/**
	* Queries CPU info using __cpuid instruction.
	*
	* @returns CPU info unsigned int queried using __cpuid.
	*/
	static uint32 QueryCPUInfo()
	{
		uint32 Info = 0;

		int Args[4];
		__cpuid(Args, 1);

		Info = Args[0];

		return Info;
	}

	/**
	* Queries cache line size using __cpuid instruction.
	*
	* @returns Cache line size.
	*/
	static int32 QueryCacheLineSize()
	{
		int32 Result = 1;

		int Args[4];
		__cpuid(Args, 0x80000006);

		Result = Args[2] & 0xFF;
		check(Result && !(Result & (Result - 1))); // assumed to be a power of two

		return Result;
	}

	/** Static field with pre-cached __cpuid data. */
	static FCPUIDQueriedData CPUIDStaticCache;

	/** If machine has CPUID instruction. */
	bool bHasCPUIDInstruction;

	/** Vendor of the CPU. */
	YString Vendor;

	/** CPU brand. */
	YString Brand;

	/** CPU info from __cpuid. */
	uint32 CPUInfo;

	/** CPU cache line size. */
	int32 CacheLineSize;
};

/** Static initialization of data to pre-cache __cpuid queries. */
FCPUIDQueriedData FCPUIDQueriedData::CPUIDStaticCache;

bool YWindowsPlatformMisc::HasCPUIDInstruction()
{
	return FCPUIDQueriedData::HasCPUIDInstruction();
}

YString YWindowsPlatformMisc::GetCPUVendor()
{
	return FCPUIDQueriedData::GetVendor();
}

YString YWindowsPlatformMisc::GetCPUBrand()
{
	return FCPUIDQueriedData::GetBrand();
}

#include "Windows/AllowWindowsPlatformTypes.h"
YString YWindowsPlatformMisc::GetPrimaryGPUBrand()
{
	static YString PrimaryGPUBrand;
	if (PrimaryGPUBrand.IsEmpty())
	{
		// Find primary display adapter and get the device name.
		PrimaryGPUBrand = YGenericPlatformMisc::GetPrimaryGPUBrand();

		DISPLAY_DEVICE DisplayDevice;
		DisplayDevice.cb = sizeof(DisplayDevice);
		DWORD DeviceIndex = 0;

		while (EnumDisplayDevices(0, DeviceIndex, &DisplayDevice, 0))
		{
			if ((DisplayDevice.StateFlags & (DISPLAY_DEVICE_ATTACHED_TO_DESKTOP | DISPLAY_DEVICE_PRIMARY_DEVICE)) > 0)
			{
				PrimaryGPUBrand = DisplayDevice.DeviceString;
				break;
			}

			FMemory::Memzero(DisplayDevice);
			DisplayDevice.cb = sizeof(DisplayDevice);
			DeviceIndex++;
		}
	}

	return PrimaryGPUBrand;
}

static void GetVideoDriverDetails(const YString& Key, FGPUDriverInfo& Out)
{
	// https://msdn.microsoft.com/en-us/library/windows/hardware/ff569240(v=vs.85).aspx

	const TCHAR* DeviceDescriptionValueName = TEXT("Device Description");

	bool bDevice = YWindowsPlatformMisc::QueryRegKey(HKEY_LOCAL_MACHINE, *Key, DeviceDescriptionValueName, Out.DeviceDescription); // AMD and NVIDIA

	if (!bDevice)
	{
		// in the case where this failed we also have:
		//  "DriverDesc"="NVIDIA GeForce GTX 670"

		// e.g. "GeForce GTX 680" (no NVIDIA prefix so no good for string comparison with DX)
		//	YWindowsPlatformMisc::QueryRegKey(HKEY_LOCAL_MACHINE, *Key, TEXT("HardwareInformation.AdapterString"), Out.DeviceDescription); // AMD and NVIDIA

		// Try again in Settings subfolder
		const YString SettingsSubKey = Key + TEXT("\\Settings");
		bDevice = YWindowsPlatformMisc::QueryRegKey(HKEY_LOCAL_MACHINE, *SettingsSubKey, DeviceDescriptionValueName, Out.DeviceDescription); // AMD and NVIDIA

		if (!bDevice)
		{
			// Neither root nor Settings subfolder contained a "Device Description" value so this is probably not a device
			Out = FGPUDriverInfo();
			return;
		}
	}

	// some key/value pairs explained: http://www.helpdoc-online.com/SCDMS01EN1A330P306~Windows-NT-Workstation-3.51-Resource-Kit-Help-en~Video-Device-Driver-Entries.htm

	YWindowsPlatformMisc::QueryRegKey(HKEY_LOCAL_MACHINE, *Key, TEXT("ProviderName"), Out.ProviderName);

	if (!Out.ProviderName.IsEmpty())
	{
		if (Out.ProviderName.Find(TEXT("NVIDIA")) != INDEX_NONE)
		{
			Out.SetNVIDIA();
		}
		else if (Out.ProviderName.Find(TEXT("Advanced Micro Devices")) != INDEX_NONE)
		{
			Out.SetAMD();
		}
		else if (Out.ProviderName.Find(TEXT("Intel")) != INDEX_NONE)	// usually TEXT("Intel Corporation")
		{
			Out.SetIntel();
		}
	}

	// technical driver version, AMD and NVIDIA
	YWindowsPlatformMisc::QueryRegKey(HKEY_LOCAL_MACHINE, *Key, TEXT("DriverVersion"), Out.InternalDriverVersion);

	Out.UserDriverVersion = Out.InternalDriverVersion;

	if (Out.IsNVIDIA())
	{
		Out.UserDriverVersion = Out.GetUnifiedDriverVersion();
	}
	else if (Out.IsAMD())
	{
		if (YWindowsPlatformMisc::QueryRegKey(HKEY_LOCAL_MACHINE, *Key, TEXT("Catalyst_Version"), Out.UserDriverVersion))
		{
			Out.UserDriverVersion = YString(TEXT("Catalyst ")) + Out.UserDriverVersion;
		}

		YString Edition;
		if (YWindowsPlatformMisc::QueryRegKey(HKEY_LOCAL_MACHINE, *Key, TEXT("RadeonSoftwareEdition"), Edition))
		{
			YString Version;
			if (YWindowsPlatformMisc::QueryRegKey(HKEY_LOCAL_MACHINE, *Key, TEXT("RadeonSoftwareVersion"), Version))
			{
				// e.g. TEXT("Crimson 15.12") or TEXT("Catalyst 14.1")
				Out.UserDriverVersion = Edition + TEXT(" ") + Version;
			}
		}
	}

	// AMD and NVIDIA
	YWindowsPlatformMisc::QueryRegKey(HKEY_LOCAL_MACHINE, *Key, TEXT("DriverDate"), Out.DriverDate);
}

FGPUDriverInfo YWindowsPlatformMisc::GetGPUDriverInfo(const YString& DeviceDescription)
{
	// to distinguish failed GetGPUDriverInfo() from call to GetGPUDriverInfo()
	FGPUDriverInfo Ret;

	Ret.InternalDriverVersion = TEXT("Unknown");
	Ret.UserDriverVersion = TEXT("Unknown");
	Ret.DriverDate = TEXT("Unknown");

	// for debugging, useful even in shipping to see what went wrong
	YString DebugString;

	uint32 FoundDriverCount = 0;

	int32 Method = CVarDriverDetectionMethod.GetValueOnGameThread();

	if (Method == 3 || Method == 4)
	{
		UE_LOG(LogWindows, Log, TEXT("EnumDisplayDevices:"));

		for (uint32 i = 0; i < 256; ++i)
		{
			DISPLAY_DEVICE Device;

			ZeroMemory(&Device, sizeof(Device));
			Device.cb = sizeof(Device);

			// see https://msdn.microsoft.com/en-us/library/windows/desktop/dd162609(v=vs.85).aspx
			if (EnumDisplayDevices(0, i, &Device, EDD_GET_DEVICE_INTERFACE_NAME) == 0)
			{
				// last device or error
				break;
			}

			UE_LOG(LogWindows, Log, TEXT("   %d. '%s' (P:%d D:%d)"),
				i,
				Device.DeviceString,
				(Device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) != 0,
				(Device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) != 0
			);

			if (Method == 3)
			{
				if (!(Device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE))
				{
					// see http://www.vistaheads.com/forums/microsoft-public-windows-vista-hardware-devices/286017-find-out-active-graphics-device-programmatically-registry-key.html
					DebugString += TEXT("JumpOverNonPrimary ");
					// we want the primary device
					continue;
				}
			}

			YString DriverLocation = Device.DeviceKey;

			if (DriverLocation.Left(18) == TEXT("\\Registry\\Machine\\"))		// not case sensitive
			{
				DriverLocation = YString(TEXT("\\HKEY_LOCAL_MACHINE\\")) + DriverLocation.RightChop(18);
			}
			if (DriverLocation.Left(20) == TEXT("\\HKEY_LOCAL_MACHINE\\"))		// not case sensitive
			{
				YString DriverKey = DriverLocation.RightChop(20);

				FGPUDriverInfo Local;
				GetVideoDriverDetails(DriverKey, Local);

				if (!Local.IsValid())
				{
					DebugString += TEXT("GetVideoDriverDetailsInvalid ");
				}

				if ((Method == 3) || (Local.DeviceDescription == DeviceDescription))
				{
					if (!FoundDriverCount)
					{
						Ret = Local;
					}
					++FoundDriverCount;
				}
				else
				{
					DebugString += TEXT("PrimaryIsNotTheChoosenAdapter ");
				}
			}
			else
			{
				DebugString += TEXT("PrimaryDriverLocationFailed ");
			}
		}

		if (FoundDriverCount != 1)
		{
			// We assume if multiple entries are found they are all the same driver. If that is correct - this is no error.
			DebugString += YString::Printf(TEXT("FoundDriverCount:%d "), FoundDriverCount);
		}

		if (!DebugString.IsEmpty())
		{
			UE_LOG(LogWindows, Log, TEXT("DebugString: %s"), *DebugString);
		}

		return Ret;
	}

	const bool bIterateAvailableAndChoose = Method == 0;

	if (bIterateAvailableAndChoose)
	{
		for (uint32 i = 0; i < 256; ++i)
		{
			// Iterate all installed display adapters
			const YString DriverNKey = YString::Printf(TEXT("SYSTEM\\CurrentControlSet\\Control\\Class\\{4D36E968-E325-11CE-BFC1-08002BE10318}\\%04d"), i);

			FGPUDriverInfo Local;
			GetVideoDriverDetails(DriverNKey, Local);

			if (!Local.IsValid())
			{
				// last device or error
				DebugString += TEXT("GetVideoDriverDetailsInvalid ");
				break;
			}

			if (Local.DeviceDescription == DeviceDescription)
			{
				// found the one we are searching for
				Ret = Local;
				++FoundDriverCount;
				break;
			}
		}
	}

	// FoundDriverCount can be != 1, we take the primary adapater (can be from upgrading a machine to a new OS or old drivers) which also might be wrong
	// see: http://win7settings.blogspot.com/2014/10/how-to-extract-installed-drivers-from.html
	// https://support.microsoft.com/en-us/kb/200435
	// http://www.experts-exchange.com/questions/10198207/Windows-NT-Display-adapter-information.html
	// alternative: from https://support.microsoft.com/en-us/kb/200435
	if (FoundDriverCount != 1)
	{
		// we start again, this time we only look at the primary adapter
		Ret.InternalDriverVersion = TEXT("Unknown");
		Ret.UserDriverVersion = TEXT("Unknown");
		Ret.DriverDate = TEXT("Unknown");

		if (bIterateAvailableAndChoose)
		{
			DebugString += YString::Printf(TEXT("FoundDriverCount:%d FallbackToPrimary "), FoundDriverCount);
		}

		YString DriverLocation; // e.g. HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\<videodriver>\Device0
								// Video0 is the first logical one, not neccesarily the primary, would have to iterate multiple to get the right one (see https://support.microsoft.com/en-us/kb/102992)
		bool bOk = YWindowsPlatformMisc::QueryRegKey(HKEY_LOCAL_MACHINE, TEXT("HARDWARE\\DEVICEMAP\\VIDEO"), TEXT("\\Device\\Video0"), /*out*/ DriverLocation);

		if (bOk)
		{
			if (DriverLocation.Left(18) == TEXT("\\Registry\\Machine\\"))		// not case sensitive
			{
				DriverLocation = YString(TEXT("\\HKEY_LOCAL_MACHINE\\")) + DriverLocation.RightChop(18);
			}
			if (DriverLocation.Left(20) == TEXT("\\HKEY_LOCAL_MACHINE\\"))		// not case sensitive
			{
				YString DriverLocationKey = DriverLocation.RightChop(20);

				FGPUDriverInfo Local;
				GetVideoDriverDetails(DriverLocationKey, Local);

				if (!Local.IsValid())
				{
					DebugString += TEXT("GetVideoDriverDetailsInvalid ");
				}

				if (Local.DeviceDescription == DeviceDescription)
				{
					Ret = Local;
					FoundDriverCount = 1;
				}
				else
				{
					DebugString += TEXT("PrimaryIsNotTheChoosenAdapter ");
				}
			}
			else
			{
				DebugString += TEXT("PrimaryDriverLocationFailed ");
			}
		}
		else
		{
			DebugString += TEXT("QueryForPrimaryFailed ");
		}
	}

	if (!DebugString.IsEmpty())
	{
		UE_LOG(LogWindows, Log, TEXT("DebugString: %s"), *DebugString);
	}

	return Ret;
}

#include "Windows/HideWindowsPlatformTypes.h"

void YWindowsPlatformMisc::GetOSVersions(YString& out_OSVersionLabel, YString& out_OSSubVersionLabel)
{
	static YString OSVersionLabel;
	static YString OSSubVersionLabel;

	if (OSVersionLabel.IsEmpty() && OSSubVersionLabel.IsEmpty())
	{
		YWindowsOSVersionHelper::GetOSVersions(OSVersionLabel, OSSubVersionLabel);
	}

	out_OSVersionLabel = OSVersionLabel;
	out_OSSubVersionLabel = OSSubVersionLabel;
}


bool YWindowsPlatformMisc::GetDiskTotalAndFreeSpace(const YString& InPath, uint64& TotalNumberOfBytes, uint64& NumberOfFreeBytes)
{
	bool bSuccess = false;
	// We need to convert the path to make sure it is formatted with windows style Drive e.g. "C:\"
	const YString ValidatedPath = YPaths::ConvertRelativePathToFull(InPath).Replace(TEXT("/"), TEXT("\\"));
	if (ValidatedPath.Len() >= 3 && ValidatedPath[1] == ':' && ValidatedPath[2] == '\\')
	{
		bSuccess = !!::GetDiskFreeSpaceEx(*ValidatedPath, nullptr, reinterpret_cast<ULARGE_INTEGER*>(&TotalNumberOfBytes), reinterpret_cast<ULARGE_INTEGER*>(&NumberOfFreeBytes));
	}
	return bSuccess;
}


uint32 YWindowsPlatformMisc::GetCPUInfo()
{
	return FCPUIDQueriedData::GetCPUInfo();
}

int32 YWindowsPlatformMisc::GetCacheLineSize()
{
	return FCPUIDQueriedData::GetCacheLineSize();
}

bool YWindowsPlatformMisc::QueryRegKey(const Windows::HKEY InKey, const TCHAR* InSubKey, const TCHAR* InValueName, YString& OutData)
{
	bool bSuccess = false;

	// Redirect key depending on system
	for (int32 RegistryIndex = 0; RegistryIndex < 2 && !bSuccess; ++RegistryIndex)
	{
		HKEY Key = 0;
		const uint32 RegFlags = (RegistryIndex == 0) ? KEY_WOW64_32KEY : KEY_WOW64_64KEY;
		if (RegOpenKeyEx(InKey, InSubKey, 0, KEY_READ | RegFlags, &Key) == ERROR_SUCCESS)
		{
			::DWORD Size = 0;
			// First, we'll call RegQueryValueEx to find out how large of a buffer we need
			if ((RegQueryValueEx(Key, InValueName, NULL, NULL, NULL, &Size) == ERROR_SUCCESS) && Size)
			{
				// Allocate a buffer to hold the value and call the function again to get the data
				char *Buffer = new char[Size];
				if (RegQueryValueEx(Key, InValueName, NULL, NULL, (LPBYTE)Buffer, &Size) == ERROR_SUCCESS)
				{
					OutData = YString(Size - 1, (TCHAR*)Buffer);
					OutData.TrimToNullTerminator();
					bSuccess = true;
				}
				delete[] Buffer;
			}
			RegCloseKey(Key);
		}
	}

	return bSuccess;
}

bool YWindowsPlatformMisc::GetVSComnTools(int32 Version, YString& OutData)
{
	checkf(12 <= Version && Version <= 15, L"Not supported Visual Studio version.");

	YString ValueName = YString::Printf(TEXT("%d.0"), Version);

	YString IDEPath;
	if (!QueryRegKey(HKEY_CURRENT_USER, TEXT("SOFTWARE\\Microsoft\\VisualStudio\\SxS\\VS7"), *ValueName, IDEPath))
	{
		if (!QueryRegKey(HKEY_LOCAL_MACHINE, TEXT("SOFTWARE\\Microsoft\\VisualStudio\\SxS\\VS7"), *ValueName, IDEPath))
		{
			if (!QueryRegKey(HKEY_CURRENT_USER, TEXT("SOFTWARE\\Wow6432Node\\Microsoft\\VisualStudio\\SxS\\VS7"), *ValueName, IDEPath))
			{
				if (!QueryRegKey(HKEY_LOCAL_MACHINE, TEXT("SOFTWARE\\Wow6432Node\\Microsoft\\VisualStudio\\SxS\\VS7"), *ValueName, IDEPath))
				{
					return false;
				}
			}
		}
	}

	OutData = YPaths::ConvertRelativePathToFull(YPaths::Combine(*IDEPath, L"Common7", L"Tools"));
	return true;
}

const TCHAR* YWindowsPlatformMisc::GetDefaultPathSeparator()
{
	return TEXT("\\");
}

FText YWindowsPlatformMisc::GetFileManagerName()
{
	return NSLOCTEXT("WindowsPlatform", "FileManagerName", "Explorer");
}

bool YWindowsPlatformMisc::IsRunningOnBattery()
{
	SYSTEM_POWER_STATUS status;
	GetSystemPowerStatus(&status);
	switch (status.BatteryFlag)
	{
	case 4://	"Critical-the battery capacity is at less than five percent"
	case 2://	"Low-the battery capacity is at less than 33 percent"
	case 1://	"High-the battery capacity is at more than 66 percent"
	case 8://	"Charging"
		return true;
	case 128://	"No system battery" - desktop, NB: UPS don't count as batteries under Windows
	case 255://	"Unknown status-unable to read the battery flag information"
	default:
		return false;
	}

	return false;
}

YString YWindowsPlatformMisc::GetOperatingSystemId()
{
	YString Result;
	// more info on this key can be found here: http://stackoverflow.com/questions/99880/generating-a-unique-machine-id
	QueryRegKey(HKEY_LOCAL_MACHINE, TEXT("Software\\Microsoft\\Cryptography"), TEXT("MachineGuid"), Result);
	return Result;
}


EConvertibleLaptopMode YWindowsPlatformMisc::GetConvertibleLaptopMode()
{
	if (!VerifyWindowsVersion(6, 2))
	{
		return EConvertibleLaptopMode::NotSupported;
	}

	if (::GetSystemMetrics(SM_CONVERTIBLESLATEMODE) == 0)
	{
		return EConvertibleLaptopMode::Tablet;
	}

	return EConvertibleLaptopMode::Laptop;
}

IPlatformChunkInstall* YWindowsPlatformMisc::GetPlatformChunkInstall()
{
	static IPlatformChunkInstall* ChunkInstall = nullptr;
	if (!ChunkInstall)
	{
#if !(WITH_EDITORONLY_DATA || IS_PROGRAM)

		IPlatformChunkInstallModule* PlatformChunkInstallModule = nullptr;

		FModuleStatus Status;
		if (FModuleManager::Get().QueryModule("HTTPChunkInstaller", Status))
		{
			PlatformChunkInstallModule = FModuleManager::LoadModulePtr<IPlatformChunkInstallModule>("HTTPChunkInstaller");
			if (PlatformChunkInstallModule != nullptr)
			{
				// Attempt to grab the platform installer
				ChunkInstall = PlatformChunkInstallModule->GetPlatformChunkInstall();
			}
		}

		if (PlatformChunkInstallModule == nullptr)
#endif
		{
			// Placeholder instance
			ChunkInstall = YGenericPlatformMisc::GetPlatformChunkInstall();
		}
	}

	return ChunkInstall;
}
