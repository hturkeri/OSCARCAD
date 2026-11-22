READY v2
apppkg: JSON (persistcad-app/1), .pcapp save/load, standalone build (payload+u64LE+PCAPP_PAYLOAD_V1), loadEmbedded (rebuild from embedded runner strips old payload), lintApp (strings prefixed "HATA: " / "UYARI: ", see pc/AppLint.h). Tests: tests/test_apppkg.cpp.
v2: lintApp warns (UYARI) on MODEL.BODY_INTERFERENCE and non-Info MODEL.MULTI_BODY (unintended split) of the default model. Tests: tests/test_apppkg_v2.cpp.

## Review Nov 2026 (independent reviewer)
- `pc_kesit model.pcad out.json [--plane xz|yz|xy] [--offset d] [--edges] [--set N=V]`: per-body planar section (+ optional 3D
  edges) as JSON polylines; used by docs/enjektor/dogrulama/bindirme.py.
