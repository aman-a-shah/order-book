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

  /* ---- HERO: minimal allocation-latency tail (memalloc vs system, log–log
     CCDF, clean lines + end dots, no axes — mirrors the reference sparkline) ---- */
  R.hero = (node, w, h, data) => {
    const t = data.latency_tail, m = { t: 16, r: 16, b: 14, l: 14 };
    const ccdf = (lat) => lat.map((v, i) => [v, 1 - t.pct[i] / 100]);
    const mem = ccdf(t.memalloc).concat([[t.memalloc_max, 1e-6]]);
    const sys = ccdf(t.system).concat([[t.system_max, 1e-6]]);
    const allX = mem.concat(sys).map((p) => log10(p[0]));
    const xd = [Math.min(...allX), Math.max(...allX)], yd = [log10(1e-6), log10(0.5)];
    const x = lin(xd[0], xd[1], m.l, w - m.r), y = lin(yd[0], yd[1], h - m.b, m.t);
    const proj = (pts) => pts.map((p) => [x(log10(p[0])), y(log10(p[1]))]);
    const toP = (pp) => pp.map((p, i) => `${i ? "L" : "M"}${p[0].toFixed(2)} ${p[1].toFixed(2)}`).join(" ");
    const mp = proj(mem), sp = proj(sys);
    let out = svgOpen(w, h);
    out += line(m.l, y(yd[1]), w - m.r, y(yd[1]), "c-grid");
    out += path(toP(sp), "c-line c-line-data2 c-line-dash");
    out += path(toP(mp), "c-line c-line-accent draw", ` style="--len:1"`);
    out += `<circle cx="${sp[sp.length - 1][0].toFixed(2)}" cy="${sp[sp.length - 1][1].toFixed(2)}" r="2.6" class="c-dot-data2"/>`;
    out += `<circle cx="${mp[mp.length - 1][0].toFixed(2)}" cy="${mp[mp.length - 1][1].toFixed(2)}" r="3.2" class="c-dot-accent"/>`;
    out += text(m.l + 2, m.t + 2, "c-legend", "— memalloc", ` fill="var(--accent-ink)"`);
    out += text(m.l + 2, m.t + 16, "c-legend", "-- system malloc", ` fill="var(--data2)"`);
    return out + "</svg>";
  };

  /* ---- §02: throughput — grouped horizontal bars (memalloc vs system) ---- */
  R.throughput = (node, w, h, data) => {
    const T = data.throughput, n = T.workloads.length;
    const m = { t: 28, r: 52, b: 28, l: 4 };
    const xMax = Math.max(...T.memalloc, ...T.system) * 1.12;
    const x = lin(0, xMax, m.l, w - m.r);
    const groupH = (h - m.t - m.b) / n;
    let out = svgOpen(w, h);
    for (const v of niceTicks(0, xMax, 5)) { out += line(x(v), m.t, x(v), h - m.b, "c-grid"); out += text(x(v), h - m.b + 14, "c-tick c-tick-x", fmt.int(v)); }
    // legend
    out += `<rect x="${m.l}" y="6" width="9" height="9" class="c-bar-accent"/>`;
    out += text(m.l + 14, 14, "c-legend", "memalloc", ` text-anchor="start"`);
    out += `<rect x="${m.l + 92}" y="6" width="9" height="9" class="c-bar"/>`;
    out += text(m.l + 106, 14, "c-legend", "system malloc", ` text-anchor="start"`);
    for (let i = 0; i < n; i++) {
      const gy = m.t + i * groupH, barH = groupH * 0.27;
      const y1 = gy + groupH * 0.18, y2 = y1 + barH + 3;
      out += text(m.l + 2, gy + groupH * 0.14, "c-label", T.workloads[i], ` text-anchor="start"`);
      out += rect(m.l, y1, x(T.memalloc[i]) - m.l, barH, "c-bar-accent");
      out += text(x(T.memalloc[i]) + 5, y1 + barH - 1, "c-val-accent", fmt.num1(T.memalloc[i]) + " · " + T.speedup[i], ` text-anchor="start"`);
      out += rect(m.l, y2, x(T.system[i]) - m.l, barH, "c-bar");
      out += text(x(T.system[i]) + 5, y2 + barH - 1, "c-val", fmt.num1(T.system[i]), ` text-anchor="start"`);
    }
    out += line(m.l, h - m.b, w - m.r, h - m.b, "c-axis");
    out += text(2, 24, "c-label", "M ops/s", ` text-anchor="start"`);
    return out + "</svg>";
  };

  /* ---- §03: mean latency vs request size, log x (memalloc vs system) ---- */
  R.latsize = (node, w, h, data) => {
    const d = data.lat_size, m = { t: 24, r: 16, b: 34, l: 42 };
    const xs = d.bytes.map(log10), xd = [xs[0], xs[xs.length - 1]];
    const yd = [0, Math.max(...d.system, ...d.memalloc) * 1.12];
    const x = lin(xd[0], xd[1], m.l, w - m.r), y = lin(yd[0], yd[1], h - m.b, m.t);
    let out = svgOpen(w, h);
    for (const v of niceTicks(yd[0], yd[1], 5)) { out += line(m.l, y(v), w - m.r, y(v), "c-grid"); out += text(m.l - 6, y(v) + 3, "c-tick c-tick-y", fmt.int(v)); }
    d.bytes.forEach((b, i) => { if (i % 2 === 0 || i === d.bytes.length - 1) out += text(x(xs[i]), h - m.b + 14, "c-tick c-tick-x", d.labels[i]); });
    out += line(m.l, h - m.b, w - m.r, h - m.b, "c-axis");
    const px = xs.map(x);
    const draw = (arr, cls, dot) => { const py = arr.map(y); out += path(toPath(px, py), "c-line " + cls); px.forEach((xx, i) => out += `<circle cx="${xx.toFixed(2)}" cy="${py[i].toFixed(2)}" r="2.3" class="${dot}"/>`); };
    draw(d.system, "c-line-data2 c-line-dash", "c-dot-data2");
    draw(d.memalloc, "c-line-accent", "c-dot-accent");
    out += text(m.l + 6, m.t + 8, "c-legend", "— memalloc", ` fill="var(--accent-ink)"`);
    out += text(m.l + 6, m.t + 22, "c-legend", "-- system malloc", ` fill="var(--data2)"`);
    out += text((m.l + w - m.r) / 2, h - 4, "c-tick", "request size (log)", ` text-anchor="middle"`);
    out += text(2, 11, "c-label", "ns", ` text-anchor="start"`);
    return out + "</svg>";
  };

  /* ---- §04: aggregate throughput vs thread count (+ ideal-linear) ---- */
  R.threads = (node, w, h, data) => {
    const d = data.threads, m = { t: 24, r: 16, b: 34, l: 46 };
    const xs = d.n.map((v) => Math.log2(v)), xd = [xs[0], xs[xs.length - 1]];
    const yd = [0, Math.max(...d.ideal) * 1.05];
    const x = lin(xd[0], xd[1], m.l, w - m.r), y = lin(yd[0], yd[1], h - m.b, m.t);
    let out = svgOpen(w, h);
    for (const v of niceTicks(yd[0], yd[1], 5)) { out += line(m.l, y(v), w - m.r, y(v), "c-grid"); out += text(m.l - 6, y(v) + 3, "c-tick c-tick-y", fmt.int(v)); }
    d.n.forEach((v, i) => out += text(x(xs[i]), h - m.b + 14, "c-tick c-tick-x", v + "T"));
    out += line(m.l, h - m.b, w - m.r, h - m.b, "c-axis");
    const px = xs.map(x);
    out += path(toPath(px, d.ideal.map(y)), "c-line", ` stroke="var(--faint)" stroke-dasharray="2 3" stroke-width="1.2"`);
    const draw = (arr, cls, dot) => { const py = arr.map(y); out += path(toPath(px, py), "c-line " + cls); px.forEach((xx, i) => out += `<circle cx="${xx.toFixed(2)}" cy="${py[i].toFixed(2)}" r="2.5" class="${dot}"/>`); };
    draw(d.system, "c-line-data2 c-line-dash", "c-dot-data2");
    draw(d.memalloc, "c-line-accent", "c-dot-accent");
    out += text(m.l + 6, m.t + 8, "c-legend", "— memalloc", ` fill="var(--accent-ink)"`);
    out += text(m.l + 6, m.t + 22, "c-legend", "-- system", ` fill="var(--data2)"`);
    out += text(m.l + 6, m.t + 36, "c-legend", "·· ideal-linear", ` fill="var(--faint)"`);
    out += text((m.l + w - m.r) / 2, h - 4, "c-tick", "threads", ` text-anchor="middle"`);
    out += text(2, 11, "c-label", "M ops/s", ` text-anchor="start"`);
    return out + "</svg>";
  };

  /* ---- §05: fragmentation — measured bar against the 3% target ---- */
  R.frag = (node, w, h, data) => {
    const f = data.frag, m = { t: 22, r: 16, b: 28, l: 4 };
    const xMax = f.target * 1.28;
    const x = lin(0, xMax, m.l, w - m.r);
    let out = svgOpen(w, h);
    for (const v of niceTicks(0, xMax, 5)) { out += line(x(v), m.t, x(v), h - m.b, "c-grid"); out += text(x(v), h - m.b + 14, "c-tick c-tick-x", fmt.num1(v) + "%"); }
    const by = m.t + (h - m.t - m.b) * 0.3, bh = (h - m.t - m.b) * 0.36;
    out += rect(m.l, by, x(f.measured) - m.l, bh, "c-bar-accent");
    out += text(x(f.measured) + 6, by + bh - 1, "c-val-accent", fmt.num2(f.measured) + "% measured", ` text-anchor="start"`);
    out += line(x(f.target), m.t - 2, x(f.target), h - m.b, "c-marker");
    out += text(x(f.target), m.t - 5, "c-marker-label", "3% target", ` text-anchor="middle"`);
    out += line(m.l, h - m.b, w - m.r, h - m.b, "c-axis");
    out += text(2, 11, "c-label", "fragmentation", ` text-anchor="start"`);
    return out + "</svg>";
  };

  /* ---- §05: resident footprint plateau across allocate/free cycles ---- */
  R.footprint = (node, w, h, data) => {
    const fp = data.footprint, m = { t: 22, r: 16, b: 34, l: 44 };
    const C = fp.cycles, M = fp.mb;
    const pts = [[0, 0], [C * 0.02, M * 0.62], [C * 0.05, M * 0.93], [C * 0.09, M], [C, M]];
    const xd = [0, C], yd = [0, M * 1.2];
    const x = lin(xd[0], xd[1], m.l, w - m.r), y = lin(yd[0], yd[1], h - m.b, m.t);
    let out = svgOpen(w, h);
    for (const v of niceTicks(yd[0], yd[1], 5)) { out += line(m.l, y(v), w - m.r, y(v), "c-grid"); out += text(m.l - 6, y(v) + 3, "c-tick c-tick-y", fmt.num1(v)); }
    for (const v of niceTicks(0, C, 5)) out += text(x(v), h - m.b + 14, "c-tick c-tick-x", fmt.compact(v));
    out += line(m.l, h - m.b, w - m.r, h - m.b, "c-axis");
    const px = pts.map((p) => x(p[0])), py = pts.map((p) => y(p[1]));
    out += path(`M${px[0].toFixed(2)} ${(h - m.b).toFixed(2)} ` + toPath(px, py).slice(1) + ` L${px[px.length - 1].toFixed(2)} ${(h - m.b).toFixed(2)} Z`, "c-area-accent");
    out += path(toPath(px, py), "c-line c-line-accent");
    out += line(m.l, y(M), w - m.r, y(M), "c-band");
    out += text(w - m.r - 4, y(M) - 5, "c-marker-label", fmt.num1(M) + " MB plateau", ` text-anchor="end"`);
    out += text((m.l + w - m.r) / 2, h - 4, "c-tick", "allocate / free cycles", ` text-anchor="middle"`);
    out += text(2, 11, "c-label", "MB", ` text-anchor="start"`);
    return out + "</svg>";
  };

  /* ---- tables ---------------------------------------------------------- */
  T.latency = (node, data) => {
    const rows = data.latency_pctile.rows.map((r) =>
      `<tr><td><span class="name">${esc(r[0])}</span></td><td class="num">${esc(r[1])}</td><td class="num">${esc(r[2])}</td><td class="num">${esc(r[3])}</td></tr>`).join("");
    node.innerHTML =
      `<table><thead><tr><th>percentile</th><th>memalloc · ns</th><th>system · ns</th><th>tighter</th></tr></thead><tbody>${rows}</tbody></table>`;
  };

  T.targets = (node, data) => {
    const rows = data.targets.map((t) =>
      `<tr class="${t.met ? "row-pass" : "row-fail"}"><td class="desc">${esc(t.name)}</td><td class="desc">${esc(t.goal)}</td><td class="desc">${esc(t.measured)}</td></tr>`).join("");
    node.innerHTML =
      `<table><thead><tr><th>PRD target</th><th>goal</th><th>measured</th></tr></thead><tbody>${rows}</tbody></table>`;
  };

  // consolidated report — two key/value pairs per row (mirrors the reference Tbl 3)
  T.report = (node, data) => {
    const rows = data.report.map((r) =>
      `<tr><td class="desc">${esc(r[0])}</td><td class="num">${esc(r[1])}</td><td class="desc">${esc(r[2])}</td><td class="num">${esc(r[3])}</td></tr>`).join("");
    node.innerHTML = `<table><tbody>${rows}</tbody></table>`;
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
    const p = document.querySelector("#fig-hero .c-line.draw"); if (!p) return;
    if (REDUCED) { p.style.setProperty("--len", "0"); p.classList.add("in"); return; }
    const len = p.getTotalLength();
    p.style.setProperty("--len", len);
    requestAnimationFrame(() => requestAnimationFrame(() => p.classList.add("in")));
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
