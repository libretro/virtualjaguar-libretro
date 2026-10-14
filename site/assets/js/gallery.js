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
    var c = a.parentNode.querySelector(".gal-cap");
    cap.textContent = c ? c.textContent : "";
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
