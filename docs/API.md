# Mini-Reactor API — moved

The canonical, as-built HTTP + WebSocket API reference now lives at the repository
root: [`../API.md`](../API.md).

It is the single source of truth and is kept in sync with the real endpoints in
[`../src/net/WebInterface.cpp`](../src/net/WebInterface.cpp). This file used to hold a
parallel "Phases 1–3" snapshot that drifted out of date (it still listed calibration
and the SD/system endpoints as *Planned*, described always-on logging, and omitted the
feature toggles); it was collapsed to this pointer to avoid dual maintenance.

- **Full reference:** [`../API.md`](../API.md)
- **Machine-readable contract:** [`openapi.yaml`](openapi.yaml)
