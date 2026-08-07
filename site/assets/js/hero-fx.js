/* Home-page hero effects init — original code written for this repository,
 * GPLv3 like the repo.
 *
 * Two effects, both optional, both fail-closed:
 *
 *  - H1 title:   Canvas UI ParticleReveal (window.CanvasUIParticle), present
 *                only when scripts/fetch_site_fx.py ran at deploy time.
 *  - Screenshot: Canvas UI VHS (window.CanvasUIVHS), same deploy-time source;
 *                falls back to the committed original crt-fx.js
 *                (window.VJCrtFx) when absent.
 *
 * The Canvas UI vanilla components capture their content through the
 * experimental html-in-canvas API (canvas.requestPaint + 2d-context
 * drawElementImage).  Browsers don't ship it yet, so this file installs a
 * minimal polyfill on the two specific source canvases only: an image blit
 * for the screenshot, and a computed-style text rasterizer for the headline.
 * The fetched components run unmodified.
 *
 * Fail-closed contract: the fx-on classes (which fade the real DOM content)
 * are added only after an engine returns a non-null instance.  Reduced
 * motion is handled inside the engines — they render a static styled frame
 * and gate only the animation — so init proceeds regardless.
 */
(function () {
  "use strict";

  /* ---- html-in-canvas shims (ours) ---- */

  function shimReset(ctx, canvas) {
    if (typeof ctx.reset !== "function") {
      ctx.reset = function () {
        ctx.setTransform(1, 0, 0, 1, 0, 0);
        ctx.clearRect(0, 0, canvas.width, canvas.height);
      };
    }
  }

  function shimRequestPaint(canvas) {
    if (typeof canvas.requestPaint !== "function") {
      canvas.requestPaint = function () {
        Promise.resolve().then(function () {
          if (typeof canvas.onpaint === "function") { canvas.onpaint(); }
        });
      };
    }
  }

  /* Screenshot capture: blit the <img> across the source canvas. */
  function shimImageCapture(srcCanvas, img) {
    var ctx = srcCanvas.getContext("2d");
    if (!ctx) { return false; }
    shimReset(ctx, srcCanvas);
    if (typeof ctx.drawElementImage !== "function") {
      ctx.drawElementImage = function () {
        ctx.drawImage(img, 0, 0, srcCanvas.width, srcCanvas.height);
      };
    }
    shimRequestPaint(srcCanvas);
    return true;
  }

  /* Headline capture: rasterize the element's text with its computed style
   * (font, color, letter-spacing, uppercase), word-wrapped to its width. */
  function shimTextCapture(srcCanvas, el) {
    var ctx = srcCanvas.getContext("2d");
    if (!ctx) { return false; }
    shimReset(ctx, srcCanvas);
    if (typeof ctx.drawElementImage !== "function") {
      ctx.drawElementImage = function () {
        var cs = getComputedStyle(el);
        var cssW = srcCanvas.clientWidth || el.clientWidth || 1;
        var dpr = (srcCanvas.width / cssW) || 1;
        var size = parseFloat(cs.fontSize) || 32;
        var lh = parseFloat(cs.lineHeight);
        var text = el.textContent || "";
        var words, lines, cur, tryLine, i, y;
        if (!isFinite(lh) || lh <= 0) { lh = size * 1.15; }
        if (cs.textTransform === "uppercase") { text = text.toUpperCase(); }
        ctx.save();
        ctx.scale(dpr, dpr);
        ctx.font = cs.fontStyle + " " + cs.fontWeight + " " + size + "px " +
                   cs.fontFamily;
        if ("letterSpacing" in ctx && cs.letterSpacing !== "normal") {
          ctx.letterSpacing = cs.letterSpacing;
        }
        ctx.fillStyle = cs.color;
        ctx.textBaseline = "top";
        words = text.split(/\s+/);
        lines = [];
        cur = "";
        for (i = 0; i < words.length; i++) {
          tryLine = cur ? cur + " " + words[i] : words[i];
          if (cur && ctx.measureText(tryLine).width > cssW) {
            lines.push(cur);
            cur = words[i];
          } else {
            cur = tryLine;
          }
        }
        if (cur) { lines.push(cur); }
        y = Math.max((lh - size) / 2, 0);
        for (i = 0; i < lines.length; i++) {
          ctx.fillText(lines[i], 0, y);
          y += lh;
        }
        ctx.restore();
      };
    }
    shimRequestPaint(srcCanvas);
    return true;
  }

  /* ---- tuned parameters ---- */

  /* VHS on the screenshot.  Motion must read within a second at a glance:
   * visible tape wave, travelling crease, head-switching band at the
   * bottom, rolling brightness beat, animated grain. */
  var VHS_OPTS = {
    speed: 0.9,
    wave: 1.2,
    jitter: 0.45,
    crease: 0.35,
    switching: 0.18,
    switchingHeight: 0.035,
    bloom: 0.4,
    aberration: 2,
    acBeat: 1,
    grain: 0.12,
    scanlines: 0.6,
    vignette: 0.28,
    barrel: 0.3,
    saturation: 1.05,
    exposure: 1.05
  };

  /* ParticleReveal on the H1: idle dust shimmer; the pointer reveals the
   * crisp headline.  background must match the page so glyph pixels are
   * detected as content. */
  function particleOpts() {
    var bg = "#0a0a0d";
    var probe = document.body ?
        getComputedStyle(document.body).backgroundColor : "";
    if (probe && probe !== "transparent") { bg = probe; }
    return {
      radius: 340,
      softness: 0.75,
      size: 1.6,
      scatter: 12,     /* tight dust: letterforms stay legible while idle */
      drift: 1,
      aberration: 30,
      bend: 40,
      fade: 1,
      threshold: 0.06,
      background: bg,
      smoothing: 0.25
    };
  }

  /* ---- wiring ---- */

  /* Fade the real content only after the engine has had two frames to
   * present — the canvas sits on top, so the swap is invisible, and a
   * viewer (or screenshot) can never catch a hidden-content/blank-canvas
   * in-between state. */
  function markOn(el) {
    requestAnimationFrame(function () {
      requestAnimationFrame(function () {
        el.classList.add("fx-on");
      });
    });
  }

  function bootTitle() {
    var frame = document.getElementById("hero-title-frame");
    var content = document.getElementById("hero-title");
    var src = document.getElementById("hero-title-src");
    var out = document.getElementById("hero-title-fx");
    var fx = null;
    if (!frame || !content || !src || !out ||
        !window.CanvasUIParticle ||
        typeof window.CanvasUIParticle.createParticleReveal !== "function") {
      return;
    }
    if (!shimTextCapture(src, content)) { return; }
    try {
      fx = window.CanvasUIParticle.createParticleReveal(
        { source: src, content: content, output: out }, particleOpts());
    } catch (err) {
      fx = null;
    }
    if (fx) { markOn(frame); }
  }

  function bootScreen() {
    var frame = document.getElementById("hero-crt-frame");
    var img = document.getElementById("hero-shot");
    var src = document.getElementById("hero-src");
    var out = document.getElementById("hero-crt");
    if (!frame || !img || !out) { return; }

    function attach() {
      var fx = null;
      if (src && window.CanvasUIVHS &&
          typeof window.CanvasUIVHS.createVHS === "function" &&
          shimImageCapture(src, img)) {
        try {
          fx = window.CanvasUIVHS.createVHS(
            { source: src, content: img, output: out }, VHS_OPTS);
        } catch (err) {
          fx = null;
        }
      }
      if (!fx && window.VJCrtFx &&
          typeof window.VJCrtFx.attach === "function") {
        fx = window.VJCrtFx.attach({ image: img, canvas: out });
      }
      if (fx) { markOn(frame); }
    }

    /* img.decode() guarantees decoded pixel data before the engines take
     * their texture snapshot — a merely-"complete" image can still be
     * undecoded, which uploads as black. */
    if (typeof img.decode === "function") {
      img.decode().then(attach, attach);
    } else if (img.complete && img.naturalWidth > 0) {
      attach();
    } else {
      img.addEventListener("load", attach, { once: true });
    }
  }

  bootScreen();
  bootTitle();
})();
