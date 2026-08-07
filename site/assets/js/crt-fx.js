/*
 * crt-fx.js — CRT-style treatment for a static screenshot.
 *
 * Original code written for the virtualjaguar-libretro project site.
 * License: GPLv3, same as the repository (see docs/GPLv3).
 *
 * Implements the usual CRT ingredients from first principles: barrel
 * distortion (p' = p * (1 + k*r^2)), a sine scanline mask over output rows,
 * radial vignette, per-pixel hash-noise grain, a small horizontal RGB channel
 * offset, a cheap horizontal glow, and a slow scan wobble.
 *
 * Contract: VJCrtFx.attach({ image, canvas }, options) draws `image` onto
 * `canvas` with the effect, returning { resize, destroy } — or null on ANY
 * failure (no WebGL, shader compile/link error, tainted image).  Callers must
 * treat null as "leave the plain <img> alone": the effect fails closed.
 */
(function (global) {
  "use strict";

  var DEFAULTS = {
    speed: 1,        /* time multiplier for grain/wobble animation        */
    curve: 0.055,    /* barrel distortion coefficient k                   */
    scanline: 0.16,  /* scanline darkening depth (0..1)                   */
    vignette: 0.4,   /* corner darkening amount (0..1)                    */
    grain: 0.05,     /* noise amplitude (0..1)                            */
    chroma: 1.1,     /* RGB offset in output pixels                       */
    glow: 0.3,       /* horizontal phosphor-glow mix (0..1)               */
    wobble: 0.0012,  /* horizontal scan wobble amplitude (uv units)       */
    exposure: 1.05,  /* final brightness multiplier                       */
    bezel: [0.063, 0.063, 0.078]  /* tube color outside the curved frame */
  };

  var VERT_SRC =
    "attribute vec2 aPos;\n" +
    "varying vec2 vUv;\n" +
    "void main () {\n" +
    "  vUv = aPos * 0.5 + 0.5;\n" +
    "  gl_Position = vec4(aPos, 0.0, 1.0);\n" +
    "}\n";

  var FRAG_SRC =
    "precision mediump float;\n" +
    "varying vec2 vUv;\n" +
    "uniform sampler2D uTex;\n" +
    "uniform vec2 uSize;\n" +
    "uniform float uTime;\n" +
    "uniform float uCurve;\n" +
    "uniform float uScanline;\n" +
    "uniform float uVignette;\n" +
    "uniform float uGrain;\n" +
    "uniform float uChroma;\n" +
    "uniform float uGlow;\n" +
    "uniform float uWobble;\n" +
    "uniform float uExposure;\n" +
    "uniform vec3 uBezel;\n" +
    "\n" +
    "float hash (vec2 p) {\n" +
    "  return fract(sin(dot(p, vec2(41.31, 289.17))) * 43758.5453);\n" +
    "}\n" +
    "\n" +
    "void main () {\n" +
    "  /* Barrel distortion about the center. */\n" +
    "  vec2 c = vUv * 2.0 - 1.0;\n" +
    "  c *= 1.0 + uCurve * dot(c, c);\n" +
    "  float edge = max(abs(c.x), abs(c.y));\n" +
    "  if (edge > 1.0) {\n" +
    "    gl_FragColor = vec4(uBezel, 1.0);\n" +
    "    return;\n" +
    "  }\n" +
    "  /* Fade the last few percent into the bezel so the tube edge is soft. */\n" +
    "  float mask = 1.0 - smoothstep(0.965, 1.0, edge);\n" +
    "  vec2 uv = c * 0.5 + 0.5;\n" +
    "\n" +
    "  /* Slow horizontal scan wobble. */\n" +
    "  uv.x += sin(uv.y * 6.5 + uTime * 0.7) * uWobble;\n" +
    "\n" +
    "  /* RGB channel offset (aberration) plus base sample. */\n" +
    "  float px = uChroma / max(uSize.x, 1.0);\n" +
    "  vec3 col;\n" +
    "  col.r = texture2D(uTex, uv + vec2(px, 0.0)).r;\n" +
    "  col.g = texture2D(uTex, uv).g;\n" +
    "  col.b = texture2D(uTex, uv - vec2(px, 0.0)).b;\n" +
    "\n" +
    "  /* Cheap horizontal glow: two wider taps averaged in. */\n" +
    "  vec3 spread = texture2D(uTex, uv + vec2(2.5 * px, 0.0)).rgb\n" +
    "              + texture2D(uTex, uv - vec2(2.5 * px, 0.0)).rgb;\n" +
    "  col = mix(col, (col + spread) / 3.0, uGlow);\n" +
    "\n" +
    "  /* Scanlines: darken between output rows. */\n" +
    "  float row = sin(uv.y * uSize.y * 3.14159265);\n" +
    "  col *= 1.0 - uScanline * (0.5 + 0.5 * row);\n" +
    "\n" +
    "  /* Vignette toward the corners. */\n" +
    "  float dist = length(c);\n" +
    "  col *= 1.0 - uVignette * smoothstep(0.55, 1.35, dist);\n" +
    "\n" +
    "  /* Animated grain. */\n" +
    "  col += (hash(uv * uSize + fract(uTime) * vec2(157.0, 113.0)) - 0.5)\n" +
    "         * uGrain;\n" +
    "\n" +
    "  col *= uExposure;\n" +
    "  gl_FragColor = vec4(mix(uBezel, col, mask), 1.0);\n" +
    "}\n";

  function attach(elements, options) {
    var image = elements && elements.image;
    var canvas = elements && elements.canvas;
    var opts = {};
    var key, gl;

    if (!image || !canvas) { return null; }
    for (key in DEFAULTS) { opts[key] = DEFAULTS[key]; }
    if (options) {
      for (key in options) {
        if (Object.prototype.hasOwnProperty.call(DEFAULTS, key)) {
          opts[key] = options[key];
        }
      }
    }

    /* preserveDrawingBuffer keeps the last frame valid for compositing and
     * screenshots; the canvas is small, so the cost is negligible.  The
     * shader writes alpha = 1 everywhere, so an alpha-enabled buffer looks
     * identical while compositing more reliably across browsers. */
    var ctxAttribs = { antialias: false, alpha: true,
                       premultipliedAlpha: false,
                       preserveDrawingBuffer: true };
    /* Prefer WebGL2 (the GLSL ES 1.00 shaders below are valid there too);
     * fall back to WebGL1. */
    gl = canvas.getContext("webgl2", ctxAttribs) ||
         canvas.getContext("webgl", ctxAttribs) ||
         canvas.getContext("experimental-webgl", ctxAttribs);
    if (!gl || gl.isContextLost()) { return null; }

    var program = null;
    var vs = null;
    var fs = null;
    var quad = null;
    var tex = null;

    /* Fail closed: undo every GL allocation and report failure so the
     * caller keeps the plain image visible. */
    function bail() {
      if (tex) { gl.deleteTexture(tex); }
      if (quad) { gl.deleteBuffer(quad); }
      if (program) { gl.deleteProgram(program); }
      if (vs) { gl.deleteShader(vs); }
      if (fs) { gl.deleteShader(fs); }
      return null;
    }

    function compile(type, src) {
      var sh = gl.createShader(type);
      if (!sh) { return null; }
      gl.shaderSource(sh, src);
      gl.compileShader(sh);
      if (!gl.getShaderParameter(sh, gl.COMPILE_STATUS)) {
        gl.deleteShader(sh);
        return null;
      }
      return sh;
    }

    vs = compile(gl.VERTEX_SHADER, VERT_SRC);
    if (!vs) { return bail(); }
    fs = compile(gl.FRAGMENT_SHADER, FRAG_SRC);
    if (!fs) { return bail(); }

    program = gl.createProgram();
    gl.attachShader(program, vs);
    gl.attachShader(program, fs);
    gl.linkProgram(program);
    if (!gl.getProgramParameter(program, gl.LINK_STATUS)) { return bail(); }

    quad = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, quad);
    gl.bufferData(gl.ARRAY_BUFFER,
                  new Float32Array([-1, -1, 1, -1, -1, 1, 1, 1]),
                  gl.STATIC_DRAW);
    var aPos = gl.getAttribLocation(program, "aPos");
    gl.enableVertexAttribArray(aPos);
    gl.vertexAttribPointer(aPos, 2, gl.FLOAT, false, 0, 0);

    /* Non-power-of-two texture: clamp + linear, no mips. */
    tex = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, tex);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, true);
    try {
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE,
                    image);
    } catch (err) {
      /* Cross-origin/tainted image: degrade to the plain <img>. */
      return bail();
    }

    var u = {};
    ["uTex", "uSize", "uTime", "uCurve", "uScanline", "uVignette", "uGrain",
     "uChroma", "uGlow", "uWobble", "uExposure", "uBezel"
    ].forEach(function (name) {
      u[name] = gl.getUniformLocation(program, name);
    });

    function fitCanvas() {
      var dpr = Math.min(global.devicePixelRatio || 1, 2);
      var cw = canvas.clientWidth;
      var ch = canvas.clientHeight;
      /* Not laid out yet (e.g. display:none until the fx-on class lands):
       * size from the source image so the first frame is full-resolution. */
      if (cw < 2 || ch < 2) {
        cw = image.naturalWidth || image.width || 1;
        ch = image.naturalHeight || image.height || 1;
      }
      var w = Math.max(1, Math.round(cw * dpr));
      var h = Math.max(1, Math.round(ch * dpr));
      if (canvas.width !== w || canvas.height !== h) {
        canvas.width = w;
        canvas.height = h;
      }
    }

    var time = 0;

    function draw() {
      gl.useProgram(program);
      gl.viewport(0, 0, canvas.width, canvas.height);
      gl.activeTexture(gl.TEXTURE0);
      gl.bindTexture(gl.TEXTURE_2D, tex);
      gl.uniform1i(u.uTex, 0);
      gl.uniform2f(u.uSize, canvas.width, canvas.height);
      gl.uniform1f(u.uTime, time);
      gl.uniform1f(u.uCurve, opts.curve);
      gl.uniform1f(u.uScanline, opts.scanline);
      gl.uniform1f(u.uVignette, opts.vignette);
      gl.uniform1f(u.uGrain, opts.grain);
      gl.uniform1f(u.uChroma, opts.chroma);
      gl.uniform1f(u.uGlow, opts.glow);
      gl.uniform1f(u.uWobble, opts.wobble);
      gl.uniform1f(u.uExposure, opts.exposure);
      gl.uniform3f(u.uBezel, opts.bezel[0], opts.bezel[1], opts.bezel[2]);
      gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
    }

    var destroyed = false;
    var running = false;
    var visible = true;
    var rafId = 0;
    var last = 0;
    var motionQuery = global.matchMedia ?
        global.matchMedia("(prefers-reduced-motion: reduce)") : null;

    function reduced() { return Boolean(motionQuery && motionQuery.matches); }

    function tick(now) {
      if (destroyed || !visible) { running = false; return; }
      if (last) { time += Math.min((now - last) / 1000, 0.1) * opts.speed; }
      last = now;
      draw();
      if (reduced()) {
        /* One static frame is enough under prefers-reduced-motion. */
        running = false;
        return;
      }
      rafId = global.requestAnimationFrame(tick);
    }

    function start() {
      if (destroyed || running || !visible) { return; }
      running = true;
      last = 0;
      rafId = global.requestAnimationFrame(tick);
    }

    function onMotionChange() { start(); }
    if (motionQuery && motionQuery.addEventListener) {
      motionQuery.addEventListener("change", onMotionChange);
    }

    var ro = null;
    if (global.ResizeObserver) {
      ro = new ResizeObserver(function () {
        fitCanvas();
        start();
      });
      ro.observe(canvas);
    }

    var io = null;
    if (global.IntersectionObserver) {
      io = new IntersectionObserver(function (entries) {
        var e = entries[entries.length - 1];
        visible = e ? e.isIntersecting : true;
        if (visible) { start(); }
      });
      io.observe(canvas);
    }

    /* Draw one frame synchronously so the canvas is never composited empty
     * between the caller flipping it visible and the first rAF tick
     * (preserveDrawingBuffer keeps this frame valid). */
    fitCanvas();
    draw();
    start();

    return {
      resize: function () {
        fitCanvas();
        start();
      },
      destroy: function () {
        destroyed = true;
        global.cancelAnimationFrame(rafId);
        if (ro) { ro.disconnect(); }
        if (io) { io.disconnect(); }
        if (motionQuery && motionQuery.removeEventListener) {
          motionQuery.removeEventListener("change", onMotionChange);
        }
        bail();
      }
    };
  }

  global.VJCrtFx = { attach: attach };
})(window);
