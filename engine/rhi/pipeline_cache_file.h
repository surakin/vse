#pragma once

#include "containers/vector.h"
#include "types.h"

//! Where a backend keeps its on-disk pipeline cache, and how the file is read and written (moved out of the Vulkan
//! backend on Phase 13 Day 167, so the Direct3D 12 backend follows the same rules). What the file holds, and how it is
//! checked, is each backend's own.

namespace engine_placeholder::rhi
{

struct DeviceCreateInfo;

/// The outcome of [[rhi::ChoosePipelineCacheDir]].
enum class PipelineCacheDirChoice : u8
{
    NotConfigured,  ///< Neither source is set: the caller uses the user's cache directory.
    Chosen,         ///< A directory was chosen and copied to the output.
    TooLong,        ///< A source is set but its directory does not fit: run without a cache (never fall back to another
                    ///< directory).
};

/// Chooses the directory for the pipeline cache from the two explicit sources.
///
/// In order: `explicit_dir` if non-empty, then `$ENGINE_RHI_CACHE_DIR`. When neither is set the caller uses the user's
/// cache directory for the application ([[io::UserCacheDirectory]], with [[rhi::SanitizePipelineCacheAppName]]).
///
/// - Param explicit_dir: a directory from the device descriptor, or null.
/// - Param env_cache_dir: value of `ENGINE_RHI_CACHE_DIR`, or null.
/// - Param out: receives the nul-terminated directory.
/// - Param capacity: size of `out` in bytes.
/// - Returns: which of the three outcomes happened.
/// - Thread-safety: thread-safe. A pure function; it does not read the environment itself.
/// - Ownership: the inputs are borrowed for the call; the caller owns `out`.
PipelineCacheDirChoice ChoosePipelineCacheDir(const char* explicit_dir, const char* env_cache_dir, char* out,
                                              usize capacity);

/// Turns an application name into one path component: slashes become underscores, a null or empty name becomes
/// `engine`.
///
/// - Param app: the application name, or null.
/// - Param out: receives the nul-terminated name.
/// - Param capacity: size of `out` in bytes.
/// - Returns: true if it fit.
/// - Fails: returns false, leaving `out` empty, if the name does not fit.
/// - Thread-safety: thread-safe. A pure function.
/// - Ownership: `app` is borrowed for the call; the caller owns `out`.
bool SanitizePipelineCacheAppName(const char* app, char* out, usize capacity);

/// Finds the directory a device keeps its pipeline cache in: [[rhi::DeviceCreateInfo]]'s `pipeline_cache_dir`, else
/// `$ENGINE_RHI_CACHE_DIR`, else the user's cache directory for `application_name`.
///
/// - Param info: the device's creation info.
/// - Param out: receives the nul-terminated native directory.
/// - Param capacity: size of `out` in bytes.
/// - Returns: true if a directory was found.
/// - Fails: false, leaving `out` empty, when a chosen directory does not fit or the user's cache directory is unknown;
/// the device then runs
///   without a cache.
/// - Thread-safety: thread-safe (reads the environment).
/// - Ownership: `info` is borrowed for the call; the caller owns `out`.
bool ResolvePipelineCacheDir(const DeviceCreateInfo& info, char* out, usize capacity);

/// Reads a whole pipeline cache file.
///
/// - Param path: the native path.
/// - Param out: receives the bytes; empty when the call fails.
/// - Returns: true if the file was read.
/// - Fails: false if it does not exist, is not a regular file (a directory or a pipe must not stall start-up), is empty
/// or larger than
///   256 MiB, or cannot be read.
/// - Thread-safety: thread-safe.
/// - Ownership: the caller owns `out`; its old contents are replaced.
bool ReadPipelineCacheFile(const char* path, Vector<byte>* out);

/// Creates the cache directory and writes the file whole, atomically: a reader sees the old cache or the new one.
///
/// - Param path: the native path.
/// - Param data: the bytes.
/// - Param size: how many.
/// - Returns: true if the file was written.
/// - Fails: false if the path does not parse or the directory or the file cannot be written.
/// - Thread-safety: thread-safe for distinct paths; callers serialize writes to one path.
/// - Ownership: `data` is borrowed for the call.
bool WritePipelineCacheFile(const char* path, const void* data, usize size);

}  // namespace engine_placeholder::rhi
