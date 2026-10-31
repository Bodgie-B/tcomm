// Walks a rendered board and posts its display list: every SVG shape flattened to screen-space
// points, every painted HTML box, every text run with its font and box. Document order = paint order.
(function () {
  const out = { shapes: [], boxes: [], texts: [] };
  const rgb = (s) => s;
  let seq = 0;
  const ids = new WeakMap();
  const idOf = (el) => { if (!ids.has(el)) ids.set(el, 'e' + (seq++)); return ids.get(el); };

  function opacityGroup(el) {  // nearest ancestor (or self) with opacity < 1, and the product of all
    let n = el, total = 1, groups = [];
    while (n && n.nodeType === 1 && n !== document.documentElement) {
      const o = parseFloat(getComputedStyle(n).opacity);
      if (o < 1) { groups.push({ id: idOf(n), o }); total *= o; }
      n = n.parentElement;
    }
    return { total, groups };
  }
  function htmlClip(el) {  // nearest ancestor HTML element with overflow hidden: rect and radii
    let n = el.parentElement;
    while (n && n !== document.body) {
      if (!(n instanceof SVGElement)) {
        const cs = getComputedStyle(n);
        if (cs.overflow === 'hidden' || cs.overflowX === 'hidden') {
          const r = n.getBoundingClientRect();
          return { id: idOf(n), x: r.left, y: r.top, w: r.width, h: r.height,
                   radii: [cs.borderTopLeftRadius, cs.borderTopRightRadius, cs.borderBottomRightRadius, cs.borderBottomLeftRadius].map(parseFloat) };
        }
      }
      n = n.parentElement;
    }
    return null;
  }
  function tp(m, x, y) { return [m.a * x + m.c * y + m.e, m.b * x + m.d * y + m.f]; }
  function flattenPath(el, m) {
    const len = el.getTotalLength();
    const step = 0.4;
    const n = Math.max(8, Math.ceil(len / step));
    const subs = [];
    let cur = [], prev = null;
    for (let i = 0; i <= n; i++) {
      const p = el.getPointAtLength(len * i / n);
      if (prev && Math.hypot(p.x - prev.x, p.y - prev.y) > 4 * (len / n) + 0.5) { if (cur.length > 1) subs.push(cur); cur = []; }
      cur.push(tp(m, p.x, p.y));
      prev = p;
    }
    if (cur.length > 1) subs.push(cur);
    return subs;
  }
  function clipPathOf(el) {
    let n = el;
    while (n && n instanceof SVGElement) {
      const cp = n.getAttribute && n.getAttribute('clip-path');
      if (cp) {
        const id = cp.match(/#([^)'"]+)/);
        const c = id && document.getElementById(id[1]);
        if (c) {
          const kid = c.querySelector('path,rect,circle,polygon,ellipse');
          const m = kid.getScreenCTM();
          let subs;
          if (kid.tagName === 'path') subs = flattenPath(kid, m);
          return { id: id[1], subs };
        }
      }
      n = n.parentElement;
    }
    return null;
  }

  const all = document.body.querySelectorAll('*');
  for (const el of all) {
    if (el.closest('defs') || el.closest('clipPath')) continue;
    const cs = getComputedStyle(el);
    if (cs.display === 'none' || cs.visibility === 'hidden') continue;
    if (el instanceof SVGGeometryElement) {
      const m = el.getScreenCTM();
      const scale = Math.sqrt(Math.abs(m.a * m.d - m.b * m.c));
      const s = { tag: el.tagName, fill: cs.fill, fillOpacity: +cs.fillOpacity, fillRule: cs.fillRule,
                  stroke: cs.stroke, strokeWidth: parseFloat(cs.strokeWidth) * scale, cap: cs.strokeLinecap, join: cs.strokeLinejoin,
                  opacity: opacityGroup(el), clip: clipPathOf(el), hclip: htmlClip(el), svg: idOf(el.ownerSVGElement) };
      const t = el.tagName;
      if (t === 'circle') {
        const c = tp(m, el.cx.baseVal.value, el.cy.baseVal.value);
        s.circle = [c[0], c[1], el.r.baseVal.value * scale];
      } else if (t === 'rect' && Math.abs(m.b) < 1e-6 && Math.abs(m.c) < 1e-6) {
        const p0 = tp(m, el.x.baseVal.value, el.y.baseVal.value);
        s.rect = [p0[0], p0[1], el.width.baseVal.value * m.a, el.height.baseVal.value * m.d, (el.rx.baseVal.value || el.ry.baseVal.value || 0) * scale];
      } else if (t === 'polygon' || t === 'polyline') {
        const pts = [];
        for (let i = 0; i < el.points.numberOfItems; i++) { const p = el.points.getItem(i); pts.push(tp(m, p.x, p.y)); }
        s.subs = [pts]; s.closed = t === 'polygon';
      } else if (t === 'line') {
        s.subs = [[tp(m, el.x1.baseVal.value, el.y1.baseVal.value), tp(m, el.x2.baseVal.value, el.y2.baseVal.value)]]; s.closed = false;
      } else {
        s.subs = flattenPath(el, m);
        s.closed = t !== 'path' || /[zZ]\s*$/.test(el.getAttribute('d') || '');
        if (t === 'path') s.d = el.getAttribute('d');
      }
      out.shapes.push(s);
      continue;
    }
    if (el instanceof SVGElement) continue;
    // HTML: painted boxes
    const r = el.getBoundingClientRect();
    const bg = cs.backgroundColor, bw = parseFloat(cs.borderTopWidth) || 0;
    if ((bg && bg !== 'rgba(0, 0, 0, 0)') || bw > 0) {
      out.boxes.push({ tag: el.tagName, x: r.left, y: r.top, w: r.width, h: r.height, bg, border: bw, borderColor: cs.borderTopColor,
                       radii: [cs.borderTopLeftRadius, cs.borderTopRightRadius, cs.borderBottomRightRadius, cs.borderBottomLeftRadius].map(parseFloat),
                       opacity: opacityGroup(el), label: el.getAttribute('aria-label') || '' });
    }
    // text runs (direct text children)
    for (const node of el.childNodes) {
      if (node.nodeType !== 3 || !node.textContent.trim()) continue;
      const range = document.createRange(); range.selectNodeContents(node);
      const rr = range.getBoundingClientRect();
      out.texts.push({ text: node.textContent.trim(), x: rr.left, y: rr.top, w: rr.width, h: rr.height,
                       family: cs.fontFamily, weight: cs.fontWeight, size: parseFloat(cs.fontSize), lineHeight: cs.lineHeight,
                       letterSpacing: cs.letterSpacing, color: cs.color, align: cs.textAlign });
    }
  }
  const name = location.pathname.replace(/^\//, '').replace(/\.html$/, '');
  return fetch('/save/' + name, { method: 'POST', body: JSON.stringify(out) }).then(r => r.text()).then(t => name + ': ' + t + ', ' + out.shapes.length + ' shapes, ' + out.boxes.length + ' boxes, ' + out.texts.length + ' texts');
})();
