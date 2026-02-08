# Windows File Manager (winfile) - High-Level Design Overview

## Overview
The Windows File Manager (winfile) is a modern recreation of the classic Windows 3.1 File Manager. It provides a traditional dual-pane interface for file system navigation and management, with a tree view for directory hierarchies and a detailed file listing view. The application follows classic Win32 programming patterns with MDI (Multiple Document Interface) architecture, direct Win32 API usage, and C-style programming with minimal abstraction layers.

## Architecture

### Single-Tier Monolithic Architecture
Unlike progman's clean separation of UI and business logic, winfile uses a traditional monolithic architecture where:

1. **Presentation and Logic Mixed** - UI components directly handle business logic
2. **Direct Win32 API Usage** - Minimal abstraction over operating system APIs
3. **Global State Management** - Extensive use of global variables for shared state
4. **Traditional C Programming** - Procedural programming with structs and function pointers

### Window Structure Hierarchy
```
Frame Window (FrameWndProc) - hwndFrame
├── Drive Bar (DrivesWndProc) - hwndDriveBar
├── MDI Client - hwndMDIClient
│   ├── Tree Window (TreeWndProc) - Multiple instances
│   │   ├── Tree Control (TreeControlWndProc) - hwndTree
│   │   │   └── Tree Listbox (IDCW_TREELISTBOX)
│   │   └── Directory Listbox (DirWndProc) - hwndDir
│   │       └── File Listbox (IDCW_LISTBOX)
│   └── Search Window (SearchWndProc) - hwndSearch
│       └── Results Listbox (IDCW_LISTBOX)
└── Status Bar - hwndStatus
```

## Core Components

### Window Management Layer

#### Main Frame Window (`winfile.cpp`)
- **`FrameWndProc`** - Central message router and coordinator
- **Window Initialization** - Application startup and MDI setup
- **Menu Management** - Dynamic menu state updates and command routing
- **Global Message Handling** - File system change notifications and cross-window coordination
- **Status Bar Updates** - Centralized status message management

#### MDI Child Windows (`wftree.cpp`)
- **`TreeWndProc`** - MDI container for tree/directory views
- **Split Management** - Dynamic resizing between tree and directory panes
- **Focus Coordination** - Managing focus between tree and directory components
- **Window State Persistence** - Saving and restoring window positions and split ratios

### File System Navigation Layer

#### Tree Control (`treectl.cpp`)
- **Hierarchical Directory Tree** - Tree-based folder navigation with expand/collapse
- **Node Management** - Dynamic tree node creation, insertion, and sorting
- **Path Resolution** - Bidirectional conversion between tree nodes and file paths
- **Lazy Loading** - On-demand directory reading with background scanning
- **Visual Tree Drawing** - Custom drawing with connection lines and folder icons

#### Directory Listing (`wfdir.cpp`)
- **File Enumeration** - Directory content reading and caching
- **Multi-Column Display** - Name, size, date, time, attributes with custom drawing
- **Sorting and Filtering** - Multiple sort criteria and attribute-based filtering
- **Selection Management** - Multi-selection with drag-and-drop support
- **View Modes** - Name-only vs. detailed views with customizable columns

### File Operations Layer

#### Copy/Move Engine (`wfcopy.cpp`)
- **File Transfer Operations** - Copy, move, delete with progress tracking
- **Conflict Resolution** - Overwrite confirmation dialogs and error handling
- **Path Validation** - Long filename support and path qualification
- **Threading Support** - Background operations with user cancellation
- **Undo/Rollback** - Atomic operations with error recovery

#### Search Functionality (`wfsearch.cpp`)
- **Multi-Threaded Search** - Background file searching with real-time results
- **Pattern Matching** - Wildcard support and attribute-based filtering
- **Progress Tracking** - Live update of search progress and file count
- **Cancellation Support** - User-initiated search termination
- **Results Management** - Dynamic result list building and display

### Drive and Hardware Layer

#### Drive Management (`wfdrives.cpp`)
- **Drive Detection** - Dynamic drive enumeration and type identification
- **Drive Bar Display** - Visual drive selection interface with status indicators
- **Network Drive Support** - UNC path handling and connection management
- **Removable Media** - Floppy disk and CD-ROM handling with validation
- **Drive Status Tracking** - Available/unavailable state management

#### Hardware Integration (`wfutil.cpp`)
- **Path Utilities** - Comprehensive path manipulation and validation
- **Drive Information** - Volume labels, file system types, and space calculations
- **Network Capabilities** - Remote connection status and share enumeration
- **System Integration** - Registry access and system directory operations

### User Interface Layer

