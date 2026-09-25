

#pragma once
#include <cstdint>

#ifndef EFIAPI
#define EFIAPI
#endif

extern "C"
{
    typedef uint64_t EFI_STATUS_T;
    typedef uint64_t EFI_UINTN_T;
    typedef uint8_t  EFI_BOOLEAN_T;

    typedef struct EFI_MP_SERVICES_PROTOCOL EFI_MP_SERVICES_PROTOCOL_t;

    typedef struct
    {
        uint32_t Data1;
        uint16_t Data2;
        uint16_t Data3;
        uint8_t  Data4[8];
    } EFI_GUID_t;

    static constexpr EFI_GUID_t gEfiMpServiceProtocolGuid = {
        0x3fdda605, 0xa76e, 0x4f46, { 0xad, 0x29, 0x12, 0xf4, 0x53, 0x1b, 0x3d, 0x08 }
    };

    typedef void(EFIAPI* EFI_AP_PROCEDURE_t)(void* ProcedureArgument);

    typedef EFI_STATUS_T(EFIAPI* EFI_MP_GET_NUMBER_OF_PROCESSORS_t)(EFI_MP_SERVICES_PROTOCOL_t* This,
                                                                    EFI_UINTN_T*                NumberOfProcessors,
                                                                    EFI_UINTN_T* NumberOfEnabledProcessors);

    typedef EFI_STATUS_T(EFIAPI* EFI_MP_STARTUP_ALL_APS_t)(EFI_MP_SERVICES_PROTOCOL_t* This,
                                                           EFI_AP_PROCEDURE_t          Procedure,
                                                           EFI_BOOLEAN_T               SingleThread,
                                                           void*                       WaitEvent,
                                                           EFI_UINTN_T                 TimeoutInMicroseconds,
                                                           void*                       ProcedureArgument,
                                                           EFI_UINTN_T**               FailedCpuList);

    typedef EFI_STATUS_T(EFIAPI* EFI_MP_WHOAMI_t)(EFI_MP_SERVICES_PROTOCOL_t* This, EFI_UINTN_T* ProcessorNumber);

    struct EFI_MP_SERVICES_PROTOCOL
    {
        EFI_MP_GET_NUMBER_OF_PROCESSORS_t GetNumberOfProcessors;
        void*                             GetProcessorInfo;
        EFI_MP_STARTUP_ALL_APS_t          StartupAllAPs;
        void*                             StartupThisAP;
        void*                             SwitchBSP;
        void*                             EnableDisableAP;
        EFI_MP_WHOAMI_t                   WhoAmI;
    };

    typedef EFI_STATUS_T(EFIAPI* EFI_LOCATE_PROTOCOL_t)(EFI_GUID_t* Protocol, void* Registration, void** Interface);

    typedef struct
    {
        uint8_t Hdr[24];

        void* RaiseTPL;
        void* RestoreTPL;

        void* AllocatePages;
        void* FreePages;
        void* GetMemoryMap;
        void* AllocatePool;
        void* FreePool;

        void* CreateEvent;
        void* SetTimer;
        void* WaitForEvent;
        void* SignalEvent;
        void* CloseEvent;
        void* CheckEvent;

        void* InstallProtocolInterface;
        void* ReinstallProtocolInterface;
        void* UninstallProtocolInterface;
        void* HandleProtocol;
        void* Reserved;
        void* RegisterProtocolNotify;
        void* LocateHandle;
        void* LocateDevicePath;
        void* InstallConfigurationTable;

        void* LoadImage;
        void* StartImage;
        void* Exit;
        void* UnloadImage;
        void* ExitBootServices;

        void* GetNextMonotonicCount;
        void* Stall;
        void* SetWatchdogTimer;

        void* ConnectController;
        void* DisconnectController;

        void* OpenProtocol;
        void* CloseProtocol;
        void* OpenProtocolInformation;

        void*                 ProtocolsPerHandle;
        void*                 LocateHandleBuffer;
        EFI_LOCATE_PROTOCOL_t LocateProtocol;
        void*                 InstallMultipleProtocolInterfaces;
        void*                 UninstallMultipleProtocolInterfaces;

    } EFI_BOOT_SERVICES_MIN_t;

    typedef struct
    {
        uint8_t                  Hdr[24];
        void*                    FirmwareVendor;
        uint32_t                 FirmwareRevision;
        uint32_t                 _pad;
        void*                    ConsoleInHandle;
        void*                    ConIn;
        void*                    ConsoleOutHandle;
        void*                    ConOut;
        void*                    StandardErrorHandle;
        void*                    StdErr;
        void*                    RuntimeServices;
        EFI_BOOT_SERVICES_MIN_t* BootServices;

    } EFI_SYSTEM_TABLE_MIN_t;
}
