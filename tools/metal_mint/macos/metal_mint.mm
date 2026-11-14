// metal_mint (Phase 21 Day 299): compiles the Metal Shading Language that `slangc -target metal` wrote into an
// AIR-only, executable `.metallib`, with the driver's own compiler: the interim route the user chose on Day 282
// (pending until they have a new Mac), the undocumented -[MTLLibrary serializeToURL:error:] of an ordinary library. It
// is the one place that route is used, so a later route (Xcode's `metal`) replaces this file.
//
//   metal_mint --source FILE.metal --stage vertex|fragment|compute --out FILE.metallib --info FILE.info
//
// The output is checked before it is written: it loads, it is an executable library, it exports exactly one function
// and that function is of the stage's type. `--info` receives three lines that shader_mint records beside the blob:
//
//   entry main_0
//   compiler metalfe-32023.622
//   macos Version 15.8.1 (Build 24G...)
//
// The minted bytes depend on the driver compiler's version and the macOS build (they embed the compiler version and a
// target triple such as air64-apple-macosx15.8.0; Day 282), so the same source mints the same bytes on the same macOS
// build only. A missing or failing selector is an error that names KNOWN-ISSUES.md, never a crash. Dev-time only: never
// linked into the engine (PLAN.md assumption #18).
#include "cstr/cstr.h"
#include "debug/failure_hooks.h"
#include "io/file.h"
#include "io/file_system.h"
#include "io/path.h"
#include "mem/heap_allocator.h"
#include "os/command_line.h"
#include "os/console.h"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

using namespace engine_placeholder;

namespace
{

int Usage()
{
    os::PrintErrorText(
        "usage: metal_mint --source FILE.metal --stage vertex|fragment|compute --out FILE.metallib --info FILE.info\n");
    return 2;
}

// An error's description, or a placeholder when the call gave none (appending nil to a string would raise).
NSString* Reason(NSError* error)
{
    return error != nil && error.description != nil ? error.description : @"no reason given";
}

int Fail(const char* source, NSString* what)
{
    os::PrintError("metal_mint: %s: %s\n", source, what.UTF8String);
    return 1;
}

io::Path PathOf(const char* text)
{
    io::Path path;
    io::Path::Parse(StringView(text), io::NativePathStyle(), &path);
    return path;
}

// The driver compiler's version, as the library embeds it ("metalfe-32023.622"), or "unknown".
NSString* CompilerVersion(NSData* bytes)
{
    const char* data = static_cast<const char*>(bytes.bytes);
    const NSUInteger size = bytes.length;
    const char tag[] = "metalfe-";
    const usize tag_length = sizeof(tag) - 1;
    for (NSUInteger i = 0; i + tag_length < size; ++i)
    {
        if (MemCompare(data + i, tag, tag_length) != 0)
        {
            continue;
        }
        NSUInteger end = i + tag_length;
        while (end < size && ((data[end] >= '0' && data[end] <= '9') || data[end] == '.'))
        {
            ++end;
        }
        return [[NSString alloc] initWithBytes:data + i length:end - i encoding:NSASCIIStringEncoding];
    }
    return @"unknown";
}

MTLFunctionType FunctionTypeOf(StringView stage)
{
    if (stage == StringView("vertex"))
    {
        return MTLFunctionTypeVertex;
    }
    if (stage == StringView("fragment"))
    {
        return MTLFunctionTypeFragment;
    }
    return MTLFunctionTypeKernel;
}

}  // namespace