#### Menu and Command System (`wfcomman.cpp`)
- **Command Dispatch** - Central command processing and routing
- **Context Menus** - Dynamic context-sensitive menu generation
- **Accelerator Keys** - Keyboard shortcut handling and conflicts
- **Menu State Management** - Enable/disable states based on current selection
- **Extension Support** - Third-party menu extension integration
- **ZIP Archive Submenu** - Archive creation and extraction commands with selection-based enabling
  - **Smart Naming** - "Add to Zip" command uses improved logic to name archives after the containing folder, with fallback handling for root paths
  - **Path Handling** - Proper path construction prevents double backslashes in archive file paths
  - **Context Menu Integration** - Both File menu and context menu properly enable/disable ZIP archive commands based on current selection, with correct timing to reflect the actual right-clicked item

#### Drag and Drop (`wfdrop.cpp`, `wfdragsrc.cpp`)
- **OLE Drag/Drop** - COM-based drag and drop implementation
- **Visual Feedback** - Drag cursors and drop target highlighting
- **Data Transfer** - File path and data object marshaling
- **Cross-Application** - Integration with Windows shell and other applications

#### Archive Progress Dialog (`archiveprogress.cpp`)
- **Progress Tracking** - Thread-safe progress dialog for zip/unzip operations
- **Cancellation Support** - User-initiated operation cancellation using libheirloom CancellationToken
- **Worker Thread Management** - Background operation execution with UI updates
- **Exception Handling** - Proper error capture and reporting from background operations
- **Timer-Based Updates** - 100ms interval UI refresh from worker thread data
- **ArchiveStatus Integration** - Uses libwinfile's ArchiveStatus class for thread-safe UI updates
- **Progress Bar Control** - Visual progress bar that shows/hides based on operation type, displays percentage completion for finalization operations

## Key Data Structures

### File System Representation
- **`XDTA`** - Extended directory entry with file metadata and display information
- **`XDTALINK`** - Linked list structure for file collections with memory management
- **`DNODE`** - Tree node structure representing directory hierarchy
- **`LFNDTA`** - Long filename directory entry wrapper around Win32 structures

### Drive and Path Management
- **`DRIVE_INFO`** - Comprehensive drive information including type, space, and network details
- **`WINDOW`** - Window state persistence structure for position and view settings
- **Global Drive Arrays** - `rgiDrive[]`, `aDriveInfo[]` for system-wide drive management

### Search and Selection
- **`SEARCH_INFO`** - Search operation state and progress tracking
- **`SELINFO`** - Multi-selection state and anchor/focus management
- **`COPYINFO`** - File operation parameters and progress tracking

## Key Features

### File System Operations
- **File Management** - Copy, move, delete, rename with confirmation dialogs
- **Directory Operations** - Create, delete, navigate with tree synchronization
- **Archive Operations** - ZIP archive creation and extraction with "Add to Zip", "Add To...", "Extract Here", "Extract to New Folder", and "Extract To..." commands
- **Attribute Handling** - Read-only, hidden, system, compressed, encrypted file support
- **Long Filename Support** - Windows 95+ LFN with legacy 8.3 compatibility
- **Network File Support** - UNC paths, mapped drives, and remote operations

### User Interface Features
- **Dual-Pane View** - Resizable split between tree and file listing
- **Multiple Windows** - MDI interface supporting up to 27 concurrent windows
- **Customizable Views** - Name-only, detailed, with configurable columns and sorting
- **Drive Bar** - Visual drive selection with status indicators
- **Search Interface** - Integrated search with real-time results and progress

### File System Integration
- **Change Notifications** - Real-time updates when files change externally
- **Shell Integration** - Windows shell extension support and context menu integration
- **Recycle Bin Support** - Delete operations with recycle bin integration
- **Icon Extraction** - File type icons from system and custom icon sources

### Advanced Features
- **Bookmark System** - Saved locations with management interface
- **History Navigation** - Back/forward navigation with mouse button support
- **Extension Support** - Plugin architecture for third-party enhancements
- **Print Support** - Directory listings and file information printing
- **Archive Progress Dialog** - Reusable progress dialog infrastructure for zip operations with cancellation support

### Help Menu
- **About** - Shows application information dialog
- **Visit Website** - Opens `https://heirloomapps.com` in the default browser using ShellExecute

## Technical Details

### Programming Model
- **Win32 API Direct Usage** - Minimal wrapper layers around Windows APIs
- **Message-Driven Architecture** - Heavy reliance on Windows message passing
- **Global State Management** - Extensive use of global variables for coordination
- **Manual Memory Management** - LocalAlloc/LocalFree for dynamic memory

### Threading Model
- **Single UI Thread** - All window operations on main thread
- **Background Workers** - File operations, search, and directory reading on separate threads
- **Thread Synchronization** - Message posting for cross-thread communication
- **Cancellation Support** - User-initiated operation termination

