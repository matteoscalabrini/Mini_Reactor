import * as store from "./store.js";
import { runParams } from "./runparams.js";
import { deriveRunbar } from "./runstate.js";
import { runStart, runPause, runResume, runStop } from "./api.js";
import { el, toast } from "./ui.js";

const _wraps = [];
function closeAllMenus() { for (const w of _wraps) w._close && w._close(); }

// A button that toggles an attached dropdown of { label, kind?, onClick } items.
function menuButton(label, cls, items) {
  const menu = el("div", { class: "rb-menu", hidden: "" },
    ...items.map((it) => el("button", {
      class: `rb-item ${it.kind || ""}`,
      onclick: (e) => { e.stopPropagation(); close(); it.onClick(); },
    }, it.label)));
  const btn = el("button", { class: cls, onclick: (e) => {
    e.stopPropagation();
    const wasClosed = menu.hasAttribute("hidden");
    closeAllMenus();
    if (wasClosed) menu.removeAttribute("hidden");
  } }, label);
  function close() { menu.setAttribute("hidden", ""); }
  const wrap = el("div", { class: "rb-wrap" }, btn, menu);
  wrap._close = close;
  _wraps.push(wrap);
  return { wrap, btn };
}

export function mount(container) {
  const start = el("button", { class: "go rb-btn" }, "▶ Start");
  const resume = el("button", { class: "go rb-btn" }, "▶ Resume");
  const held = el("span", { class: "rb-held" }, "");
  const pause = menuButton("⏸ Pause ▾", "rb-btn warn", [
    { label: "Pause motor", onClick: () => runPause("motor") },
    { label: "Pause heater", onClick: () => runPause("heater") },
    { label: "Pause all", onClick: () => runPause("all") },
  ]);
  const stop = menuButton("■ Stop ▾", "stop rb-btn", [
    { label: "Cancel", onClick: () => {} },
    { label: "Stop & save data", onClick: () => runStop("save") },
    { label: "Stop & discard data", kind: "danger",
      onClick: () => { if (confirm("Discard this run's data?")) runStop("discard"); } },
  ]);

  start.addEventListener("click", async () => {
    const p = runParams.get();
    if (p.rpm < 0 || p.rpm > 30) return toast("rpm must be 0–30");
    if (p.targetC < 0 || p.targetC > 55) return toast("target must be 0–55 °C");
    const res = await runStart(p.targetC, p.rpm, p.durationMin);
    if (!res.ok) toast(res.body && res.body.error ? res.body.error.message : "start failed");
  });
  resume.addEventListener("click", () => runResume());

  container.append(start, pause.wrap, resume, held, stop.wrap);
  document.addEventListener("click", closeAllMenus);

  function update(status) {
    const { mode, held: heldWhich, startBlocked } = deriveRunbar(status);
    start.hidden = mode !== "idle";
    pause.wrap.hidden = mode !== "running";
    resume.hidden = mode !== "paused";
    stop.wrap.querySelector("button").disabled = mode === "idle";
    start.disabled = startBlocked;
    start.title = startBlocked ? "Heater probe faulted — Start is blocked" : "";
    held.textContent = heldWhich ? `${heldWhich} held` : "";
    if (mode === "idle") closeAllMenus();
  }

  const unsub = store.subscribe(update);
  update(store.getStatus());
  return () => { unsub(); document.removeEventListener("click", closeAllMenus); };
}
