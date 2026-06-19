(() => {
  "use strict";
  const REDUCED = matchMedia("(prefers-reduced-motion: reduce)").matches;

  /* formatters — nz() strips negative zero ("-0.00" -> "0.00") */
  const nz = (s) => (parseFloat(s) === 0 ? s.replace("-", "") : s);
  const fmt = {
    int: (v) => Math.round(v).toLocaleString("en-US"),
    num1: (v) => nz(v.toFixed(1)), num2: (v) => nz(v.toFixed(2)),
    num3: (v) => nz(v.toFixed(3)), num4: (v) => nz(v.toFixed(4)),
    pct2: (v) => nz((v * 100).toFixed(2)) + "%", pct3: (v) => nz((v * 100).toFixed(3)) + "%",
    compact: (v) => new Intl.NumberFormat("en-US", { notation: "compact", maximumFractionDigits: 1 }).format(v),
    money: (v) => "$" + Math.round(v).toLocaleString("en-US"),
    sci: (v) => v === 0 ? "0" : (Math.abs(v) >= 1e-3 && Math.abs(v) < 1e4 ? String(Number(v.toPrecision(3))) : v.toExponential(1)),
  };

  /* scale + svg-string helpers */
  const lin = (d0, d1, r0, r1) => (v) => d1 === d0 ? (r0 + r1) / 2 : r0 + ((v - d0) / (d1 - d0)) * (r1 - r0);
  const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
  const ext = (a) => [Math.min(...a), Math.max(...a)];
  function niceTicks(min, max, count) {
    if (min === max) return [min];
    const step0 = (max - min) / count, mag = Math.pow(10, Math.floor(Math.log10(step0))), n = step0 / mag;
    const step = (n >= 5 ? 5 : n >= 2 ? 2 : 1) * mag, start = Math.ceil(min / step) * step, out = [];
    for (let t = start; t <= max + step * 1e-6; t += step) out.push(Math.round(t / step) * step);
    return out;
  }
  const line = (x1, y1, x2, y2, c, ex = "") => `<line x1="${x1}" y1="${y1}" x2="${x2}" y2="${y2}" class="${c}"${ex}/>`;
  const text = (x, y, c, s, ex = "") => `<text x="${x}" y="${y}" class="${c}"${ex}>${s}</text>`;
  const path = (d, c, ex = "") => `<path d="${d}" class="${c}"${ex}/>`;
  const toPath = (xs, ys) => xs.map((x, i) => `${i ? "L" : "M"}${x.toFixed(2)} ${ys[i].toFixed(2)}`).join(" ");
  const svgOpen = (w, h) => `<svg viewBox="0 0 ${w} ${h}" preserveAspectRatio="none" width="100%" height="100%" role="img">`;
  function gridAndAxes(m, w, h, xd, yd, xfmt, yfmt, { xticks = 5, yticks = 5, yLabels = true } = {}) {
    const x = lin(xd[0], xd[1], m.l, w - m.r), y = lin(yd[0], yd[1], h - m.b, m.t); let s = "";
    for (const t of niceTicks(yd[0], yd[1], yticks)) { const yy = y(t); s += line(m.l, yy, w - m.r, yy, "c-grid"); if (yLabels) s += text(m.l - 6, yy + 3, "c-tick c-tick-y", yfmt(t)); }
    if (xticks) for (const t of niceTicks(xd[0], xd[1], xticks)) s += text(x(t), h - m.b + 14, "c-tick c-tick-x", xfmt(t));
    s += line(m.l, h - m.b, w - m.r, h - m.b, "c-axis"); return { s, x, y };
  }

  /* === per-project registries ============================================ */
  const R = {};   // chart key -> (node, w, h, data) => svgString
  const T = {};   // table key -> (node, data) => void

  /* extra local helpers in scope of the renderers */
  const rect = (x, y, w, h, c, ex = "") => `<rect x="${x.toFixed(2)}" y="${y.toFixed(2)}" width="${Math.max(0, w).toFixed(2)}" height="${Math.max(0, h).toFixed(2)}" class="${c}"${ex}/>`;
  const log10 = (v) => Math.log10(Math.max(v, 1e-9));
  const esc = (s) => String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;");
  // money/throughput compact, e.g. 7.0M
  const eps = (v) => {
    if (v >= 1e6) return nz((v / 1e6).toFixed(1)) + "M";
    if (v >= 1e3) return nz((v / 1e3).toFixed(0)) + "k";
    return Math.round(v).toString();
  };
  // ticks-of-the-hardware-counter axis label
  const tk = (v) => v >= 1000 ? eps(v) : Math.round(v).toString();

  /* ---- HERO: cumulative latency distribution (CDF), log-x, p50/p99 marks - */
  R.hero = (node, w, h, data) => {
    const H = data.latency.hist, n = H.ub.length;
    const total = H.count.reduce((a, b) => a + b, 0);
    let acc = 0;
    const xs = [], ys = [];
    for (let i = 0; i < n; i++) { acc += H.count[i]; xs.push(log10(H.ub[i])); ys.push(acc / total); }
    const m = { t: 14, r: 14, b: 24, l: 10 };
    const xd = [log10(1), Math.max(xs[n - 1], log10(1024))], yd = [0, 1];
    const x = lin(xd[0], xd[1], m.l, w - m.r), y = lin(yd[0], yd[1], h - m.b, m.t);
    let out = svgOpen(w, h);
    // x decade ticks: 1, 10, 100, 1k, 10k
    for (let e = 0; Math.pow(10, e) <= Math.pow(10, xd[1]); e++) {
      const xx = x(e); if (xx > w - m.r + 1) break;
      out += line(xx, m.t, xx, h - m.b, "c-grid");
      out += text(xx, h - m.b + 14, "c-tick c-tick-x", e === 0 ? "1" : "1e" + e);
    }
    const px = xs.map(x), py = ys.map(y);
    // area
    out += path(`M${px[0].toFixed(2)} ${(h - m.b).toFixed(2)} ` + toPath(px, py).slice(1) + ` L${px[n - 1].toFixed(2)} ${(h - m.b).toFixed(2)} Z`, "c-area-accent");
    // line (animated draw on screen)
    out += path(toPath(px, py), "c-hero-line draw", ` id="hero-path"`);
    // p50 / p99 markers (values are stated in the caption; keep labels terse)
    [["p50", data.latency.p50], ["p99", data.latency.p99]].forEach(([lab, v]) => {
      const xx = x(log10(v));
      out += line(xx, m.t, xx, h - m.b, "c-marker", ` stroke-dasharray="3 3"`);
      out += text(xx + 3, m.t + 11, "c-marker-label", lab);
    });
    out += text(w - m.r, m.t + 10, "c-legend", "cumulative share of events", ` text-anchor="end"`);
    return out + "</svg>";
  };

  /* ---- §01: 40-byte binary message field map --------------------------- */
  R.msg = (node, w, h, data) => {
    const B = 40;
    const fields = [
      ["type", 0, 1, ""], ["side", 1, 1, ""],
      ["order_id", 2, 8, "field-id"], ["price", 10, 4, ""], ["qty", 14, 4, ""],
      ["new_id", 18, 8, "field-id"], ["sym", 26, 4, ""],
      ["magic 'OBL1'", 30, 4, "field-magic"], ["reserved", 34, 6, "field-pad"],
    ];
    const m = { t: 34, r: 14, b: 30, l: 14 };
    const x = lin(0, B, m.l, w - m.r);
    const bandTop = m.t, bandH = h - m.t - m.b;
    let out = svgOpen(w, h) + `<g class="bytemap">`;
    // byte ruler
    for (let b = 0; b <= B; b += 8) {
      out += text(x(b), bandTop - 8, "foff", String(b));
    }
    fields.forEach(([name, off, len, cls]) => {
      const xx = x(off), ww = x(off + len) - x(off);
      out += rect(xx, bandTop, ww, bandH, cls || "");
      if (ww > 26) out += text(xx + ww / 2, bandTop + bandH / 2 + 4, "flabel", name);
      else out += text(xx + ww / 2, bandTop + bandH / 2 + 4, "flabel", name[0]);
      out += text(xx + ww / 2, bandTop + bandH + 14, "foff", `${len}B`);
    });
    out += `</g>`;
    out += text(m.l, h - 6, "c-legend", "40-byte little-endian record · order ids shaded crimson · magic shaded slate", ` text-anchor="start"`);
    return out + "</svg>";
  };

  /* ---- §03: cumulative market-depth ladder ----------------------------- */
  R.depth = (node, w, h, data) => {
    const d = data.depth, bids = d.bids, asks = d.asks;
    const m = { t: 22, r: 16, b: 34, l: 46 };
    const prices = bids.map((b) => b.price).concat(asks.map((a) => a.price));
    const xd = [Math.min(...prices), Math.max(...prices)];
    const maxCum = Math.max(bids[bids.length - 1].cum, asks[asks.length - 1].cum);
    const yd = [0, maxCum * 1.08];
    const { s: base, x, y } = gridAndAxes(m, w, h, xd, yd, (v) => String(Math.round(v)), (v) => fmt.int(v), { xticks: 6, yticks: 5 });
    let out = svgOpen(w, h) + base;
    // staircase builder from the touch outward
    const stair = (rows, dir) => {
      // rows are best-first; step horizontally to each price then vertically to its cum
      let pts = [[x(d.mid), y(0)]];
      let prevCum = 0;
      rows.forEach((r) => { pts.push([x(r.price), y(prevCum)]); pts.push([x(r.price), y(r.cum)]); prevCum = r.cum; });
      const last = rows[rows.length - 1];
      pts.push([x(last.price - dir * 0.5), y(last.cum)]);
      return pts;
    };
    const areaFrom = (pts) => {
      const d0 = pts.map((p, i) => `${i ? "L" : "M"}${p[0].toFixed(2)} ${p[1].toFixed(2)}`).join(" ");
      return `${d0} L${pts[pts.length - 1][0].toFixed(2)} ${y(0).toFixed(2)} L${pts[0][0].toFixed(2)} ${y(0).toFixed(2)} Z`;
    };
    const bidPts = stair(bids, 1), askPts = stair(asks, -1);
    out += path(areaFrom(bidPts), "c-area-accent");
    out += path(areaFrom(askPts), "c-area-data2");
    out += path(bidPts.map((p, i) => `${i ? "L" : "M"}${p[0].toFixed(2)} ${p[1].toFixed(2)}`).join(" "), "c-line c-line-accent");
    out += path(askPts.map((p, i) => `${i ? "L" : "M"}${p[0].toFixed(2)} ${p[1].toFixed(2)}`).join(" "), "c-line c-line-data2");
    // mid marker
    out += line(x(d.mid), m.t, x(d.mid), h - m.b, "c-band");
    out += text(x(d.mid), m.t + 10, "c-marker-label", `mid ${fmt.num1(d.mid)}`, ` text-anchor="middle"`);
    // legend
    out += text(m.l + 4, m.t + 10, "c-legend", "■ bid depth", ` fill="var(--accent-ink)"`);
    out += text(w - m.r - 4, m.t + 10, "c-legend", "ask depth ■", ` text-anchor="end" fill="var(--data2)"`);
    out += text((m.l + w - m.r) / 2, h - 4, "c-tick", "price (ticks)", ` text-anchor="middle"`);
    return out + "</svg>";
  };

  /* ---- §05: per-event latency distribution (log y, p50/p99 marks) ------ */
  R.latency = (node, w, h, data) => {
    const H = data.latency.hist, n = H.ub.length;
    const m = { t: 24, r: 14, b: 32, l: 40 };
    const maxC = Math.max(...H.count);
    const yMax = Math.ceil(log10(maxC));
    const x = lin(0, n, m.l, w - m.r), y = lin(0, yMax, h - m.b, m.t);
    let out = svgOpen(w, h);
    // y decade gridlines + labels (1,10,100,...)
    for (let e = 0; e <= yMax; e++) {
      const yy = y(e); out += line(m.l, yy, w - m.r, yy, "c-grid");
      out += text(m.l - 6, yy + 3, "c-tick c-tick-y", e === 0 ? "1" : "1e" + e);
    }
    const bw = (x(1) - x(0)) * 0.78;
    for (let i = 0; i < n; i++) {
      const bx = x(i) + (x(1) - x(0)) * 0.11;
      const bh = (h - m.b) - y(log10(H.count[i]));
      out += rect(bx, y(log10(H.count[i])), bw, bh, "c-bar");
      if (i % 2 === 0) out += text(x(i + 0.5), h - m.b + 14, "c-tick c-tick-x", "≤" + tk(H.ub[i]));
    }
    out += line(m.l, h - m.b, w - m.r, h - m.b, "c-axis");
    // p50 / p99 markers, positioned at the bucket they fall in
    const idxFor = (v) => { let i = H.ub.findIndex((u) => u >= v); return i < 0 ? n - 1 : i; };
    [["p50", data.latency.p50], ["p99", data.latency.p99]].forEach(([lab, v]) => {
      const xx = x(idxFor(v) + 0.5);
      out += line(xx, m.t, xx, h - m.b, "c-marker", ` stroke-dasharray="3 3"`);
      out += text(xx + 3, m.t + 11, "c-marker-label", `${lab} ${v}`);
    });
    out += text((m.l + w - m.r) / 2, h - 4, "c-tick", "per-event latency bucket (counter ticks)", ` text-anchor="middle"`);
    out += text(2, 11, "c-label", "events", ` text-anchor="start"`);
    return out + "</svg>";
  };

  /* ---- §05: tail percentile curve, two workloads ----------------------- */
  R.tail = (node, w, h, data) => {
    const ps = [50, 90, 99, 99.9];
    const xn = (p) => -Math.log10(1 - p / 100); // 0.30,1,2,3 — even "nines" spacing
    const lat = [data.latency.p50, data.latency.p90, data.latency.p99, data.latency.p999];
    const rep = [data.replay.p50, data.replay.p90, data.replay.p99, data.replay.p999];
    const m = { t: 24, r: 16, b: 34, l: 42 };
    const xd = [xn(50), xn(99.9)];
    const yMax = Math.ceil(log10(Math.max(...lat, ...rep)));
    const x = lin(xd[0], xd[1], m.l, w - m.r), y = lin(0, yMax, h - m.b, m.t);
    let out = svgOpen(w, h);
    for (let e = 0; e <= yMax; e++) { const yy = y(e); out += line(m.l, yy, w - m.r, yy, "c-grid"); out += text(m.l - 6, yy + 3, "c-tick c-tick-y", e === 0 ? "1" : "1e" + e); }
    ps.forEach((p) => out += text(x(xn(p)), h - m.b + 14, "c-tick c-tick-x", "p" + p));
    out += line(m.l, h - m.b, w - m.r, h - m.b, "c-axis");
    const px = ps.map((p) => x(xn(p)));
    const draw = (arr, cls, dot) => {
      const py = arr.map((v) => y(log10(v)));
      out += path(toPath(px, py), "c-line " + cls);
      px.forEach((xx, i) => out += `<circle cx="${xx.toFixed(2)}" cy="${py[i].toFixed(2)}" r="2.6" class="${dot}"/>`);
    };
    draw(rep, "c-line-data2 c-line-dash", "c-dot-data2");
    draw(lat, "c-line-accent", "c-dot-accent");
    out += text(m.l + 6, m.t + 8, "c-legend", "— add/cancel hot path", ` fill="var(--accent-ink)"`);
    out += text(m.l + 6, m.t + 22, "c-legend", "-- binary replay", ` fill="var(--data2)"`);
    out += text((m.l + w - m.r) / 2, h - 4, "c-tick", "percentile", ` text-anchor="middle"`);
    out += text(2, 11, "c-label", "ticks", ` text-anchor="start"`);
    return out + "</svg>";
  };

  /* ---- §05: throughput vs the 2M/s design target ----------------------- */
  R.throughput = (node, w, h, data) => {
    const target = 2_000_000;
    const rows = [
      ["Add / cancel hot path", data.latency.throughput_median],
      ["Binary replay · decode→route→match", data.replay.throughput],
      ["SPSC pipeline · two threads", data.pipeline.throughput],
    ];
    const m = { t: 16, r: 58, b: 30, l: 4 };
    const xMax = Math.max(target, ...rows.map((r) => r[1])) * 1.12;
    const x = lin(0, xMax, m.l, w - m.r);
    const rowH = (h - m.t - m.b) / rows.length;
    let out = svgOpen(w, h);
    // x grid
    for (const t of niceTicks(0, xMax, 4)) { out += line(x(t), m.t, x(t), h - m.b, "c-grid"); out += text(x(t), h - m.b + 14, "c-tick c-tick-x", eps(t)); }
    rows.forEach(([label, v], i) => {
      const yy = m.t + i * rowH + rowH * 0.18, bh = rowH * 0.42;
      out += rect(m.l, yy, x(v) - m.l, bh, i === 1 ? "c-bar-accent" : "c-bar");
      out += text(m.l + 2, yy - 4, "c-label", label, ` text-anchor="start"`);
      out += text(x(v) + 5, yy + bh - 1, "c-val", eps(v) + "/s", ` text-anchor="start"`);
    });
    // target marker
    out += line(x(target), m.t - 2, x(target), h - m.b, "c-marker");
    out += text(x(target), m.t - 5, "c-marker-label", "2M target", ` text-anchor="middle"`);
    return out + "</svg>";
  };

  /* ---- §05: pipeline throughput vs batch size (log x) ------------------ */
  R.scaling = (node, w, h, data) => {
    const sc = data.pipeline.scaling;
    const m = { t: 24, r: 16, b: 32, l: 44 };
    const xd = [log10(sc.n[0]), log10(sc.n[sc.n.length - 1])];
    const yd = [0, Math.max(...sc.throughput) * 1.1];
    const x = lin(xd[0], xd[1], m.l, w - m.r), y = lin(yd[0], yd[1], h - m.b, m.t);
    let out = svgOpen(w, h);
    for (const t of niceTicks(yd[0], yd[1], 5)) { out += line(m.l, y(t), w - m.r, y(t), "c-grid"); out += text(m.l - 6, y(t) + 3, "c-tick c-tick-y", eps(t)); }
    sc.n.forEach((nv) => out += text(x(log10(nv)), h - m.b + 14, "c-tick c-tick-x", eps(nv)));
    out += line(m.l, h - m.b, w - m.r, h - m.b, "c-axis");
    const px = sc.n.map((nv) => x(log10(nv))), py = sc.throughput.map(y);
    out += path(toPath(px, py), "c-line c-line-accent");
    px.forEach((xx, i) => out += `<circle cx="${xx.toFixed(2)}" cy="${py[i].toFixed(2)}" r="2.6" class="c-dot-accent"/>`);
    out += text((m.l + w - m.r) / 2, h - 4, "c-tick", "events per run (log)", ` text-anchor="middle"`);
    out += text(2, 11, "c-label", "ev/s", ` text-anchor="start"`);
    return out + "</svg>";
  };

  /* ---- tables ---------------------------------------------------------- */
  T.tests = (node, data) => {
    const rows = data.tests.map((t) =>
      `<tr class="${t.pass ? "row-pass" : "row-fail"}"><td class="t">${esc(t.name)}</td><td class="t">${esc(t.desc)}</td><td>${t.pass ? "PASS" : "FAIL"}</td></tr>`).join("");
    node.innerHTML =
      `<table><thead><tr><th>check</th><th>covers</th><th>result</th></tr></thead><tbody>${rows}</tbody></table>`;
  };

  T.trades = (node, data) => {
    const rows = data.depth.trades.map((t) =>
      `<tr><td class="t">#${t.seq}</td><td>${t.side === "BUY" ? "BUY" : "SELL"}</td><td>${fmt.int(t.price)}</td><td>${t.qty}</td></tr>`).join("");
    node.innerHTML =
      `<table><thead><tr><th>seq</th><th>aggressor</th><th>price</th><th>qty</th></tr></thead><tbody>${rows}</tbody></table>`;
  };

  /* === plumbing — copy verbatim ========================================= */
  let DATA = null;
  const resolve = (o, p) => p.split(".").reduce((x, k) => x == null ? undefined : x[k], o);
  function renderOne(node) {
    const k = node.dataset.chart;
    if (T[k]) return T[k](node, DATA);
    if (!R[k]) return;
    const w = Math.max(320, Math.round(node.clientWidth)), h = Math.max(120, Math.round(node.clientHeight));
    node.innerHTML = R[k](node, w, h, DATA);
  }
  const renderAll = () => document.querySelectorAll("[data-chart]").forEach(renderOne);
  function bindStats() {
    document.querySelectorAll("[data-stat]").forEach((n) => {
      const v = resolve(DATA, n.dataset.stat); if (v == null) return;
      const f = n.dataset.fmt; n.textContent = f && fmt[f] ? fmt[f](v) : v;
    });
  }
  function heroDraw() {
    if (REDUCED) return;
    const p = document.getElementById("hero-path"); if (!p) return;
    const len = p.getTotalLength();
    p.style.setProperty("--len", len);
    requestAnimationFrame(() => requestAnimationFrame(() => p.classList.add("go")));
  }
  function setupMotion() {
    if (REDUCED) return;
    const ts = document.querySelectorAll("figure,.param-grid,.throughput,.pipeline,.abstract-stats,.toc,.hero-spark");
    ts.forEach((t) => t.classList.add("reveal"));
    const io = new IntersectionObserver((es) => es.forEach((e) => { if (e.isIntersecting) { e.target.classList.add("in"); io.unobserve(e.target); if (e.target.classList.contains("hero-spark")) heroDraw(); } }), { threshold: 0.12, rootMargin: "0px 0px -8% 0px" });
    ts.forEach((t) => io.observe(t));
  }
  let mathTries = 0;
  function typesetMath() {
    if (typeof renderMathInElement !== "function") {
      if (mathTries++ < 60) setTimeout(typesetMath, 80);  // bounded: a forever-retry
      return;                                              // keeps virtual time from going
    }                                                      // idle and hangs --print-to-pdf
    // ignoredTags MUST include "svg": auto-render otherwise descends into the
    // injected chart SVGs and processing math inside SVG text nodes hangs the
    // headless layout pass (--print-to-pdf never captures).
    renderMathInElement(document.body, {
      throwOnError: false,
      ignoredTags: ["script", "noscript", "style", "textarea", "pre", "code", "option", "svg"],
    });
  }
  function progress() {
    const bar = document.getElementById("progress-bar"); if (!bar) return;
    const on = () => { const sc = document.documentElement.scrollTop, mx = document.documentElement.scrollHeight - innerHeight; bar.style.transform = `scaleX(${mx > 0 ? clamp(sc / mx, 0, 1) : 0})`; };
    addEventListener("scroll", on, { passive: true }); on();
  }
  let rt; addEventListener("resize", () => { clearTimeout(rt); rt = setTimeout(renderAll, 150); }, { passive: true });
  addEventListener("beforeprint", renderAll);
  async function init() {
    try { DATA = await (await fetch("data.json", { cache: "no-cache" })).json(); }
    catch (e) { document.querySelectorAll("[data-chart]").forEach((n) => (n.textContent = "data unavailable — run export_data.py")); return; }
    bindStats(); renderAll(); typesetMath(); setupMotion(); progress();
  }
  document.readyState === "loading" ? addEventListener("DOMContentLoaded", init) : init();
})();
