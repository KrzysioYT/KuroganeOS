# DejaVu Sans atlas source

The regular and bold TTF files here are unmodified DejaVu Sans font software.
They were obtained from the Debian `fonts-dejavu-core` package and are
redistributed under the Bitstream Vera license; see [`COPYRIGHT`](COPYRIGHT).
The same notice is installed into the system image at
`/usr/share/lic/DEJAVU/COPYING.TXT` (the path follows the installer's FAT 8.3
name contract).

The freestanding renderer uses a generated, grayscale 4-bit glyph atlas for
ASCII, Latin-1, Polish Latin Extended-A letters, and U+FFFD. It contains normal
and bold faces at 8, 10, 12, 16, 20, 24 and 32 pixels. It is not a full Unicode
font service and does not provide shaping or kerning.

Regenerate the checked-in atlas with Pillow 12.3.0 and FreeType 2.14.3:

```powershell
python -m pip install -r scripts/requirements-font-atlas.txt
python scripts/generate-font-atlas.py `
  --normal third_party/fonts/dejavu/DejaVuSans.ttf `
  --bold third_party/fonts/dejavu/DejaVuSans-Bold.ttf `
  --output kernel/drivers/font_atlas.generated.hpp
```

The normal source SHA-256 is
`b4c632e3cdf9acc7f28758fb5a323c8524d7fc6660d46904d9b6cbe2809c419c`; the bold
source SHA-256 is
`e1d733afbbfce842a1b43dced0c9885a76a4e67290dc7e690c4e9b76ebfc7d23`.
