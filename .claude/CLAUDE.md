# Heirloom apps
- This is two different apps: winfile and progman.
- C++ 17
- Vanilla Win32
- We only support Windows 10 and above. Don't support older systems.
- We only support Unicode. Don't support ANSI. Use WCHAR over TCHAR, LPWSTR over LPTSTR, L"" over TEXT("").
- Do not run the apps. I will run them myself in the debugger for testing.
- Never use the pImpl pattern.
- If you get `error C3859: Failed to create virtual memory for PCH` or `error C1076: compiler limit: internal heap limit reached`, just run the build again. This is not a problem with your code.

## File Manager (winfile)
- Codebase documentation: `doc/winfile-overview.md`. Important: Keep this updated as you modify the codebase!
- Directory: `src/winfile/`
- Always run the build with `bash src/build-winfile.sh` after making changes.
- This is the classic Windows File Manager codebase.
- When creating new source files, OMIT the file-level comment header.

## Program Manager (progman)
- Codebase documentation: `doc/program-overview.md`. Important: Keep this updated as you modify the codebase!
- Directories: `src/progman/`, `src/libprogman/`, `src/libprogman_tests`
- Always run the build and tests with `bash src/build-progman.sh` after making changes.
- Prefer the C++ Standard Library like std::wstring over old fashioned C like WCHAR* or LPWSTR.
- Use smart pointer classes. Avoid "new".
- Only use old fashioned C conventions when interfacing with Win32 API.
- Never use Hungarian notation.
- Never use C-style casts.
- Member variable names have trailing underscores: fooBar_
- All functions use camelCase: fooBar()
- Use exceptions for error handling. Convert error codes to exceptions ASAP.
- Use wil (Windows Implementation Library) classes for Win32 RAII. Use THROW_IF_FAILED() to handle HRESULTs. Use throwing wrappers.
- Use an immutable style for data classes.
- Add system #includes to the project precompiled header: src/progman/pch.h or src/libprogman/pch.h.
- Use std::min/std::max because we define NOMINMAX.
- Use constructor dependency injection. The DI graph is constructed in [progman.cpp](mdc:src/progman/progman.cpp)
- Use CancellationToken and OperationCanceledException [cancel.h](mdc:src/libheirloom/cancel.h) for cancellation; it works the same as in C#.
- Use setWindowData/getWindowData in libprogman/ [window_data.h](mdc:src/libprogman/window_data.h) to associate a backing pointer with an HWND.
- Define the WndProc as a `friend` of the backing class so it can call private methods.

# Tool Instructions
- `Bash`: Always use `/` forward slashes in paths, not `\` slashes.
