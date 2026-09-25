

#ifndef EFI_H
#define EFI_H

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef EFIAPI
#define EFIAPI
#endif

    typedef unsigned char      UINT8;
    typedef unsigned short     UINT16;
    typedef unsigned int       UINT32;
    typedef unsigned long long UINT64;
    typedef signed char        INT8;
    typedef short              INT16;
    typedef int                INT32;
    typedef long long          INT64;

    typedef UINT64 UINTN;
    typedef INT64  INTN;

    typedef UINT8  BOOLEAN;
    typedef UINT16 CHAR16;
    typedef UINT8  CHAR8;

    typedef void VOID;

    typedef UINTN  EFI_STATUS;
    typedef VOID*  EFI_HANDLE;
    typedef VOID*  EFI_EVENT;
    typedef UINT64 EFI_PHYSICAL_ADDRESS;
    typedef UINT64 EFI_VIRTUAL_ADDRESS;
    typedef UINT64 EFI_LBA;
    typedef UINTN  EFI_TPL;

#define TRUE  ((BOOLEAN)1)
#define FALSE ((BOOLEAN)0)
#ifndef NULL
#define NULL  ((VOID*)0)
#endif

    typedef struct
    {
        UINT32 Data1;
        UINT16 Data2;
        UINT16 Data3;
        UINT8  Data4[8];
    } EFI_GUID;

#define EFI_ERROR_BIT (((UINTN)1) << 63)
#define EFIERR(a)     (EFI_ERROR_BIT | (a))
#define EFI_ERROR(s)  (((INTN)(s)) < 0)

#define EFI_SUCCESS           0
#define EFI_LOAD_ERROR        EFIERR(1)
#define EFI_INVALID_PARAMETER EFIERR(2)
#define EFI_UNSUPPORTED       EFIERR(3)
#define EFI_BAD_BUFFER_SIZE   EFIERR(4)
#define EFI_BUFFER_TOO_SMALL  EFIERR(5)
#define EFI_NOT_READY         EFIERR(6)
#define EFI_DEVICE_ERROR      EFIERR(7)
#define EFI_WRITE_PROTECTED   EFIERR(8)
#define EFI_OUT_OF_RESOURCES  EFIERR(9)
#define EFI_NOT_FOUND         EFIERR(14)
#define EFI_ACCESS_DENIED     EFIERR(15)
#define EFI_ABORTED           EFIERR(21)

    typedef struct
    {
        UINT64 Signature;
        UINT32 Revision;
        UINT32 HeaderSize;
        UINT32 CRC32;
        UINT32 Reserved;
    } EFI_TABLE_HEADER;

    typedef enum
    {
        AllocateAnyPages,
        AllocateMaxAddress,
        AllocateAddress,
        MaxAllocateType
    } EFI_ALLOCATE_TYPE;

    typedef enum
    {
        EfiReservedMemoryType,
        EfiLoaderCode,
        EfiLoaderData,
        EfiBootServicesCode,
        EfiBootServicesData,
        EfiRuntimeServicesCode,
        EfiRuntimeServicesData,
        EfiConventionalMemory,
        EfiUnusableMemory,
        EfiACPIReclaimMemory,
        EfiACPIMemoryNVS,
        EfiMemoryMappedIO,
        EfiMemoryMappedIOPortSpace,
        EfiPalCode,
        EfiPersistentMemory,
        EfiMaxMemoryType
    } EFI_MEMORY_TYPE;

