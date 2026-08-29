$ErrorActionPreference = "Stop"

$sourcePath = Join-Path $PSScriptRoot "..\main\fluid_pendant.c"
$source = Get-Content -LiteralPath $sourcePath -Raw

$checks = @(
    @{ Name = "KKD1 center constant"; Pattern = '#define\s+WATCH_KKD1_COMPLICATION_CENTER_X\s+161' },
    @{ Name = "KKD1 time top"; Pattern = '#define\s+WATCH_KKD1_TIME_TOP\s+160' },
    @{ Name = "KKD1 date top"; Pattern = '#define\s+WATCH_KKD1_DATE_TOP\s+187' },
    @{ Name = "KKD2 center constant"; Pattern = '#define\s+WATCH_KKD2_COMPLICATION_CENTER_X\s+161' },
    @{ Name = "KKD2 time top"; Pattern = '#define\s+WATCH_KKD2_TIME_TOP\s+162' },
    @{ Name = "KKD2 date top"; Pattern = '#define\s+WATCH_KKD2_DATE_TOP\s+188' },
    @{ Name = "Visible glyph bounds helper"; Pattern = 'static\s+bool\s+watch_text_visible_bounds' },
    @{ Name = "Visible bounds use glyph box width"; Pattern = 'glyph\.box_w' },
    @{ Name = "Visible bounds ignore empty glyphs"; Pattern = 'glyph\.box_h' },
    @{ Name = "Visible bounds include glyph side bearing"; Pattern = 'glyph\.ofs_x' },
    @{ Name = "KKD1 time uses its layout"; Pattern = 'watch_text_optically_centered\(digital,\s*WATCH_KKD1_COMPLICATION_CENTER_X,\s*WATCH_KKD1_TIME_TOP,\s*&lv_font_montserrat_18' },
    @{ Name = "KKD1 date uses its layout"; Pattern = 'watch_text_optically_centered\(date,\s*WATCH_KKD1_COMPLICATION_CENTER_X,\s*WATCH_KKD1_DATE_TOP,\s*&lv_font_montserrat_14' },
    @{ Name = "KKD2 date is formatted"; Pattern = 'snprintf\(date,\s*sizeof\(date\),\s*"%02d/%02d"' },
    @{ Name = "KKD2 time uses its layout"; Pattern = 'watch_text_optically_centered\(digital,\s*WATCH_KKD2_COMPLICATION_CENTER_X,\s*WATCH_KKD2_TIME_TOP,\s*&lv_font_montserrat_18' },
    @{ Name = "KKD2 date uses its layout"; Pattern = 'watch_text_optically_centered\(date,\s*WATCH_KKD2_COMPLICATION_CENTER_X,\s*WATCH_KKD2_DATE_TOP,\s*&lv_font_montserrat_14' }
)

foreach ($check in $checks) {
    if ($source -notmatch $check.Pattern) {
        throw "Watch complication text alignment check failed: $($check.Name)"
    }
}

Write-Host "Watch complication text alignment checks passed."
