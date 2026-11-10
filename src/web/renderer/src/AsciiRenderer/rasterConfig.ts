export const BROWSER_RASTER_FONT_SIZE_PT = 12;

// Matrix's wider glyph advances produce 16x32 cells at 12pt. At 7.5pt its
// cells match the default font's 10x20 pixels, preserving the automatic grid.
export function browserRasterFontSize(matrix: boolean): number {
  return matrix ? 7.5 : BROWSER_RASTER_FONT_SIZE_PT;
}