#define EFI_PAGE_SIZE        4096u
#define EFI_SIZE_TO_PAGES(s) (((s) + EFI_PAGE_SIZE - 1) / EFI_PAGE_SIZE)

    typedef EFI_STATUS(EFIAPI* EFI_ALLOCATE_PAGES)(EFI_ALLOCATE_TYPE     Type,
                                                   EFI_MEMORY_TYPE       MemoryType,
                                                   UINTN                 Pages,
                                                   EFI_PHYSICAL_ADDRESS* Memory);

    typedef EFI_STATUS(EFIAPI* EFI_FREE_PAGES)(EFI_PHYSICAL_ADDRESS Memory, UINTN Pages);

    typedef EFI_STATUS(EFIAPI* EFI_ALLOCATE_POOL)(EFI_MEMORY_TYPE PoolType, UINTN Size, VOID** Buffer);

    typedef EFI_STATUS(EFIAPI* EFI_FREE_POOL)(VOID* Buffer);

    typedef VOID(EFIAPI* EFI_COPY_MEM)(VOID* Destination, VOID* Source, UINTN Length);
    typedef VOID(EFIAPI* EFI_SET_MEM)(VOID* Buffer, UINTN Size, UINT8 Value);

    typedef EFI_STATUS(EFIAPI* EFI_HANDLE_PROTOCOL)(EFI_HANDLE Handle, EFI_GUID* Protocol, VOID** Interface);

    typedef EFI_STATUS(EFIAPI* EFI_LOCATE_PROTOCOL)(EFI_GUID* Protocol, VOID* Registration, VOID** Interface);

    typedef enum
    {
        AllHandles,
        ByRegisterNotify,
        ByProtocol
    } EFI_LOCATE_SEARCH_TYPE;

    typedef EFI_STATUS(EFIAPI* EFI_LOCATE_HANDLE_BUFFER)(
        EFI_LOCATE_SEARCH_TYPE SearchType, EFI_GUID* Protocol, VOID* SearchKey, UINTN* NoHandles, EFI_HANDLE** Buffer);

#define EFI_PCI_IO_PROTOCOL_GUID_LITERAL \
    { 0x4cf5b200, 0x68b8, 0x4ca5, { 0x9e, 0xec, 0xb2, 0x3e, 0x3f, 0x50, 0x02, 0x9a } }

    struct _EFI_PCI_IO_PROTOCOL;
    typedef struct _EFI_PCI_IO_PROTOCOL EFI_PCI_IO_PROTOCOL;

    typedef EFI_STATUS(EFIAPI* EFI_PCI_IO_PROTOCOL_GET_LOCATION)(
        EFI_PCI_IO_PROTOCOL* This, UINTN* SegmentNumber, UINTN* BusNumber, UINTN* DeviceNumber, UINTN* FunctionNumber);

    struct _EFI_PCI_IO_PROTOCOL
    {
        VOID*                            PollMem;
        VOID*                            PollIo;
        VOID*                            Mem;
        VOID*                            Io;
        VOID*                            Pci;
        VOID*                            CopyMem;
        VOID*                            Map;
        VOID*                            Unmap;
        VOID*                            AllocateBuffer;
        VOID*                            FreeBuffer;
        VOID*                            Flush;
        EFI_PCI_IO_PROTOCOL_GET_LOCATION GetLocation;
        VOID*                            Attributes;
        VOID*                            GetBarAttributes;
        VOID*                            SetBarAttributes;
        UINT64                           RomSize;
        VOID*                            RomImage;
    };

#ifndef EFI_SIMPLE_NETWORK_PROTOCOL_GUID
#define EFI_SIMPLE_NETWORK_PROTOCOL_GUID \
    { 0xA19832B9, 0xAC25, 0x11D3, { 0x9A, 0x2D, 0x00, 0x90, 0x27, 0x3F, 0xC1, 0x4D } }
