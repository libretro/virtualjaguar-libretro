/* Screenshot gallery lightbox.  Progressive enhancement: every thumbnail is a
 * plain <a href="full image">, so without this script it opens the picture.
 * With it, the link opens a <dialog>: Esc closes it (native), the arrow keys
 * and the buttons move between pictures, and focus goes back to the card that
 * opened it.  No library, no network. */
(function () {
  "use strict";
  var links = Array.prototype.slice.call(document.querySelectorAll("a.gal-link"));
  if (!links.length || typeof HTMLDialogElement === "undefined") return;

  var dlg = document.createElement("dialog");
  dlg.className = "lightbox";
  dlg.setAttribute("aria-label", "Screenshot viewer");
  dlg.innerHTML =
    '<div class="lb-bar"><span class="lb-title"></span>' +
    '<button type="button" class="lb-btn lb-prev" aria-label="Previous screenshot">&lsaquo; Prev</button>' +
    '<button type="button" class="lb-btn lb-next" aria-label="Next screenshot">Next &rsaquo;</button>' +
    '<button type="button" class="lb-btn lb-close" aria-label="Close">Close</button></div>' +
    '<div class="lb-well"><img class="lb-img" alt=""></div>' +
    '<p class="lb-cap"></p>';
  document.body.appendChild(dlg);

  var img = dlg.querySelector(".lb-img");
  var cap = dlg.querySelector(".lb-cap");
  var title = dlg.querySelector(".lb-title");
  var index = 0;
  var opener = null;

  function show(i) {
    index = (i + links.length) % links.length;
    var a = links[index];
    var thumb = a.querySelector("img");
    img.src = a.getAttribute("href");
    img.alt = thumb ? thumb.getAttribute("alt") : "";
    title.textContent = (index + 1) + " / " + links.length;
    /* Gallery cards keep the caption in .gal-cap; a captured frame
     * (figure.shot) uses its window title and figcaption. */
    var fig = a.closest ? a.closest("figure") : null;
    var c = fig && (fig.querySelector(".gal-cap") || fig.querySelector("figcaption"));
    var bar = fig && !fig.querySelector(".gal-cap") ? fig.querySelector(".win-bar") : null;
    var head = "";
    if (bar) {
      var badge = bar.querySelector(".badge");
      head = bar.firstChild ? bar.firstChild.textContent.trim() : "";
      if (badge) head += " (" + badge.textContent.trim() + ")";
      head += ". ";
    }
    cap.textContent = head +
                      (c ? c.textContent.replace(/\s+/g, " ").trim() : "");
  }

  links.forEach(function (a, i) {
    a.addEventListener("click", function (e) {
      if (e.ctrlKey || e.metaKey || e.shiftKey || e.button !== 0) return;
      e.preventDefault();
      opener = a;
      show(i);
      dlg.showModal();
      dlg.querySelector(".lb-close").focus();
    });
  });

  dlg.querySelector(".lb-prev").addEventListener("click", function () { show(index - 1); });
  dlg.querySelector(".lb-next").addEventListener("click", function () { show(index + 1); });
  dlg.querySelector(".lb-close").addEventListener("click", function () { dlg.close(); });
  dlg.addEventListener("click", function (e) {
    if (e.target === dlg) dlg.close();   // a click on the backdrop
  });
  dlg.addEventListener("keydown", function (e) {
    if (e.key === "ArrowLeft") { e.preventDefault(); show(index - 1); }
    else if (e.key === "ArrowRight") { e.preventDefault(); show(index + 1); }
  });
  dlg.addEventListener("close", function () {
    img.removeAttribute("src");
    if (opener) opener.focus();
  });
})();

/* Screenshot strip (home page): Prev/Next buttons that page the strip by
 * what is visible.  The strip scrolls without this (touch, trackpad, the
 * scrollbar, the keyboard once a card has focus); the buttons are hidden
 * until this runs. */
(function () {
  "use strict";
  var strips = Array.prototype.slice.call(document.querySelectorAll(".strip-wrap"));
  strips.forEach(function (wrap) {
    var strip = wrap.querySelector(".strip");
    var bar = wrap.querySelector(".strip-bar");
    if (!strip || !bar) return;
    var prev = bar.querySelector(".strip-prev");
    var next = bar.querySelector(".strip-next");
    var reduce = window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches;
    function page(dir) {
      strip.scrollBy({ left: dir * strip.clientWidth, behavior: reduce ? "auto" : "smooth" });
    }
    function update() {
      var max = strip.scrollWidth - strip.clientWidth - 2;
      prev.disabled = strip.scrollLeft <= 2;
      next.disabled = strip.scrollLeft >= max;
    }
    prev.addEventListener("click", function () { page(-1); });
    next.addEventListener("click", function () { page(1); });
    strip.addEventListener("scroll", update, { passive: true });
    window.addEventListener("resize", update);
    bar.hidden = false;
    update();
  });
})();