### Error Handling
- **Win32 Error Codes** - Direct handling of GetLastError() values
- **User Dialogs** - Error presentation through message boxes and custom dialogs
- **Graceful Degradation** - Fallback behavior for network and hardware failures
- **Operation Logging** - Status bar and progress dialog updates

### Performance Optimizations
- **Lazy Loading** - Directory reading only when needed
- **Caching Strategies** - Drive information and directory content caching
- **Background Operations** - Non-blocking file operations and searches
- **Memory Management** - Custom allocation schemes for file lists

### Compatibility and Standards
- **Windows 10+ Only** - Modern Windows API usage without legacy compatibility
- **Unicode Support** - Full Unicode file names and paths (no ANSI)
- **Long Path Support** - Paths exceeding MAX_PATH limitations
- **Network Standards** - SMB/CIFS network file system support

## File Organization

### Main Application Files
- **Entry Point**: `winfile.cpp` - Application initialization and main message loop
- **Window Procedures**: Individual files for each major window type
- **Resource Files**: `res.rc`, `resource.h` - Dialogs, menus, icons, and strings

### Functional Modules
- **File Operations**: `wfcopy.cpp` - Copy, move, delete operations
- **Directory Handling**: `wfdir.cpp`, `wfdirrd.cpp` - Directory enumeration and display
- **Tree Management**: `wftree.cpp`, `treectl.cpp` - Hierarchical tree navigation
- **Search Functionality**: `wfsearch.cpp` - File search implementation
- **Drive Management**: `wfdrives.cpp` - Drive enumeration and selection

### Utility and Support
- **String Utilities**: `wfutil.cpp` - Path manipulation and string operations
- **Long Filename Support**: `lfn.cpp` - LFN API wrappers and compatibility
- **Network Support**: `wnetcaps.cpp` - Network capability detection
- **Extension Framework**: `wfext.cpp` - Plugin support infrastructure

### Configuration and Data
- **Settings Storage**: Windows registry and INI file support for configuration
- **Icon Resources**: Comprehensive icon sets for files, folders, and drives
- **Help Integration**: Windows Help system integration for user assistance

## Dependencies and External Systems
- **Windows Shell** - Icon extraction, file associations, context menus
- **Network Providers** - SMB/CIFS support through Windows networking
- **File System Drivers** - NTFS, FAT32, network file systems
- **Graphics Subsystem** - GDI for custom drawing and PNG support
- **COM/OLE** - Drag and drop operations and shell integration
- **libheirloom** - Shared library providing cancellation token support for background operations
- **libwinfile** - Static library providing shared functionality for winfile
  - **ArchiveStatus** - Thread-safe status class for archive operations with mutex-protected UI updates
    - **Progress Support** - Enhanced with progress percentage tracking via updateWithProgress() and readWithProgress() methods
    - **Finalization Progress** - Displays real-time percentage completion during ZIP finalization with detailed progress indicators
  - **ZipArchive** - Core ZIP archive functionality with createZipArchive() and extractZipArchive() functions
    - **createZipArchive()** - Creates ZIP archives from files and directories with recursive support and relative path handling
      - **Optimized Processing** - Files are added to ZIP as they are encountered during directory traversal, eliminating upfront file enumeration delays
      - **Real-time Progress** - Progress reporting shows current file being processed without total file counts
      - **Finalization Progress** - Uses libzip progress callbacks to show percentage completion during zip_close() operations
      - **Cancellation Support** - Supports cancellation during zip_close() operations via libzip cancel callbacks
    - **extractZipArchive()** - Extracts ZIP archives to target folders with directory structure preservation
      - **Automatic Overwrite** - Existing files are automatically overwritten without user prompts by ensuring write permissions
      - **Robust File Creation** - Uses std::ios::trunc flag to ensure proper file overwriting
      - **Percentage Progress** - Shows extraction progress as percentage completion based on files processed
    - **Progress Reporting** - Both functions integrate with ArchiveStatus for thread-safe UI progress updates
    - **Error Handling** - Comprehensive exception handling with detailed error messages
      - **Cancellation-Aware** - Exceptions during cancellation are filtered out to prevent spurious error messages
      - **Compression Cancellation** - Cancellation checks are performed before processing each file/folder during compression
    - **Cross-Platform Paths** - Proper UTF-8 path handling and Windows path conversion
    - **Smart Naming** - "Add to Zip" command uses intelligent naming: when creating an archive from a single folder, the archive is named after the selected folder rather than the containing directory; when creating an archive from a single file, the archive is named after the file (without extension) rather than the containing directory
- **libzip** - Library for ZIP archive creation and extraction

## Build System
- **Visual Studio Projects** - Traditional `.vcxproj` files with custom build rules
- **Build Script**: `scripts/build-winfile.sh` - Automated build process
- **Resource Compilation** - Icon processing and resource file compilation
- **Architecture Support** - x64 and ARM64 builds with platform-specific optimizations 