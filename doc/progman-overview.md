# Program Manager (progman) - High-Level Design Overview

## Overview
The Program Manager (progman) is a modern Windows desktop application that recreates the classic Windows Program Manager experience. It provides a folder-based interface for organizing and launching applications through shortcuts. The application follows modern C++ practices with dependency injection, smart pointers, immutable data structures, and exception-based error handling.

## Architecture

### Two-Tier Architecture
The application is structured as two main components:

1. **UI Layer** (`src/progman/`) - Windows-specific UI components and dialogs
2. **Business Logic Layer** (`src/libprogman/`) - Platform-agnostic business logic and data management

### Dependency Injection
The application uses constructor dependency injection with the DI graph constructed in `progman.cpp`. This enables loose coupling and testability.

## Core Components

### Business Logic Layer (`libprogman/`)

#### Data Models
- **`Shortcut`** - Represents a `.lnk` file with path, icon, and metadata
- **`ShortcutFolder`** - Represents a folder containing shortcuts (immutable)
- Uses immutable data structures from the `immer` library for thread-safety

#### Management Services
- **`ShortcutManager`** - Central manager for the folder/shortcut hierarchy
  - Manages shortcuts in `%APPDATA%\Heirloom Program Manager\Shortcuts\`
  - Supports folder operations (create, rename, delete)
  - Thread-safe with mutex protection
  - Single-level folder structure (no nesting)

- **`ShortcutFactory`** - Factory for creating `Shortcut` objects
  - Extracts icons and metadata from `.lnk` files
  - Handles Win32 shell link operations

- **`InstalledAppList`** - Discovers installed applications
  - Scans system Start Menu folders
  - Supports cancellation tokens for long operations
  - Thread-safe app discovery and caching

#### Infrastructure Services
- **`FolderWatcher`** - Monitors filesystem changes
  - Watches the shortcuts folder for changes
  - Triggers UI refresh when files change
  - Runs on background thread

- **Utility Classes**
  - `window_data` - Associates backing objects with HWNDs
  - `window_state` - Saves/restores window positions and sizes
  - `string_util` - UTF-8/UTF-16 string conversions
  - `cancel` - Cancellation token system (C#-style)
  - `Error` - Custom exception types

### UI Layer (`progman/`)

#### Main Window
- **`ProgramManagerWindow`** - MDI frame window
  - Hosts multiple `FolderWindow` instances
  - Manages window menu and accelerators
  - Handles splitter between folder list and MDI client
  - Coordinates between UI components

#### Child Windows
- **`FolderWindow`** - MDI child window showing folder contents
  - Displays shortcuts in a list view
  - Handles item selection, renaming, launching
  - Supports minimization to folder list
  - Saves/restores window state per folder
  - Supports drag and drop of files and shortcuts

- **`MinimizedFolderListControl`** - Shows minimized folders
  - List control for minimized folder windows
  - Supports splitter resize functionality
  - Double-click to restore folders
  - F2 key to rename selected minimized folder
  - Ctrl+D key to delete selected minimized folder
  - Right-click context menu with Open, Rename, Delete options
  - Prevents duplicate folder icons during rename by deferring UI updates to filesystem watcher

#### Dialog Windows
- **`AboutDialog`** - Application about dialog
- **`NewFolderDialog`** - Create new folder dialog
- **`NewShortcutDialog`** - Create shortcut from installed apps dialog
- **`FindingAppsDialog`** - Progress dialog for app discovery

#### Help Menu
- **About** - Shows application information dialog
- **Visit Website** - Opens `https://heirloomapps.com` in the default browser using ShellExecute

## Key Features

### Folder Management
- Create, rename, and delete folders
- Single-level folder hierarchy (no nested folders)
- Automatic folder creation on first run
- Folder state persistence (window positions, minimization state)

### Shortcut Management
- Launch shortcuts via double-click or Enter key
- Create shortcuts from discovered installed applications
- Delete shortcuts via context menu or Delete key
- Rename shortcuts via F2 key
- Show shortcut properties

