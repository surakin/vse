#include "renderer/terrain_source.h"

#include "lang/scalar.h"

namespace vse::renderer
{

bool TerrainWorldValid(const Mat4& m)
{
    for (u32 c = 0; c < 4; ++c)
    {
        for (u32 r = 0; r < 4; ++r)
        {
            if (!IsFinite(m.col[c][r]))
            {
                return false;
            }
        }
    }
    const f32 eps = 1e-4f;
    const bool y_axis = Abs(m.col[1][0]) < eps && Abs(m.col[1][1] - 1.0f) < eps && Abs(m.col[1][2]) < eps;
    const bool x_axis = Abs(m.col[0][1]) < eps &&
                        Abs(m.col[0][0] * m.col[0][0] + m.col[0][2] * m.col[0][2] - 1.0f) < eps;
    const bool z_axis = Abs(m.col[2][1]) < eps &&
                        Abs(m.col[2][0] * m.col[2][0] + m.col[2][2] * m.col[2][2] - 1.0f) < eps;
    const bool square = Abs(m.col[0][0] * m.col[2][0] + m.col[0][2] * m.col[2][2]) < eps;
    const bool bottom = Abs(m.col[0][3]) < eps && Abs(m.col[1][3]) < eps && Abs(m.col[2][3]) < eps &&
                        Abs(m.col[3][3] - 1.0f) < eps;
    return x_axis && y_axis && z_axis && square && bottom;
}

}  // namespace vse::renderer