int main(int argc, char** argv)
{
    debug::InstallDiagnosticsHooks();
    static const os::CommandLineOption cli_options[] = {
        {"source", true}, {"stage", true}, {"out", true}, {"info", true}};
    const os::CommandLine<HeapAllocator> line(HeapAllocator::Default(), argc, argv,
                                              Span<const os::CommandLineOption>(cli_options, 4));
    StringView source_value;
    StringView stage_value;
    StringView out_value;
    StringView info_value;
    if (!line.Ok() || line.ArgCount() != 0 || !line.TryGetOption(StringView("source"), source_value) ||
        !line.TryGetOption(StringView("stage"), stage_value) || !line.TryGetOption(StringView("out"), out_value) ||
        !line.TryGetOption(StringView("info"), info_value))
    {
        return Usage();
    }
    const char* source = source_value.Data();
    const char* stage = stage_value.Data();
    const char* out = out_value.Data();
    const char* info = info_value.Data();
    if (StringView(stage) != StringView("vertex") && StringView(stage) != StringView("fragment") &&
        StringView(stage) != StringView("compute"))
    {
        return Usage();
    }
    @autoreleasepool
    {
        Vector<u8> text;
        if (!io::ReadFile(PathOf(source), &text).Ok())
        {
            return Fail(source, @"cannot read the Metal source");
        }
        NSString* msl = [[NSString alloc] initWithBytes:text.Data() length:text.Size() encoding:NSUTF8StringEncoding];
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (msl == nil || device == nil)
        {
            return Fail(source, msl == nil ? @"the source is not UTF-8" : @"no Metal device on this Mac");
        }
        MTLCompileOptions* options = [[MTLCompileOptions alloc] init];
        options.languageVersion = MTLLanguageVersion3_0;  // macOS 13, the deployment target
        options.libraryType = MTLLibraryTypeExecutable;
        NSError* error = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:msl options:options error:&error];
        if (library == nil)
        {
            return Fail(source, [@"the driver's compiler refused the source: " stringByAppendingString:Reason(error)]);
        }
        // The undocumented call (Day 282). Guarded: a macOS without it, or a failure, is a clear error.
        const SEL serialize = NSSelectorFromString(@"serializeToURL:error:");
        if (![library respondsToSelector:serialize])
        {
            return Fail(source,
                        @"this macOS's MTLLibrary has no serializeToURL:error: (the interim minting route of Phase 21 "
                        @"Day 282 is gone): see KNOWN-ISSUES.md; mint on another Mac or move to Xcode's metal");
        }
        NSString* temp_path = [NSString stringWithFormat:@"%s.serializing", out];
        NSURL* temp_url = [NSURL fileURLWithPath:temp_path];
        using SerializeFn = BOOL (*)(id, SEL, NSURL*, NSError**);
        const IMP implementation = [static_cast<NSObject*>(library) methodForSelector:serialize];
        const SerializeFn call = reinterpret_cast<SerializeFn>(implementation);
        error = nil;
        const BOOL serialized = call(library, serialize, temp_url, &error);
        Vector<u8> minted;
        const bool read = serialized && io::ReadFile(PathOf(temp_path.UTF8String), &minted).Ok();
        io::Remove(PathOf(temp_path.UTF8String));
        if (!read)
        {
            return Fail(source, [@"serializeToURL:error: failed (see KNOWN-ISSUES.md): "
                                    stringByAppendingString:(error != nil ? Reason(error) : @"no file")]);
        }
        // Checked before it is written: the tag, an executable library, loading, one function of the stage's type.
        if (minted.Size() < 32 || minted[0] != 'M' || minted[1] != 'T' || minted[2] != 'L' || minted[3] != 'B' ||
            minted[10] != 0)
        {
            return Fail(source, @"the serialized file is not an executable Metal library (MTLB, type 0)");
        }
        NSData* bytes = [NSData dataWithBytes:minted.Data() length:minted.Size()];
        dispatch_data_t data = dispatch_data_create(bytes.bytes, bytes.length, nil, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
        error = nil;
        id<MTLLibrary> loaded = [device newLibraryWithData:data error:&error];
        if (loaded == nil || loaded.type != MTLLibraryTypeExecutable)
        {
            return Fail(source, loaded == nil
                                    ? [@"the minted library does not load: " stringByAppendingString:Reason(error)]
                                    : @"the minted library loads as a dynamic library");
        }
        if (loaded.functionNames.count != 1)
        {
            return Fail(source,
                        [NSString stringWithFormat:@"the minted library exports %lu functions, not one entry point",
                                                   static_cast<unsigned long>(loaded.functionNames.count)]);
        }
        NSString* entry = loaded.functionNames[0];
        id<MTLFunction> function = [loaded newFunctionWithName:entry];
        if (function == nil || function.functionType != FunctionTypeOf(StringView(stage)))
        {
            return Fail(source,
                        [NSString stringWithFormat:@"the minted function '%@' is not a %s function", entry, stage]);
        }
        NSString* info_text = [NSString stringWithFormat:@"entry %@\ncompiler %@\nmacos %@\n", entry,
                                                         CompilerVersion(bytes),
                                                         [NSProcessInfo processInfo].operatingSystemVersionString];
        const char* info_bytes = info_text.UTF8String;
        if (!io::WriteFileAtomic(PathOf(out), minted.Data(), minted.Size(), 0644).Ok() ||
            !io::WriteFileAtomic(PathOf(info), info_bytes, CStrLength(info_bytes), 0644).Ok())
        {
            return Fail(source, @"cannot write the library or its info file");
        }
    }
    return 0;
}
