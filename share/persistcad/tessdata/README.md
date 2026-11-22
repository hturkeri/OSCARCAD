# Tesseract language data (offline, shipped with PersistCAD)

| File | Source | Licence | SHA-256 |
|---|---|---|---|
| `eng.traineddata` | tesseract-ocr/tessdata_fast (LSTM, via Debian/Ubuntu `tesseract-ocr-eng`) | Apache-2.0 | `7d4322bd2a7749724879683fc3912cb542f19906c83bcc1a52132556427170b2` |
| `tur.traineddata` | tesseract-ocr/tessdata_fast (LSTM, via Debian/Ubuntu `tesseract-ocr-tur`) | Apache-2.0 | `7393381111e1152420fc4092cb44eef4237580d21b92bf30d7d221aad192c6b7` |

Copyright 2006-2022 Google Inc. Licensed under the Apache License, Version 2.0
(<http://www.apache.org/licenses/LICENSE-2.0>). Used by `src/drawing/ocr` (eng: dimension text with whitelist,
tur: notes / title block). Lookup order: `IngestOptions::tessdataDir`, `<exe>/../share/persistcad/tessdata`,
`$TESSDATA_PREFIX`, system tessdata. Owner: WP-2. Phase 2: fine-tuned `pcdim.traineddata` (tesstrain) goes here.