#define EFI_SIMPLE_NETWORK_PROTOCOL_GUID_LITERAL EFI_SIMPLE_NETWORK_PROTOCOL_GUID
#endif

    struct _EFI_SIMPLE_NETWORK_PROTOCOL;
    typedef struct _EFI_SIMPLE_NETWORK_PROTOCOL EFI_SIMPLE_NETWORK_PROTOCOL;

    typedef struct
    {
        UINT8 Addr[32];
    } EFI_MAC_ADDRESS;

    typedef enum
    {
        EfiSimpleNetworkStopped,
        EfiSimpleNetworkStarted,
        EfiSimpleNetworkInitialized,
        EfiSimpleNetworkMaxState
    } EFI_SIMPLE_NETWORK_STATE;

    typedef struct
    {
        UINT32          State;
        UINT32          HwAddressSize;
        UINT32          MediaHeaderSize;
        UINT32          MaxPacketSize;
        UINT32          NvRamSize;
        UINT32          NvRamAccessSize;
        UINT32          ReceiveFilterMask;
        UINT32          ReceiveFilterSetting;
        UINT32          MaxMCastFilterCount;
        UINT32          MCastFilterCount;
        EFI_MAC_ADDRESS MCastFilter[16];
        EFI_MAC_ADDRESS CurrentAddress;
        EFI_MAC_ADDRESS BroadcastAddress;
        EFI_MAC_ADDRESS PermanentAddress;
        UINT8           IfType;
        BOOLEAN         MacAddressChangeable;
        BOOLEAN         MultipleTxSupported;
        BOOLEAN         MediaPresentSupported;
        BOOLEAN         MediaPresent;
    } EFI_SIMPLE_NETWORK_MODE;

    typedef EFI_STATUS(EFIAPI* EFI_SIMPLE_NETWORK_START)(EFI_SIMPLE_NETWORK_PROTOCOL* This);
    typedef EFI_STATUS(EFIAPI* EFI_SIMPLE_NETWORK_STOP)(EFI_SIMPLE_NETWORK_PROTOCOL* This);
    typedef EFI_STATUS(EFIAPI* EFI_SIMPLE_NETWORK_INITIALIZE)(EFI_SIMPLE_NETWORK_PROTOCOL* This,
                                                              UINTN                        ExtraRxBufferSize,
                                                              UINTN                        ExtraTxBufferSize);
    typedef EFI_STATUS(EFIAPI* EFI_SIMPLE_NETWORK_RESET)(EFI_SIMPLE_NETWORK_PROTOCOL* This,
                                                         BOOLEAN                      ExtendedVerification);
    typedef EFI_STATUS(EFIAPI* EFI_SIMPLE_NETWORK_SHUTDOWN)(EFI_SIMPLE_NETWORK_PROTOCOL* This);
    typedef EFI_STATUS(EFIAPI* EFI_SIMPLE_NETWORK_RECEIVE_FILTERS)(EFI_SIMPLE_NETWORK_PROTOCOL* This,
                                                                   UINT32                       Enable,
                                                                   UINT32                       Disable,
                                                                   BOOLEAN                      ResetMCastFilter,
                                                                   UINTN                        MCastFilterCnt,
                                                                   EFI_MAC_ADDRESS*             MCastFilter);
    typedef EFI_STATUS(EFIAPI* EFI_SIMPLE_NETWORK_STATION_ADDRESS)(EFI_SIMPLE_NETWORK_PROTOCOL* This,
                                                                   BOOLEAN                      Reset,
                                                                   EFI_MAC_ADDRESS*             New);
    typedef EFI_STATUS(EFIAPI* EFI_SIMPLE_NETWORK_STATISTICS)(EFI_SIMPLE_NETWORK_PROTOCOL* This,
                                                              BOOLEAN                      Reset,
                                                              UINTN*                       StatisticsSize,
                                                              VOID*                        StatisticsTable);
    typedef EFI_STATUS(EFIAPI* EFI_SIMPLE_NETWORK_MCAST_IP_TO_MAC)(EFI_SIMPLE_NETWORK_PROTOCOL* This,
                                                                   BOOLEAN                      IPv6,
                                                                   VOID*                        IP,
                                                                   EFI_MAC_ADDRESS*             MAC);
    typedef EFI_STATUS(EFIAPI* EFI_SIMPLE_NETWORK_NVDATA)(
        EFI_SIMPLE_NETWORK_PROTOCOL* This, BOOLEAN ReadWrite, UINTN Offset, UINTN BufferSize, VOID* Buffer);
    typedef EFI_STATUS(EFIAPI* EFI_SIMPLE_NETWORK_GET_STATUS)(EFI_SIMPLE_NETWORK_PROTOCOL* This,
                                                              UINT32*                      InterruptStatus,
                                                              VOID**                       TxBuf);
    typedef EFI_STATUS(EFIAPI* EFI_SIMPLE_NETWORK_TRANSMIT)(EFI_SIMPLE_NETWORK_PROTOCOL* This,
                                                            UINTN                        HeaderSize,
                                                            UINTN                        BufferSize,
                                                            VOID*                        Buffer,
                                                            EFI_MAC_ADDRESS*             SrcAddr,
                                                            EFI_MAC_ADDRESS*             DestAddr,
                                                            UINT16*                      Protocol);
    typedef EFI_STATUS(EFIAPI* EFI_SIMPLE_NETWORK_RECEIVE)(EFI_SIMPLE_NETWORK_PROTOCOL* This,
                                                           UINTN*                       HeaderSize,
                                                           UINTN*                       BufferSize,
                                                           VOID*                        Buffer,
                                                           EFI_MAC_ADDRESS*             SrcAddr,
                                                           EFI_MAC_ADDRESS*             DestAddr,
                                                           UINT16*                      Protocol);

    struct _EFI_SIMPLE_NETWORK_PROTOCOL
    {
        UINT64                             Revision;
        EFI_SIMPLE_NETWORK_START           Start;
        EFI_SIMPLE_NETWORK_STOP            Stop;
        EFI_SIMPLE_NETWORK_INITIALIZE      Initialize;
        EFI_SIMPLE_NETWORK_RESET           Reset;
        EFI_SIMPLE_NETWORK_SHUTDOWN        Shutdown;
        EFI_SIMPLE_NETWORK_RECEIVE_FILTERS ReceiveFilters;
        EFI_SIMPLE_NETWORK_STATION_ADDRESS StationAddress;
        EFI_SIMPLE_NETWORK_STATISTICS      Statistics;
        EFI_SIMPLE_NETWORK_MCAST_IP_TO_MAC MCastIpToMac;
        EFI_SIMPLE_NETWORK_NVDATA          NvData;
        EFI_SIMPLE_NETWORK_GET_STATUS      GetStatus;
        EFI_SIMPLE_NETWORK_TRANSMIT        Transmit;
        EFI_SIMPLE_NETWORK_RECEIVE         Receive;
        EFI_EVENT                          WaitForPacket;
        EFI_SIMPLE_NETWORK_MODE*           Mode;
    };

    struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
    typedef struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

    typedef EFI_STATUS(EFIAPI* EFI_TEXT_STRING)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* This, CHAR16* String);

    struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL
    {
        VOID*           Reset;
        EFI_TEXT_STRING OutputString;

        VOID* TestString;
        VOID* QueryMode;
        VOID* SetMode;
        VOID* SetAttribute;
        VOID* ClearScreen;
        VOID* SetCursorPosition;
        VOID* EnableCursor;
        VOID* Mode;
    };

    typedef struct
    {
        UINT16 ScanCode;
        CHAR16 UnicodeChar;
    } EFI_INPUT_KEY;

