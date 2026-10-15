/* Download widget (Download page, and the app page's "Get it" window):
 * release picker and installer tabs.
 *
 * Progressive enhancement.  The generator renders one panel per release and
 * shows only the latest; without this script that is what you get, plus links
 * to the GitHub release list.  With it, the picker (hidden in the markup)
 * appears and just toggles which panel is visible.  No markup is built here,
 * no network is used.
 *
 *   - Nightly / Latest buttons (aria-pressed) and an "Older releases" select.
 *   - The choice is mirrored in the URL hash (#nightly, #v3.8.0) so a link can
 *     point at one release; #installer, #tools and #verify open their section.
 *   - The installer commands pick up --channel (or $env:VJ_CHANNEL) for the
 *     selected release.
 *   - The installer's OS tabs follow the WAI-ARIA tabs pattern (arrow keys,
 *     Home/End, roving tabindex). */
(function () {
  "use strict";
  var root = document.querySelector("section.dl");
  if (!root) return;

  var panels = Array.prototype.slice.call(root.querySelectorAll(".dl-panel"));
  var picker = root.querySelector(".dl-picker");
  if (!panels.length || !picker) return;

  var buttons = Array.prototype.slice.call(picker.querySelectorAll(".dl-btn"));
  var older = document.getElementById("dl-older");
  var status = document.getElementById("dl-status");
  var nojs = root.querySelector(".dl-nojs");
  var latestTag = null;
  panels.forEach(function (p) {
    if (p.getAttribute("data-kind") === "latest") latestTag = p.getAttribute("data-rel");
  });

  function panelFor(rel) {
    if (rel === "latest") rel = latestTag;
    for (var i = 0; i < panels.length; i++) {
      if (panels[i].getAttribute("data-rel") === rel) return panels[i];
    }
    return null;
  }

  /* Installer commands for the selected release (data-what presets
   * --what, on the app page). */
  var cmds = Array.prototype.slice.call(root.querySelectorAll("code[data-cmd]"));
  function setCommands(panel) {
    var kind = panel.getAttribute("data-kind");
    var rel = panel.getAttribute("data-rel");
    cmds.forEach(function (el) {
      var base = el.getAttribute("data-base");
      var what = el.getAttribute("data-what");
      var channel = kind !== "latest" ? rel : null;
      var text;
      if (el.getAttribute("data-cmd") === "sh") {
        var args = (what ? " --what " + what : "") + (channel ? " --channel " + channel : "");
        text = args ? base + " -s --" + args : base;
      } else {
        text = (what ? "$env:VJ_WHAT = '" + what + "'; " : "") +
               (channel ? "$env:VJ_CHANNEL = '" + channel + "'; " : "") + base;
      }
      el.textContent = text;
    });
  }

  function show(rel, opts) {
    var panel = panelFor(rel);
    if (!panel) return false;
    var tag = panel.getAttribute("data-rel");
    panels.forEach(function (p) { p.hidden = p !== panel; });
    buttons.forEach(function (b) {
      b.setAttribute("aria-pressed", b.getAttribute("data-rel") === tag ? "true" : "false");
    });
    if (older) {
      var isOlder = panel.getAttribute("data-kind") === "older";
      older.value = isOlder ? tag : "";
      older.classList.toggle("on", isOlder);
    }
    setCommands(panel);
    if (status) {
      status.textContent = "Showing " + (tag === "nightly" ? "the nightly build" : tag);
    }
    if (!opts || opts.hash !== false) {
      try {
        history.replaceState(null, "", panel.getAttribute("data-kind") === "latest" ? location.pathname + location.search : "#" + tag);
      } catch (e) { /* file:// or sandbox: the hash is a convenience */ }
    }
    return true;
  }

  picker.hidden = false;
  if (nojs) nojs.hidden = true;

  buttons.forEach(function (b) {
    b.addEventListener("click", function () { show(b.getAttribute("data-rel")); });
  });
  if (older) {
    older.addEventListener("change", function () {
      if (older.value) show(older.value);
    });
  }
  /* "No app in this release" pointers: switch panel instead of leaving. */
  root.addEventListener("click", function (ev) {
    var a = ev.target.closest ? ev.target.closest("a[data-rel-link]") : null;
    if (!a || ev.metaKey || ev.ctrlKey || ev.shiftKey) return;
    if (show(a.getAttribute("data-rel-link"))) {
      ev.preventDefault();
      picker.scrollIntoView({ block: "nearest" });
    }
  });

  /* Hash: a release, or a section to open. */
  function fromHash(initial) {
    var h = decodeURIComponent(location.hash.replace(/^#/, ""));
    if (!h) return;
    if (show(h, { hash: false })) return;
    var el = null;
    if (h === "core" || h === "tools") {
      var cur = root.querySelector(".dl-panel:not([hidden])");
      el = h === "tools" ? cur.querySelector("details.tools") : cur.querySelectorAll(".dl-h")[1];
      if (h === "tools" && el) el.open = true;
    } else if (h === "installer" || h === "verify") {
      el = document.getElementById(h);
      if (h === "installer" && el) el.open = true;
    }
    if (el && el.scrollIntoView) el.scrollIntoView();
  }
  window.addEventListener("hashchange", function () { fromHash(false); });
  fromHash(true);
  if (!root.querySelector(".dl-panel:not([hidden])")) show("latest", { hash: false });
  else setCommands(root.querySelector(".dl-panel:not([hidden])"));

  /* Installer tabs. */
  var tablist = root.querySelector(".inst-tabs");
  if (tablist) {
    var tabs = Array.prototype.slice.call(tablist.querySelectorAll("[role=tab]"));
    var pick = function (tab, focus) {
      tabs.forEach(function (t) {
        var on = t === tab;
        t.setAttribute("aria-selected", on ? "true" : "false");
        t.tabIndex = on ? 0 : -1;
        document.getElementById(t.getAttribute("aria-controls")).hidden = !on;
      });
      if (focus) tab.focus();
    };
    tablist.hidden = false;
    /* Default to the visitor's OS when it can be told; otherwise first tab. */
    var plat = (navigator.platform || "") + " " + (navigator.userAgent || "");
    pick(/Win/i.test(plat) && !/Darwin|Mac/i.test(plat) ? tabs[1] : tabs[0], false);
    tabs.forEach(function (t, i) {
      t.addEventListener("click", function () { pick(t, false); });
      t.addEventListener("keydown", function (ev) {
        var n = null;
        if (ev.key === "ArrowRight") n = tabs[(i + 1) % tabs.length];
        else if (ev.key === "ArrowLeft") n = tabs[(i + tabs.length - 1) % tabs.length];
        else if (ev.key === "Home") n = tabs[0];
        else if (ev.key === "End") n = tabs[tabs.length - 1];
        if (n) { ev.preventDefault(); pick(n, true); }
      });
    });
  }
})();
