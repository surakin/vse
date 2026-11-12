#import <Metal/Metal.h>
// The Metal API outside engine/rhi/metal/: an include (line 1) and names (lines 3 and 5) are each a violation, as are
id<MTLDevice> LeakedDevice();
const char* kTag = "MTLB";  // the library file tag is not an API name
const long kFamily = MTLGPUFamilyMac2;  // an acronym after MTL
#import <MetalKit/MetalKit.h>
@import Metal;
// MetalKit (line 6) and the module import (line 7).