#define EFI_SCAN_ESC 0x17

    struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL;
    typedef struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL EFI_SIMPLE_TEXT_INPUT_PROTOCOL;

    typedef EFI_STATUS(EFIAPI* EFI_INPUT_RESET)(EFI_SIMPLE_TEXT_INPUT_PROTOCOL* This, BOOLEAN ExtendedVerification);
    typedef EFI_STATUS(EFIAPI* EFI_INPUT_READ_KEY)(EFI_SIMPLE_TEXT_INPUT_PROTOCOL* This, EFI_INPUT_KEY* Key);

    struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL
    {
        EFI_INPUT_RESET    Reset;
        EFI_INPUT_READ_KEY ReadKeyStroke;
        EFI_EVENT          WaitForKey;
    };

    typedef struct _EFI_DEVICE_PATH_PROTOCOL
    {
        UINT8 Type;
        UINT8 SubType;
        UINT8 Length[2];
    } EFI_DEVICE_PATH_PROTOCOL;

#define EFI_DEVICE_PATH_PROTOCOL_GUID { 0x09576e91, 0x6d3f, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }

    typedef EFI_STATUS(EFIAPI* EFI_IMAGE_LOAD)(BOOLEAN                   BootPolicy,
                                               EFI_HANDLE                ParentImageHandle,
                                               EFI_DEVICE_PATH_PROTOCOL* DevicePath,
                                               VOID*                     SourceBuffer,
                                               UINTN                     SourceSize,
                                               EFI_HANDLE*               ImageHandle);

    typedef EFI_STATUS(EFIAPI* EFI_IMAGE_START)(EFI_HANDLE ImageHandle, UINTN* ExitDataSize, CHAR16** ExitData);

    typedef EFI_STATUS(EFIAPI* EFI_EXIT)(EFI_HANDLE ImageHandle,
                                         EFI_STATUS ExitStatus,
                                         UINTN      ExitDataSize,
                                         CHAR16*    ExitData);

