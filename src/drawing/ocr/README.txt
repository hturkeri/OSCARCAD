WP-2: Tesseract-based OCR (compiled only with OpenCV + Tesseract). No public API in the frozen headers;
raster/ calls it through a WP-2 private header placed here (e.g. ocr/Ocr.h). Fallbacks go to ocr/nodeps/.