### Drag and Drop Support
- **Drop Target Support** - Accept files and folders from Windows Explorer
  - Automatically creates shortcuts for files and folders
  - Copies existing `.lnk` files to the folder (always copy from external sources)
  - Moves `.lnk` files between folder windows (internal move operations)
  - Prevents duplicate shortcuts by generating unique names
- **Drag Source Support** - Drag shortcuts between folder windows only
  - Move shortcuts between folder windows (DROPEFFECT_MOVE)
  - External drag operations are not supported to simplify the implementation
  - Automatic source file cleanup for move operations
- **Implementation** - Uses Windows OLE drag and drop APIs
  - `DropTarget` class implements `IDropTarget` interface with internal/external drag detection
  - `DragSource` class implements `IDropSource` interface for internal-only operations
  - `DataObject` class implements `IDataObject` interface with custom clipboard format for internal identification
  - Custom clipboard format "ProgmanInternalDrag" distinguishes internal from external drag sources
  - Requires OLE initialization via `OleInitialize()` before registering drop targets
  - Handles `LVN_BEGINDRAG` notification to initiate drag operations
  - `DragOver` method re-determines drag effects based on stored data object reference from `DragEnter` to ensure consistent behavior
  - `Drop` method checks internal vs external drag source to determine whether to delete source files (only internal drags delete sources)

### Application Discovery
- Scans system and user Start Menu folders
- Extracts application metadata and icons
- Cancellable background scanning
- Caches discovered applications for performance

### User Interface
- Modern MDI interface with folder windows
- Resizable splitter between folder list and workspace
- Keyboard shortcuts and accelerators
- Context menus for operations
- Minimized folder list with restore functionality
- Dynamic menu state management with context-sensitive command enabling

### Menu State Management
- **Dynamic Command Enabling** - Menu commands are enabled/disabled based on current application state
  - "New shortcut" - Enabled only when a folder window is focused and active
  - "Rename" - Enabled only when a shortcut icon or minimized folder icon is selected
  - "Delete" - Enabled when a folder window is focused or a minimized folder icon is selected
  - "Cascade" and "Tile" - Enabled only when there is at least one restored window
- **Real-time Updates** - Menu states update automatically when:
  - Folder windows are created, minimized, restored, or closed
  - Selection changes in folder windows or the minimized folder list
  - Focus changes between folder windows and the minimized folder list
- **Implementation** - Uses callback system to notify main window of state changes
  - `FolderWindow` notifies on focus and selection changes
  - `MinimizedFolderListControl` notifies on focus and selection changes
  - `ProgramManagerWindow::updateMenuStates()` centralizes all menu state logic

### Filesystem Integration
- Real-time monitoring of shortcuts folder
- Automatic refresh when files change externally
- Proper handling of file system errors
- Unicode support throughout

## Technical Details

### Threading Model
- Main UI thread for all window operations
- Background thread for folder watching
- Background thread for app discovery (with cancellation)
- Thread-safe data structures using immutable collections

### Error Handling
- Exception-based error handling throughout
- HRESULT to exception conversion using WIL `THROW_IF_FAILED`
- Custom `Error` exception types with error codes
- User-friendly error dialogs

### Memory Management
- Smart pointers (`std::unique_ptr`, `std::shared_ptr`) for automatic cleanup
- RAII wrappers for Win32 resources using WIL
- No raw `new`/`delete` usage
- Immutable data structures prevent many memory issues

### Coding Standards
- Modern C++17 features
- camelCase for functions, trailing underscore for members
- No Hungarian notation
- No C-style casts
- Exception-based error handling
- Dependency injection pattern

## File Organization

### Configuration and Data
- Shortcuts stored in: `%APPDATA%\Heirloom Program Manager\Shortcuts\`
- Window state saved in: `%APPDATA%\Heirloom Program Manager\window_state.ini` 
- Scans for installed apps in system and user Start Menu folders

### Dependencies
- Windows 10+ only (no legacy OS support)
- Unicode-only (no ANSI support)
- WIL (Windows Implementation Library) for Win32 RAII
- immer library for immutable data structures
- Standard C++ library preferred over Win32 C-style APIs

## Build System
- Visual Studio project files
- Build script: `scripts/build-progman.sh`
- Includes unit tests in `src/libprogman_tests/` 