#define EFI_LOADED_IMAGE_PROTOCOL_GUID \
    { 0x5b1b31a1, 0x9562, 0x11d2, { 0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }

#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID \
    { 0x0964e5b22, 0x6459, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }

    typedef struct _EFI_LOADED_IMAGE_PROTOCOL
    {
        UINT32                    Revision;
        EFI_HANDLE                ParentHandle;
        VOID*                     SystemTable;
        EFI_HANDLE                DeviceHandle;
        EFI_DEVICE_PATH_PROTOCOL* FilePath;
        VOID*                     Reserved;
        UINT32                    LoadOptionsSize;
        VOID*                     LoadOptions;
        VOID*                     ImageBase;
        UINT64                    ImageSize;
        EFI_MEMORY_TYPE           ImageCodeType;
        EFI_MEMORY_TYPE           ImageDataType;
        VOID*                     Unload;
    } EFI_LOADED_IMAGE_PROTOCOL;

    struct _EFI_FILE_PROTOCOL;
    typedef struct _EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;

    struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;
    typedef struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

    typedef EFI_STATUS(EFIAPI* EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_OPEN_VOLUME)(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* This,
                                                                            EFI_FILE_PROTOCOL**              Root);

    struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL
    {
        UINT64                                      Revision;
        EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_OPEN_VOLUME OpenVolume;
    };

#define EFI_FILE_MODE_READ   0x0000000000000001ULL
#define EFI_FILE_MODE_WRITE  0x0000000000000002ULL
#define EFI_FILE_MODE_CREATE 0x8000000000000000ULL

    typedef EFI_STATUS(EFIAPI* EFI_FILE_OPEN)(
        EFI_FILE_PROTOCOL* This, EFI_FILE_PROTOCOL** NewHandle, CHAR16* FileName, UINT64 OpenMode, UINT64 Attributes);
    typedef EFI_STATUS(EFIAPI* EFI_FILE_CLOSE)(EFI_FILE_PROTOCOL* This);
    typedef EFI_STATUS(EFIAPI* EFI_FILE_DELETE)(EFI_FILE_PROTOCOL* This);
    typedef EFI_STATUS(EFIAPI* EFI_FILE_READ)(EFI_FILE_PROTOCOL* This, UINTN* BufferSize, VOID* Buffer);
    typedef EFI_STATUS(EFIAPI* EFI_FILE_WRITE)(EFI_FILE_PROTOCOL* This, UINTN* BufferSize, VOID* Buffer);
    typedef EFI_STATUS(EFIAPI* EFI_FILE_GET_POSITION)(EFI_FILE_PROTOCOL* This, UINT64* Position);
    typedef EFI_STATUS(EFIAPI* EFI_FILE_SET_POSITION)(EFI_FILE_PROTOCOL* This, UINT64 Position);
    typedef EFI_STATUS(EFIAPI* EFI_FILE_GET_INFO)(EFI_FILE_PROTOCOL* This,
                                                  EFI_GUID*          InformationType,
                                                  UINTN*             BufferSize,
                                                  VOID*              Buffer);
    typedef EFI_STATUS(EFIAPI* EFI_FILE_SET_INFO)(EFI_FILE_PROTOCOL* This,
                                                  EFI_GUID*          InformationType,
                                                  UINTN              BufferSize,
                                                  VOID*              Buffer);
    typedef EFI_STATUS(EFIAPI* EFI_FILE_FLUSH)(EFI_FILE_PROTOCOL* This);

    struct _EFI_FILE_PROTOCOL
    {
        UINT64                Revision;
        EFI_FILE_OPEN         Open;
        EFI_FILE_CLOSE        Close;
        EFI_FILE_DELETE       Delete;
        EFI_FILE_READ         Read;
        EFI_FILE_WRITE        Write;
        EFI_FILE_GET_POSITION GetPosition;
        EFI_FILE_SET_POSITION SetPosition;
        EFI_FILE_GET_INFO     GetInfo;
        EFI_FILE_SET_INFO     SetInfo;
        EFI_FILE_FLUSH        Flush;
    };

#define EFI_FILE_INFO_GUID { 0x09576e92, 0x6d3f, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }

    typedef struct
    {
        UINT64 Size;
        UINT64 FileSize;
        UINT64 PhysicalSize;

        UINT8  reserved[8 * 3 + 8];
        CHAR16 FileName[1];
    } EFI_FILE_INFO;

#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID \
    { 0x9042a9de, 0x23dc, 0x4a38, { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a } }

    typedef enum
    {
        PixelRedGreenBlueReserved8BitPerColor = 0,
        PixelBlueGreenRedReserved8BitPerColor = 1,
        PixelBitMask                          = 2,
        PixelBltOnly                          = 3,
        PixelFormatMax                        = 4
    } EFI_GRAPHICS_PIXEL_FORMAT;

    typedef struct
    {
        UINT32 RedMask;
        UINT32 GreenMask;
        UINT32 BlueMask;
        UINT32 ReservedMask;
    } EFI_PIXEL_BITMASK;

    typedef struct
    {
        UINT32                    Version;
        UINT32                    HorizontalResolution;
        UINT32                    VerticalResolution;
        EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
        EFI_PIXEL_BITMASK         PixelInformation;
        UINT32                    PixelsPerScanLine;
    } EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

    typedef struct
    {
        UINT32                                MaxMode;
        UINT32                                Mode;
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* Info;
        UINTN                                 SizeOfInfo;
        EFI_PHYSICAL_ADDRESS                  FrameBufferBase;
        UINTN                                 FrameBufferSize;
    } EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

    typedef struct
    {
        VOID*                              QueryMode;
        VOID*                              SetMode;
        VOID*                              Blt;
        EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE* Mode;
    } EFI_GRAPHICS_OUTPUT_PROTOCOL;

    typedef struct
    {
        EFI_TABLE_HEADER Hdr;

        VOID* RaiseTPL;
        VOID* RestoreTPL;

        EFI_ALLOCATE_PAGES AllocatePages;
        EFI_FREE_PAGES     FreePages;
        VOID*              GetMemoryMap;
        EFI_ALLOCATE_POOL  AllocatePool;
        EFI_FREE_POOL      FreePool;

        VOID* CreateEvent;
        VOID* SetTimer;
        VOID* WaitForEvent;
        VOID* SignalEvent;
        VOID* CloseEvent;
        VOID* CheckEvent;

        VOID*               InstallProtocolInterface;
        VOID*               ReinstallProtocolInterface;
        VOID*               UninstallProtocolInterface;
        EFI_HANDLE_PROTOCOL HandleProtocol;
        VOID*               Reserved;
        VOID*               RegisterProtocolNotify;
        VOID*               LocateHandle;
        VOID*               LocateDevicePath;
        VOID*               InstallConfigurationTable;

        EFI_IMAGE_LOAD  LoadImage;
        EFI_IMAGE_START StartImage;
        VOID*           Exit;
        VOID*           UnloadImage;
        VOID*           ExitBootServices;

        VOID* GetNextMonotonicCount;
        EFI_STATUS(EFIAPI* Stall)(UINTN Microseconds);
        VOID* SetWatchdogTimer;

        VOID* ConnectController;
        VOID* DisconnectController;

        VOID* OpenProtocol;
        VOID* CloseProtocol;
        VOID* OpenProtocolInformation;

        VOID*                    ProtocolsPerHandle;
        EFI_LOCATE_HANDLE_BUFFER LocateHandleBuffer;
        EFI_LOCATE_PROTOCOL      LocateProtocol;
        VOID*                    InstallMultipleProtocolInterfaces;
        VOID*                    UninstallMultipleProtocolInterfaces;

        VOID* CalculateCrc32;

        EFI_COPY_MEM CopyMem;
        EFI_SET_MEM  SetMem;
        VOID*        CreateEventEx;
    } EFI_BOOT_SERVICES;

    typedef struct
    {
        EFI_GUID VendorGuid;
        VOID*    VendorTable;
    } EFI_CONFIGURATION_TABLE;

    typedef struct
    {
        EFI_TABLE_HEADER                        Hdr;
        CHAR16*                                 FirmwareVendor;
        UINT32                                  FirmwareRevision;
        EFI_HANDLE                              ConsoleInHandle;
        struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL* ConIn;
        EFI_HANDLE                              ConsoleOutHandle;
        EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*        ConOut;
        EFI_HANDLE                              StandardErrorHandle;
        EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*        StdErr;
        VOID*                                   RuntimeServices;
        EFI_BOOT_SERVICES*                      BootServices;
        UINTN                                   NumberOfTableEntries;
        EFI_CONFIGURATION_TABLE*                ConfigurationTable;
    } EFI_SYSTEM_TABLE;

#ifdef __cplusplus
}
#endif

#endif
