/* Home-page hero: attach the CRT treatment (crt-fx.js, original code, GPLv3)
 * to the in-core Cybermorph screenshot.  Progressive enhancement only: the
 * plain <img> is the content; the effect draws on a canvas layered over it
 * and is skipped entirely without JS, without WebGL, or when the user
 * prefers reduced motion.  VJCrtFx.attach fails closed (returns null on any
 * GL error), and the fx-on class is only added on success, so the image is
 * never faded behind a dead canvas. */
(function () {
  "use strict";

  if (window.matchMedia &&
      window.matchMedia("(prefers-reduced-motion: reduce)").matches) {
    return;
  }

  var frame = document.getElementById("hero-crt-frame");
  var img = document.getElementById("hero-shot");
  var out = document.getElementById("hero-crt");
  if (!frame || !img || !out || !window.VJCrtFx) { return; }

  function boot() {
    var fx = window.VJCrtFx.attach({ image: img, canvas: out });
    if (fx) { frame.classList.add("fx-on"); }
  }

  if (img.complete && img.naturalWidth > 0) {
    boot();
  } else {
    img.addEventListener("load", boot, { once: true });
  }
})();
