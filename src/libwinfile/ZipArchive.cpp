#include "libwinfile/pch.h"
#include "ZipArchive.h"
#include "ArchiveStatus.h"
#include <thread>
#include <chrono>
#include <atomic>

namespace libwinfile {

namespace {

// Convert std::filesystem::path to UTF-8 string for libzip
std::string pathToUtf8(const std::filesystem::path& path) {
    return path.u8string();
}

// Convert UTF-8 string to std::filesystem::path
std::filesystem::path utf8ToPath(const std::string& utf8String) {
    return std::filesystem::u8path(utf8String);
}

// Callback state for zip_close operations
struct ZipCloseCallbackState {
    ArchiveStatus* status;
    std::wstring zipFilePath;
    std::atomic<bool>* cancelRequested;

    ZipCloseCallbackState(ArchiveStatus* s, const std::wstring& path, std::atomic<bool>* cancel)
        : status(s), zipFilePath(path), cancelRequested(cancel) {}
};

// Progress callback for zip_close
void zipProgressCallback(zip_t* /*archive*/, double progress, void* userData) {
    ZipCloseCallbackState* state = static_cast<ZipCloseCallbackState*>(userData);
    if (state && state->status) {
        state->status->updateWithProgress(state->zipFilePath, L"Compressing...", L"", progress);
    }
}

// Cancel callback for zip_close
int zipCancelCallback(zip_t* /*archive*/, void* userData) {
    ZipCloseCallbackState* state = static_cast<ZipCloseCallbackState*>(userData);
    if (state && state->cancelRequested) {
        return state->cancelRequested->load() ? 1 : 0;
    }
    return 0;
}

// Recursively collect all files and directories from the given paths
void collectFilesRecursive(
    const std::filesystem::path& path,
    const std::filesystem::path& relativeToPath,
    std::vector<std::pair<std::filesystem::path, std::string>>& fileEntries) {
    if (std::filesystem::is_directory(path)) {
        // Add directory entry
        auto relativePath = std::filesystem::relative(path, relativeToPath);
        std::string zipEntryName = pathToUtf8(relativePath);
        // Ensure directory names end with '/'
        if (!zipEntryName.empty() && zipEntryName.back() != '/') {
            zipEntryName += '/';
        }
        fileEntries.emplace_back(path, zipEntryName);

        // Recursively process directory contents
        for (const auto& entry : std::filesystem::recursive_directory_iterator(path)) {
            if (entry.is_directory()) {
                auto relativePath = std::filesystem::relative(entry.path(), relativeToPath);
                std::string zipEntryName = pathToUtf8(relativePath);
                if (!zipEntryName.empty() && zipEntryName.back() != '/') {
                    zipEntryName += '/';
                }
                fileEntries.emplace_back(entry.path(), zipEntryName);
            } else if (entry.is_regular_file()) {
                auto relativePath = std::filesystem::relative(entry.path(), relativeToPath);
                std::string zipEntryName = pathToUtf8(relativePath);
                fileEntries.emplace_back(entry.path(), zipEntryName);
            }
        }
    } else if (std::filesystem::is_regular_file(path)) {
        // Add file entry
        auto relativePath = std::filesystem::relative(path, relativeToPath);
        std::string zipEntryName = pathToUtf8(relativePath);
        fileEntries.emplace_back(path, zipEntryName);
    }
}

// Add files to zip as we encounter them during directory traversal
void addToZipRecursive(
    zip_t* archive,
    const std::filesystem::path& path,
    const std::filesystem::path& relativeToPath,
    ArchiveStatus* status,
    const std::wstring& zipFilePath,
    const libheirloom::CancellationToken& cancellationToken) {
    // Check for cancellation before processing each item
    cancellationToken.throwIfCancellationRequested();

    if (std::filesystem::is_directory(path)) {
        // Add directory entry
        auto relativePath = std::filesystem::relative(path, relativeToPath);
        std::string zipEntryName = pathToUtf8(relativePath);
        // Ensure directory names end with '/'
        if (!zipEntryName.empty() && zipEntryName.back() != '/') {
            zipEntryName += '/';
        }

        status->update(zipFilePath, L"Scanning folder:", path.wstring());
        zip_int64_t idx = zip_dir_add(archive, zipEntryName.c_str(), ZIP_FL_ENC_UTF_8);
        if (idx < 0) {
            throw std::runtime_error("Failed to add directory to zip: " + zipEntryName);
        }

        // Recursively process directory contents
        for (const auto& entry : std::filesystem::directory_iterator(path)) {
            addToZipRecursive(archive, entry.path(), relativeToPath, status, zipFilePath, cancellationToken);
        }
    } else if (std::filesystem::is_regular_file(path)) {
        // Add file entry
        auto relativePath = std::filesystem::relative(path, relativeToPath);
        std::string zipEntryName = pathToUtf8(relativePath);

        status->update(zipFilePath, L"Scanning file:", path.wstring());

        zip_source_t* source = zip_source_file(archive, pathToUtf8(path).c_str(), 0, ZIP_LENGTH_TO_END);
        if (!source) {
            throw std::runtime_error("Failed to create source for file: " + pathToUtf8(path));
        }

        zip_int64_t idx = zip_file_add(archive, zipEntryName.c_str(), source, ZIP_FL_ENC_UTF_8);
        if (idx < 0) {
            zip_source_free(source);
            throw std::runtime_error("Failed to add file to zip: " + zipEntryName);
        }
        // Note: source is managed by libzip after successful zip_file_add
    }
}

}  // anonymous namespace

void createZipArchive(
    const std::filesystem::path& zipFilePath,
    const std::vector<std::filesystem::path>& addFileOrFolderPaths,
    const std::filesystem::path& relativeToPath,
    ArchiveStatus* status,
    const libheirloom::CancellationToken& cancellationToken) {
    if (!status) {
        throw std::invalid_argument("status parameter cannot be null");
    }

    // Create zip archive
    int error = 0;
    zip_t* archive = zip_open(pathToUtf8(zipFilePath).c_str(), ZIP_CREATE | ZIP_TRUNCATE, &error);
    if (!archive) {
        zip_error_t zipError;
        zip_error_init_with_code(&zipError, error);
        std::string errorMsg = "Failed to create zip archive: " + std::string(zip_error_strerror(&zipError));
        zip_error_fini(&zipError);
        throw std::runtime_error(errorMsg);
    }

    try {
        status->update(zipFilePath.wstring(), L"Starting compression...", L"");

        // Process files as we encounter them instead of collecting them first
        for (const auto& path : addFileOrFolderPaths) {
            addToZipRecursive(archive, path, relativeToPath, status, zipFilePath.wstring(), cancellationToken);
        }

        // Close archive (this writes the zip file)
        status->update(zipFilePath.wstring(), L"Compressing...", L"");

        // Set up callbacks for progress and cancellation during zip_close
        std::atomic<bool> cancelRequested{ false };

        // Check if cancellation token has a way to access the underlying atomic bool
        // For now, we'll use a separate atomic bool and check cancellation before calling zip_close
        if (cancellationToken.isCancellationRequested()) {
            cancelRequested = true;
        }

        ZipCloseCallbackState callbackState(status, zipFilePath.wstring(), &cancelRequested);

        // Register progress callback (precision 0.01 = 1% increments)
        zip_register_progress_callback_with_state(archive, 0.01, zipProgressCallback, nullptr, &callbackState);

        // Register cancel callback
        zip_register_cancel_callback_with_state(archive, zipCancelCallback, nullptr, &callbackState);

        // Start a thread to monitor cancellation token and update our atomic bool
        std::thread cancellationMonitor([&cancelRequested, &cancellationToken]() {
            while (!cancelRequested.load()) {
                if (cancellationToken.isCancellationRequested()) {
                    cancelRequested = true;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });

        // Close the archive
        int closeResult = zip_close(archive);

        // Stop the cancellation monitor
        cancelRequested = true;
        if (cancellationMonitor.joinable()) {
            cancellationMonitor.join();
        }

        if (closeResult < 0) {
            throw std::runtime_error("Failed to close zip archive: " + std::string(zip_strerror(archive)));
        }

        status->update(zipFilePath.wstring(), L"Compression complete.", L"");
    } catch (const libheirloom::OperationCanceledException&) {
        // Clean up on cancellation - ignore the exception if cancellation was requested
        zip_discard(archive);
        if (cancellationToken.isCancellationRequested()) {
            throw;  // Re-throw the cancellation exception
        }
        // If cancellation wasn't requested, treat it as a regular error
        throw;
    } catch (...) {
        // Clean up on error
        zip_discard(archive);
        // If cancellation was requested, ignore other exceptions as they're likely related to cancellation
        if (cancellationToken.isCancellationRequested()) {
            throw libheirloom::OperationCanceledException();
        }
        throw;
    }
}

void extractZipArchive(
    const std::filesystem::path& zipFilePath,
    const std::filesystem::path& targetFolder,
    ArchiveStatus* status) {
    if (!status) {
        throw std::invalid_argument("status parameter cannot be null");
    }

    // Open zip archive
    int error = 0;
    zip_t* archive = zip_open(pathToUtf8(zipFilePath).c_str(), ZIP_RDONLY, &error);
    if (!archive) {
        zip_error_t zipError;
        zip_error_init_with_code(&zipError, error);
        std::string errorMsg = "Failed to open zip archive: " + std::string(zip_error_strerror(&zipError));
        zip_error_fini(&zipError);
        throw std::runtime_error(errorMsg);
    }

    try {
        // Get number of entries
        zip_int64_t numEntries = zip_get_num_entries(archive, 0);
        if (numEntries < 0) {
            throw std::runtime_error("Failed to get number of entries in zip archive");
        }

        status->update(zipFilePath.wstring(), L"Starting extraction...", L"");

        // Create target directory if it doesn't exist
        std::filesystem::create_directories(targetFolder);

        // Extract each entry
        for (zip_int64_t i = 0; i < numEntries; ++i) {
            zip_stat_t stat;
            zip_stat_init(&stat);
            if (zip_stat_index(archive, i, 0, &stat) < 0) {
                throw std::runtime_error("Failed to get file info for entry " + std::to_string(i));
            }

            std::string entryName = stat.name;
            std::filesystem::path entryPath = targetFolder / utf8ToPath(entryName);

            // Update progress with percentage
            double progress = static_cast<double>(i) / static_cast<double>(numEntries);
            std::wstring progressText = L"Extracting file:";
            status->updateWithProgress(zipFilePath.wstring(), progressText, entryPath.wstring(), progress);

            // Check if it's a directory (ends with '/')
            if (!entryName.empty() && entryName.back() == '/') {
                // Create directory
                std::filesystem::create_directories(entryPath);
            } else {
                // Create parent directories if needed
                std::filesystem::create_directories(entryPath.parent_path());

                // Extract file
                zip_file_t* file = zip_fopen_index(archive, i, 0);
                if (!file) {
                    throw std::runtime_error("Failed to open file in zip: " + entryName);
                }

                try {
                    // Ensure we can overwrite the file if it exists
                    if (std::filesystem::exists(entryPath)) {
                        std::filesystem::permissions(
                            entryPath, std::filesystem::perms::owner_write, std::filesystem::perm_options::add);
                    }

                    // Create output file
                    std::ofstream outFile(entryPath, std::ios::binary | std::ios::trunc);
                    if (!outFile.is_open()) {
                        throw std::runtime_error("Failed to create output file: " + pathToUtf8(entryPath));
                    }

                    // Copy data
                    constexpr size_t bufferSize = 1048576;
                    auto buffer = std::make_unique<char[]>(bufferSize);
                    zip_int64_t bytesRead;
                    while ((bytesRead = zip_fread(file, buffer.get(), bufferSize)) > 0) {
                        outFile.write(buffer.get(), bytesRead);
                        if (outFile.fail()) {
                            throw std::runtime_error("Failed to write to output file: " + pathToUtf8(entryPath));
                        }
                    }

                    if (bytesRead < 0) {
                        throw std::runtime_error("Failed to read from zip file: " + entryName);
                    }

                    outFile.close();
                } catch (...) {
                    zip_fclose(file);
                    throw;
                }

                zip_fclose(file);
            }
        }

        status->update(zipFilePath.wstring(), L"Extraction complete.", L"");
    } catch (...) {
        zip_close(archive);
        throw;
    }

    zip_close(archive);
}

}  // namespace libwinfile