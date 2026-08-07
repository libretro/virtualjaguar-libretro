/*!
 * Canvas UI — VHS (vanilla), vendored.
 *
 * Upstream:  https://github.com/DavidHDev/canvas-ui
 *            src/lib/VHS/VHSVanilla.ts
 * Commit:    880de315dd23d8add253575655ddc57f2160a19d (canvasui.dev)
 * License:   MIT + Commons Clause v1.0 — see LICENSE-canvas-ui.md in this
 *            directory.  Copyright (c) 2026 David Haz.
 *
 * Local adaptation (documented per docs/site-maintenance.md; no CDN, no
 * external requests): TypeScript types stripped by hand; the experimental
 * html-in-canvas capture path (drawElementImage/requestPaint) is replaced by
 * a one-time <img> texture upload (createVHSFromImage), because this site
 * applies the effect to a static screenshot, not live DOM.  The shaders,
 * uniforms, render loop, reduced-motion handling and observer wiring are
 * unchanged from upstream.
 */
(function (global) {
  "use strict";

  var DEFAULTS = {
    speed: 0.5,
    wave: 1,
    jitter: 0.25,
    crease: 0.1,
    switching: 0.05,
    switchingHeight: 0.02,
    bloom: 0.4,
    aberration: 2,
    acBeat: 1,
    grain: 0.1,
    scanlines: 0.1,
    vignette: 0,
    barrel: 0,
    saturation: 1,
    exposure: 1
  };

  var VERT = "#version 300 es\n" +
    "precision highp float;\n" +
    "layout(location = 0) in vec2 aPos;\n" +
    "out vec2 vUv;\n" +
    "void main () {\n" +
    "  vUv = aPos * 0.5 + 0.5;\n" +
    "  gl_Position = vec4(aPos, 0.0, 1.0);\n" +
    "}";

  var FRAG = "#version 300 es\n" +
    "precision highp float;\n" +
    "in vec2 vUv;\n" +
    "out vec4 outColor;\n" +
    "uniform sampler2D uContent;\n" +
    "uniform vec2 uResolution;\n" +
    "uniform float uTime;\n" +
    "uniform float uWave;\n" +
    "uniform float uJitter;\n" +
    "uniform float uCrease;\n" +
    "uniform float uSwitching;\n" +
    "uniform float uSwitchHeight;\n" +
    "uniform float uBloom;\n" +
    "uniform float uAberration;\n" +
    "uniform float uAcBeat;\n" +
    "uniform float uGrain;\n" +
    "uniform float uScanlines;\n" +
    "uniform float uVignette;\n" +
    "uniform float uSaturation;\n" +
    "uniform float uExposure;\n" +
    "uniform float uBarrel;\n" +
    "uniform vec3 uBezel;\n" +
    "uniform float uCreaseNoise;\n" +
    "uniform float uMaxX;\n" +
    "\n" +
    "#define PI 3.14159265\n" +
    "\n" +
    "float hash (vec2 v) {\n" +
    "  return fract(sin(dot(v, vec2(89.44, 19.36))) * 22189.22);\n" +
    "}\n" +
    "\n" +
    "float iHash (vec2 v, vec2 r) {\n" +
    "  float h00 = hash(floor(v * r + vec2(0.0, 0.0)) / r);\n" +
    "  float h10 = hash(floor(v * r + vec2(1.0, 0.0)) / r);\n" +
    "  float h01 = hash(floor(v * r + vec2(0.0, 1.0)) / r);\n" +
    "  float h11 = hash(floor(v * r + vec2(1.0, 1.0)) / r);\n" +
    "  vec2 ip = smoothstep(vec2(0.0), vec2(1.0), mod(v * r, 1.0));\n" +
    "  return (h00 * (1.0 - ip.x) + h10 * ip.x) * (1.0 - ip.y)\n" +
    "    + (h01 * (1.0 - ip.x) + h11 * ip.x) * ip.y;\n" +
    "}\n" +
    "\n" +
    "float noise (vec2 v) {\n" +
    "  float sum = 0.0;\n" +
    "  float s = 2.0;\n" +
    "  for (int i = 1; i < 7; i++) {\n" +
    "    sum += iHash(v + vec2(i), vec2(2.0 * s)) / s;\n" +
    "    s *= 2.0;\n" +
    "  }\n" +
    "  return sum;\n" +
    "}\n" +
    "\n" +
    "vec4 tape (vec2 p) {\n" +
    "  p.x = clamp(p.x, 0.0005, uMaxX - 0.0005);\n" +
    "  p.y = clamp(p.y, 0.0005, 0.9995);\n" +
    "  return texture(uContent, vec2(p.x, 1.0 - p.y));\n" +
    "}\n" +
    "\n" +
    "void main () {\n" +
    "  vec2 uv = vUv;\n" +
    "  if (uv.x > uMaxX) {\n" +
    "    outColor = vec4(0.0);\n" +
    "    return;\n" +
    "  }\n" +
    "\n" +
    "  float edgeMask = 1.0;\n" +
    "  if (uBarrel > 0.0) {\n" +
    "    vec2 c = vec2(uv.x / uMaxX, uv.y) * 2.0 - 1.0;\n" +
    "    c *= 1.0 + uBarrel * 0.15 * dot(c, c);\n" +
    "    float m = max(abs(c.x), abs(c.y));\n" +
    "    edgeMask = 1.0 - smoothstep(1.0 - 0.12 * uBarrel, 1.0, m);\n" +
    "    if (edgeMask <= 0.0) {\n" +
    "      outColor = vec4(uBezel, 1.0);\n" +
    "      return;\n" +
    "    }\n" +
    "    uv = vec2((c.x * 0.5 + 0.5) * uMaxX, c.y * 0.5 + 0.5);\n" +
    "  }\n" +
    "\n" +
    "  vec2 uvn = uv;\n" +
    "  float t = uTime;\n" +
    "\n" +
    "  float lineNoise = 0.0;\n" +
    "  if (uJitter + uCrease + uSwitching > 0.0) {\n" +
    "    lineNoise = noise(vec2(uvn.y * 100.0, t * 10.0));\n" +
    "  }\n" +
    "\n" +
    "  if (uWave > 0.0) {\n" +
    "    uvn.x += (noise(vec2(uvn.y, t)) - 0.5) * 0.005 * uWave;\n" +
    "  }\n" +
    "  uvn.x += (lineNoise - 0.5) * 0.01 * uJitter;\n" +
    "\n" +
    "  float tcPhase = clamp(\n" +
    "    (sin(uvn.y * 8.0 - t * PI * 1.2) - 0.92) * uCreaseNoise,\n" +
    "    0.0, 0.01\n" +
    "  ) * 10.0 * uCrease;\n" +
    "  float tcNoise = max(lineNoise - 0.5, 0.0);\n" +
    "  uvn.x -= tcNoise * tcPhase;\n" +
    "\n" +
    "  float snPhase = smoothstep(max(uSwitchHeight, 1e-4), 0.0, uvn.y) * uSwitching;\n" +
    "  uvn.y += snPhase * 0.3;\n" +
    "  uvn.x += snPhase * ((lineNoise - 0.5) * 0.2);\n" +
    "\n" +
    "  vec4 base = tape(uvn);\n" +
    "  vec3 col = base.rgb;\n" +
    "  col *= 1.0 - tcPhase;\n" +
    "\n" +
    "  col = mix(col, col.yzx, clamp(snPhase, 0.0, 1.0));\n" +
    "\n" +
    "  if (uBloom > 0.0) {\n" +
    "    float px = uAberration / max(uResolution.x, 1.0);\n" +
    "    vec3 bloomSum = vec3(0.0);\n" +
    "    for (int i = -8; i <= 2; i++) {\n" +
    "      vec3 s = tape(uvn + vec2(float(i) * px, 0.0)).rgb;\n" +
    "      if (i >= -4) bloomSum.r += s.r;\n" +
    "      if (i >= -6 && i <= 0) bloomSum.g += s.g;\n" +
    "      if (i <= -2) bloomSum.b += s.b;\n" +
    "    }\n" +
    "    bloomSum *= 0.1;\n" +
    "\n" +
    "    col = mix(col, (col + bloomSum) / 1.7, clamp(uBloom, 0.0, 1.0));\n" +
    "  }\n" +
    "\n" +
    "  if (uAcBeat > 0.0) {\n" +
    "    col *= 1.0 + clamp(\n" +
    "      noise(vec2(0.0, uv.y + t * 0.2)) * 0.6 - 0.25, 0.0, 0.1\n" +
    "    ) * uAcBeat;\n" +
    "  }\n" +
    "\n" +
    "  float g = hash(uv * uResolution + fract(t) * vec2(127.1, 311.7)) - 0.5;\n" +
    "  col += g * uGrain;\n" +
    "\n" +
    "  float scan = sin(uv.y * uResolution.y * PI) * 0.5;\n" +
    "  col *= 1.0 - uScanlines * 0.35 * scan;\n" +
    "\n" +
    "  vec2 vd = (uv - 0.5) * vec2(uResolution.x / max(uResolution.y, 1.0), 1.0);\n" +
    "  col *= 1.0 - uVignette * smoothstep(0.4, 1.1, length(vd));\n" +
    "\n" +
    "  float lum = dot(col, vec3(0.299, 0.587, 0.114));\n" +
    "  col = mix(vec3(lum), col, clamp(uSaturation, 0.0, 2.0));\n" +
    "\n" +
    "  col *= uExposure;\n" +
    "\n" +
    "  float alpha = max(base.a, clamp(snPhase + tcPhase, 0.0, 1.0));\n" +
    "\n" +
    "  if (uBarrel > 0.0) {\n" +
    "    col = mix(uBezel, col, edgeMask);\n" +
    "    alpha = 1.0;\n" +
    "  }\n" +
    "  outColor = vec4(col, alpha);\n" +
    "}";

  /* CPU-side copies of the shader noise, used for the uCreaseNoise uniform
   * (unchanged from upstream). */
  function fract(x) { return x - Math.floor(x); }
  function hash2(x, y) {
    return fract(Math.sin(x * 89.44 + y * 19.36) * 22189.22);
  }
  function smooth01(x) { return x * x * (3 - 2 * x); }
  function iHashCpu(vx, vy, r) {
    var fx = Math.floor(vx * r);
    var fy = Math.floor(vy * r);
    var h00 = hash2(fx / r, fy / r);
    var h10 = hash2((fx + 1) / r, fy / r);
    var h01 = hash2(fx / r, (fy + 1) / r);
    var h11 = hash2((fx + 1) / r, (fy + 1) / r);
    var ix = smooth01(fract(vx * r));
    var iy = smooth01(fract(vy * r));
    return (h00 * (1 - ix) + h10 * ix) * (1 - iy) +
           (h01 * (1 - ix) + h11 * ix) * iy;
  }
  function noiseCpu(vx, vy) {
    var sum = 0;
    var s = 2;
    var i;
    for (i = 1; i < 7; i++) {
      sum += iHashCpu(vx + i, vy + i, 2 * s) / s;
      s *= 2;
    }
    return sum;
  }

  /**
   * Apply the VHS effect to a static image.
   *
   * elements: { image: HTMLImageElement (loaded), output: HTMLCanvasElement }
   * options:  see DEFAULTS above (same knobs as upstream VHSOptions).
   * Returns an instance { setOptions, resize, destroy }, or null when WebGL2
   * is unavailable -- callers must leave the plain <img> in place then.
   */
  function createVHSFromImage(elements, options) {
    var config = {};
    var key;
    for (key in DEFAULTS) { config[key] = DEFAULTS[key]; }
    if (options) { for (key in options) { config[key] = options[key]; } }

    var image = elements.image;
    var output = elements.output;

    var gl = output.getContext("webgl2", {
      alpha: true,
      depth: false,
      stencil: false,
      antialias: false,
      premultipliedAlpha: false
    });
    if (!gl || gl.isContextLost()) { return null; }

    function compile(type, text) {
      var shader = gl.createShader(type);
      gl.shaderSource(shader, text);
      gl.compileShader(shader);
      if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
        console.error("VHS shader error:", gl.getShaderInfoLog(shader));
      }
      return shader;
    }

    var vertexShader = compile(gl.VERTEX_SHADER, VERT);
    var fragmentShader = compile(gl.FRAGMENT_SHADER, FRAG);
    var program = gl.createProgram();
    gl.attachShader(program, vertexShader);
    gl.attachShader(program, fragmentShader);
    gl.linkProgram(program);

    var uniforms = {};
    var count = gl.getProgramParameter(program, gl.ACTIVE_UNIFORMS);
    var i, info;
    for (i = 0; i < count; i++) {
      info = gl.getActiveUniform(program, i);
      uniforms[info.name] = gl.getUniformLocation(program, info.name);
    }

    var quad = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, quad);
    gl.bufferData(gl.ARRAY_BUFFER,
                  new Float32Array([-1, -1, 1, -1, -1, 1, 1, 1]),
                  gl.STATIC_DRAW);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 0, 0);

    var contentTexture = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, contentTexture);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    /* Adaptation: the content texture is the screenshot, uploaded once,
     * instead of an html-in-canvas capture re-uploaded per paint.  A
     * cross-origin or tainted image throws SecurityError here; degrade to
     * the plain <img> instead of leaving a broken canvas. */
    try {
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE,
                    image);
    } catch (err) {
      gl.deleteTexture(contentTexture);
      gl.deleteProgram(program);
      gl.deleteShader(vertexShader);
      gl.deleteShader(fragmentShader);
      gl.deleteBuffer(quad);
      return null;
    }

    var contentMaxX = 1;

    /* Bezel color: nearest non-transparent computed background above the
     * image (same walk as upstream syncBezelColor, rooted at the image). */
    var bezel = [0, 0, 0];
    var bezelProbe = document.createElement("canvas");
    bezelProbe.width = bezelProbe.height = 1;
    var bezelCtx = bezelProbe.getContext("2d", { willReadFrequently: true });

    function syncBezelColor() {
      if (!bezelCtx) { return; }
      var el = image;
      var bg, data;
      while (el) {
        bg = getComputedStyle(el).backgroundColor;
        if (bg && bg !== "transparent") {
          bezelCtx.clearRect(0, 0, 1, 1);
          bezelCtx.fillStyle = bg;
          bezelCtx.fillRect(0, 0, 1, 1);
          data = bezelCtx.getImageData(0, 0, 1, 1).data;
          if (data[3] > 0) {
            bezel = [data[0] / 255, data[1] / 255, data[2] / 255];
            return;
          }
        }
        el = el.parentElement;
      }
      bezel = [0, 0, 0];
    }

    function syncCanvasSize() {
      var dpr = Math.min(window.devicePixelRatio || 1, 2);
      var width = Math.max(1, Math.round(output.clientWidth * dpr));
      var height = Math.max(1, Math.round(output.clientHeight * dpr));
      if (output.width !== width || output.height !== height) {
        output.width = width;
        output.height = height;
      }
    }

    syncCanvasSize();
    syncBezelColor();

    var time = 0;

    function render() {
      gl.useProgram(program);
      gl.activeTexture(gl.TEXTURE0);
      gl.bindTexture(gl.TEXTURE_2D, contentTexture);
      gl.uniform1i(uniforms.uContent, 0);
      gl.uniform2f(uniforms.uResolution, output.width, output.height);
      gl.uniform1f(uniforms.uTime, time);
      gl.uniform1f(uniforms.uWave, Math.max(config.wave, 0));
      gl.uniform1f(uniforms.uJitter, Math.max(config.jitter, 0));
      gl.uniform1f(uniforms.uCrease, Math.max(config.crease, 0));
      gl.uniform1f(uniforms.uSwitching, Math.max(config.switching, 0));
      gl.uniform1f(uniforms.uSwitchHeight, Math.max(config.switchingHeight, 0));
      gl.uniform1f(uniforms.uBloom, config.bloom);
      var dpr = output.width / Math.max(output.clientWidth, 1);
      gl.uniform1f(uniforms.uAberration, Math.max(config.aberration, 0) * dpr);
      gl.uniform1f(uniforms.uAcBeat, Math.max(config.acBeat, 0));
      gl.uniform1f(uniforms.uGrain, Math.max(config.grain, 0));
      gl.uniform1f(uniforms.uScanlines, Math.max(config.scanlines, 0));
      gl.uniform1f(uniforms.uVignette, Math.max(config.vignette, 0));
      gl.uniform1f(uniforms.uBarrel, Math.max(config.barrel, 0));
      gl.uniform3f(uniforms.uBezel, bezel[0], bezel[1], bezel[2]);
      gl.uniform1f(uniforms.uCreaseNoise, noiseCpu(time, time));
      gl.uniform1f(uniforms.uSaturation, config.saturation);
      gl.uniform1f(uniforms.uExposure, Math.max(config.exposure, 0));
      gl.uniform1f(uniforms.uMaxX, contentMaxX);
      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
      gl.viewport(0, 0, output.width, output.height);
      gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
    }

    var raf = 0;
    var lastTime = performance.now();
    var destroyed = false;
    var running = false;
    var visible = true;

    var motionQuery = window.matchMedia("(prefers-reduced-motion: reduce)");
    var reducedMotion = motionQuery.matches;

    function frame(now) {
      if (destroyed) { return; }
      if (!visible) {
        running = false;
        return;
      }
      var delta = Math.min((now - lastTime) / 1000, 1 / 30);
      lastTime = now;
      if (!reducedMotion) { time += delta * config.speed; }
      render();
      if (reducedMotion) {
        /* One static frame is enough when the user prefers reduced motion. */
        running = false;
        return;
      }
      raf = requestAnimationFrame(frame);
    }

    function start() {
      if (destroyed || running || !visible) { return; }
      running = true;
      lastTime = performance.now();
      raf = requestAnimationFrame(frame);
    }

    start();

    function onMotionChange() {
      reducedMotion = motionQuery.matches;
      start();
    }
    motionQuery.addEventListener("change", onMotionChange);

    var observer = new ResizeObserver(function () {
      syncCanvasSize();
      start();
    });
    observer.observe(output);

    var intersection = new IntersectionObserver(function (entries) {
      var last = entries[entries.length - 1];
      visible = last ? last.isIntersecting : true;
      if (visible) { start(); }
    });
    intersection.observe(output);

    return {
      setOptions: function (next) {
        var changed = false;
        var k;
        for (k in next) {
          if (config[k] !== next[k]) { changed = true; }
        }
        if (!changed) { return; }
        for (k in next) { config[k] = next[k]; }
        start();
      },
      resize: function () {
        syncCanvasSize();
        start();
      },
      destroy: function () {
        destroyed = true;
        cancelAnimationFrame(raf);
        observer.disconnect();
        intersection.disconnect();
        motionQuery.removeEventListener("change", onMotionChange);
        gl.deleteTexture(contentTexture);
        gl.deleteProgram(program);
        gl.deleteShader(vertexShader);
        gl.deleteShader(fragmentShader);
        gl.deleteBuffer(quad);
      }
    };
  }

  global.CanvasUIVHS = { createVHSFromImage: createVHSFromImage };
})(window);
