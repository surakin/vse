/*
 * The engine's module list (replaces the file FreeType's build generates from modules.cfg; see third_party/freetype/VENDORED.md).
 *
 * TrueType and OpenType/CFF outlines, the smooth (anti-aliased) rasterizer and the auto-hinter, nothing else: no Type 1, CID, PFR, PCF,
 * BDF, FNT, SDF, SVG or monochrome renderer, and no compressed-font, OpenType-validation or GX-validation modules.
 */
FT_USE_MODULE( FT_Module_Class, autofit_module_class )
FT_USE_MODULE( FT_Driver_ClassRec, tt_driver_class )
FT_USE_MODULE( FT_Driver_ClassRec, cff_driver_class )
FT_USE_MODULE( FT_Module_Class, psaux_module_class )
FT_USE_MODULE( FT_Module_Class, psnames_module_class )
FT_USE_MODULE( FT_Module_Class, pshinter_module_class )
FT_USE_MODULE( FT_Module_Class, sfnt_module_class )
FT_USE_MODULE( FT_Renderer_Class, ft_smooth_renderer_class )
