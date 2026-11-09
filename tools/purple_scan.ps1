# Counts "purple" pixels (b > g+25, r > g+15, g < 10) in frame_*.png with vblank >= MinVblank.
# GS pipeline race check (purple 8x8 blocks): run it on a TTR_FRAME_DUMP folder after any GS change.
# usage: powershell -ExecutionPolicy Bypass -File tools/purple_scan.ps1 <folder> [MinVblank]
param([string]$Folder, [int]$MinVblank = 1100)
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class PurpleScan {
    public static int Count(string path) {
        using (var bmp = new Bitmap(path)) {
            var rect = new Rectangle(0, 0, bmp.Width, bmp.Height);
            var data = bmp.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            var bytes = new byte[data.Stride * bmp.Height];
            Marshal.Copy(data.Scan0, bytes, 0, bytes.Length);
            bmp.UnlockBits(data);
            int count = 0;
            for (int i = 0; i < bytes.Length; i += 4) {
                int b = bytes[i], g = bytes[i + 1], r = bytes[i + 2];
                if (g < 10 && b > g + 25 && r > g + 15) count++;
            }
            return count;
        }
    }
}
'@
$total = 0
$bad = @()
foreach ($f in Get-ChildItem $Folder -Filter 'frame_*.png' | Sort-Object Name) {
    $vb = [int]($f.BaseName -replace 'frame_', '')
    if ($vb -lt $MinVblank) { continue }
    $count = [PurpleScan]::Count($f.FullName)
    if ($count -gt 0) { $bad += "$($f.BaseName)=$count" }
    $total++
}
"frames=$total with-purple=$($bad.Count) $($bad -join ' ')"
