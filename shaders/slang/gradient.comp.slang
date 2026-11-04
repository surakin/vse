// gradient.comp: writes pixel (x, y) = (x / 255, y / 255, 0, 1) into a storage image. Used by the RHI compute tests.
//
// Interface
//   local size 8 x 8 x 1; dispatch ceil(width / 8) x ceil(height / 8) workgroups (extra invocations are ignored)
//   set 0, binding 0: storage image, rgba8, write only
//   no push constants

[[vk::binding(0, 0)]] [[vk::image_format("rgba8")]] WTexture2D<float4> img;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint width, height;
    img.GetDimensions(width, height);
    if (id.x < width && id.y < height) {
        img.Store(id.xy, float4(float(id.x) / 255.0, float(id.y) / 255.0, 0.0, 1.0));
    }
}
