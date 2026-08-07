/* Home-page hero: attach the vendored Canvas UI VHS effect (canvas-ui-vhs.js)
 * to the in-core Cybermorph screenshot.  Progressive enhancement only: the
 * plain <img> is the content; the effect draws on a canvas layered over it
 * and is skipped entirely without JS, without WebGL2, or when the user
 * prefers reduced motion. */
(function () {
  "use strict";

  if (window.matchMedia &&
      window.matchMedia("(prefers-reduced-motion: reduce)").matches) {
    return;
  }

  var frame = document.getElementById("hero-crt-frame");
  var img = document.getElementById("hero-shot");
  var out = document.getElementById("hero-crt");
  if (!frame || !img || !out ||
      !window.CanvasUIVHS || !window.CanvasUIVHS.createVHSFromImage) {
    return;
  }

  function boot() {
    var fx = window.CanvasUIVHS.createVHSFromImage(
      { image: img, output: out },
      {
        /* Tuned toward CRT, away from broken-tape: light wave/jitter,
         * visible scanlines, slight tube curvature. */
        speed: 0.5,
        wave: 0.6,
        jitter: 0.15,
        crease: 0.08,
        switching: 0.04,
        switchingHeight: 0.02,
        bloom: 0.35,
        aberration: 1.5,
        acBeat: 0.6,
        grain: 0.07,
        scanlines: 0.5,
        vignette: 0.3,
        barrel: 0.35,
        saturation: 1.05,
        exposure: 1.05
      });
    if (fx) { frame.classList.add("fx-on"); }
  }

  if (img.complete && img.naturalWidth > 0) {
    boot();
  } else {
    img.addEventListener("load", boot, { once: true });
  }
})();
