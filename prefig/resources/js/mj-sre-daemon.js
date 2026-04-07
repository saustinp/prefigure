#! /usr/bin/env node

/*************************************************************************
 *
 *  mj-sre-daemon.js — persistent MathJax rendering daemon for prefigure
 *
 *  Identical math rendering pipeline as mj-sre-page.js, but instead of
 *  reading one HTML file from argv and writing one rendered HTML to
 *  stdout, this script:
 *
 *    1. Loads MathJax + speech-rule-engine ONCE at startup (~700 ms).
 *    2. Reads line-delimited JSON requests from stdin, one per line:
 *
 *         {"id":"<request-id>","format":"svg","latex":"<latex>"}
 *         {"id":"<request-id>","format":"braille","latex":"<latex>"}
 *
 *    3. Renders each request and writes a single line JSON response on
 *       stdout, framed by exactly one '\n':
 *
 *         {"id":"<request-id>","svg":"<rendered svg>"}
 *         {"id":"<request-id>","braille":"<braille text>"}
 *         {"id":"<request-id>","error":"<message>"}
 *
 *    4. Exits cleanly on stdin EOF.
 *
 *  Per-request cost after the one-time startup is ~5–30 ms instead of
 *  ~700 ms.  Combined with the in-memory + on-disk LaTeX→SVG cache in
 *  the prefigure backends, this turns batch builds with novel labels
 *  from N × 700 ms into 700 ms + N × 10 ms.
 *
 *  See profiling_comparison.py and the cache implementation in
 *  prefig/core/label_tools.py and prefigure-cpp/src/label_tools.cpp.
 *
 *************************************************************************/

//
// Same imports as mj-sre-page.js — we want the daemon to use the EXACT
// same library code path so the rendered output is byte-identical to
// what the one-shot script produces.  Any divergence here would break
// the round-trip equivalence between the daemon and the disk cache.
//
require('mathjax-full/js/util/asyncLoad/node.js');
const {mathjax} = require('mathjax-full/js/mathjax.js');
const {TeX} = require('mathjax-full/js/input/tex.js');
const {MathML} = require('mathjax-full/js/input/mathml.js');
const {SVG} = require('mathjax-full/js/output/svg.js');
const {RegisterHTMLHandler} = require('mathjax-full/js/handlers/html.js');
const {liteAdaptor} = require('mathjax-full/js/adaptors/liteAdaptor.js');
const {STATE, newState} = require('mathjax-full/js/core/MathItem.js');
const {EnrichHandler} = require('mathjax-full/js/a11y/semantic-enrich.js');
const {AllPackages} = require('mathjax-full/js/input/tex/AllPackages.js');
const {Sre} = require('mathjax-full/js/a11y/sre.js');
const {SerializedMmlVisitor} = require('mathjax-full/js/core/MmlTree/SerializedMmlVisitor.js');

const adaptor = liteAdaptor({fontSize: 16});
RegisterHTMLHandler(adaptor);

newState('PRETEXT', STATE.METRICS + 10);
newState('PRETEXTACTION', STATE.PRETEXT + 10);

// Used by the SRE-enriched SVG path: the rendered MathML output is fed
// back through a MathML→SVG conversion to attach speech metadata.
const mmldoc = mathjax.document('', {
  InputJax: new MathML(),
  OutputJax: new SVG({fontCache: 'local'}),
});

const visitor = new SerializedMmlVisitor();
const toMathML = (node) => visitor.visitTree(node, html);
let html;  // updated per-request below

//
// Helper: build the renderActions for a single request, mirroring the
// shape used by mj-sre-page.js for `--svg --svgenhanced --depth deep`
// (sighted) and `--braille` (tactile).
//
function svgRenderActions() {
  return {
    pretext: [STATE.PRETEXT, (doc) => {
      for (const math of doc.math) {
        math.outputData.pretext = [adaptor.text('\n')];
        math.outputData.mml = toMathML(math.root).toString();
      }
    }],
    typeset: [STATE.TYPESET, (doc) => {
      for (const math of doc.math) {
        math.typesetRoot = adaptor.node('mjx-data', {}, math.outputData.pretext);
      }
    }],
    svg: [STATE.PRETEXTACTION, (doc) => {
      for (const math of doc.math) {
        try {
          const out = mmldoc.convert(Sre.toEnriched(math.outputData.mml).toString());
          math.outputData.pretext.push(out);
          math.outputData.pretext.push(adaptor.text('\n'));
        } catch (err) {
          throw err;
        }
      }
    }],
  };
}

function brailleRenderActions() {
  return {
    pretext: [STATE.PRETEXT, (doc) => {
      for (const math of doc.math) {
        math.outputData.pretext = [adaptor.text('\n')];
        math.outputData.mml = toMathML(math.root).toString();
      }
    }],
    typeset: [STATE.TYPESET, (doc) => {
      for (const math of doc.math) {
        math.typesetRoot = adaptor.node('mjx-data', {}, math.outputData.pretext);
      }
    }],
    braille: [STATE.PRETEXTACTION, (doc) => {
      for (const math of doc.math) {
        const speech = Sre.toSpeech(math.outputData.mml);
        math.outputData.pretext.push(adaptor.node('mjx-braille', {}, [adaptor.text(speech)]));
        math.outputData.pretext.push(adaptor.text('\n'));
      }
    }],
  };
}

// Walk children of @parent and return the first one whose kind matches
// @kindName, or null if no such sibling exists.  We need this because
// the lite-adaptor tree includes whitespace text nodes (the "\n" entries
// the typeset action injects between siblings) so adaptor.firstChild
// usually points at one of those instead of the element we want.
function findChildByKind(parent, kindName) {
  let n = adaptor.firstChild(parent);
  while (n) {
    if (adaptor.kind(n) === kindName) return n;
    n = adaptor.next(n);
  }
  return null;
}

// Recursive descent: find the first descendant of @root whose kind matches
// @kindName.  Used as a defensive fallback if the rendered tree shape
// changes between MathJax versions.
function findDescendantByKind(root, kindName) {
  if (!root) return null;
  if (adaptor.kind(root) === kindName) return root;
  let n = adaptor.firstChild(root);
  while (n) {
    const found = findDescendantByKind(n, kindName);
    if (found) return found;
    n = adaptor.next(n);
  }
  return null;
}

//
// Render a single LaTeX expression in the requested format and return
// either { svg: "<svg>..." } / { braille: "..." } or { error: "..." }.
//
async function renderOne(latex, format) {
  // Build a tiny one-label HTML document and run the appropriate
  // renderActions over it.  Each call creates a fresh mathjax.document
  // because the renderActions are stateful per-document.
  const htmlText = '<html><body><div id="m">\\(' + latex + '\\)</div></body></html>';
  const renderActions = (format === 'braille') ? brailleRenderActions() : svgRenderActions();

  // Match mj-sre-page.js's --svgenhanced --depth deep configuration.
  // The `speech: 'deep'` flag is critical: without it, the rendered SVG
  // is missing the data-semantic-speech="..." attributes that diagcess
  // (prefigure's accessibility tool) reads to speak the math out loud.
  if (format === 'braille') {
    Sre.setupEngine({modality: 'braille', locale: 'nemeth', markup: 'layout', domain: 'default'});
  } else {
    Sre.setupEngine({speech: 'deep', modality: 'speech', locale: 'en', domain: 'mathspeak'});
  }

  html = mathjax.document(htmlText, {
    renderActions,
    InputJax: new TeX({packages: AllPackages}),
    OutputJax: new SVG({fontCache: 'local'}),
  });

  await mathjax.handleRetriesFor(() => html.render());

  // The rendered tree shape (matching mj-sre-page.js + svgenhanced) is:
  //   body → div(id="m") → mjx-data → mjx-container → svg
  // for sighted output, and
  //   body → div(id="m") → mjx-data → mjx-braille
  // for tactile.  In both cases there are also "\n" text siblings inside
  // the mjx-data, which is why we walk by kind rather than calling
  // firstChild() three times.  As a safety net we fall back to a recursive
  // descent if the direct path doesn't find the target.
  const div = findChildByKind(adaptor.body(html.document), 'div');
  if (!div) return {error: 'no <div> in rendered output'};
  const mjxData = findChildByKind(div, 'mjx-data');
  if (!mjxData) return {error: 'no <mjx-data> in rendered output'};

  if (format === 'braille') {
    let mjxBraille = findChildByKind(mjxData, 'mjx-braille');
    if (!mjxBraille) mjxBraille = findDescendantByKind(mjxData, 'mjx-braille');
    if (!mjxBraille) return {error: 'no mjx-braille in rendered output'};
    return {braille: adaptor.textContent(mjxBraille)};
  } else {
    const mjxContainer = findChildByKind(mjxData, 'mjx-container');
    if (!mjxContainer) return {error: 'no mjx-container in rendered output'};
    let svg = findChildByKind(mjxContainer, 'svg');
    if (!svg) svg = findDescendantByKind(mjxContainer, 'svg');
    if (!svg) return {error: 'no svg in rendered output'};
    return {svg: adaptor.outerHTML(svg)};
  }
}

//
// Wait for SRE to be ready, then enter the request loop.
// Stdin is read line-by-line via readline.  Each request is processed
// in order; we await each render before reading the next line so the
// response stream is well-ordered with respect to the request stream.
//
(async function main() {
  const feature = {
    xpath: require.resolve('wicked-good-xpath/dist/wgxpath.install-node.js'),
    json: require.resolve('speech-rule-engine/lib/mathmaps/base.json').replace(/\/base\.json$/, ''),
  };
  Sre.setupEngine(feature);
  Sre.setupEngine({locale: 'en'});
  await Sre.sreReady();

  // Signal readiness so the parent doesn't have to time the startup race.
  // We write a single line and flush.
  process.stdout.write(JSON.stringify({ready: true}) + '\n');

  // Line-buffered request reader.
  const readline = require('readline');
  const rl = readline.createInterface({input: process.stdin});

  // We process requests sequentially.  rl.on('line') would fire async
  // and could interleave; instead use an async iterator.
  for await (const line of rl) {
    if (!line) continue;
    let req;
    try {
      req = JSON.parse(line);
    } catch (err) {
      process.stdout.write(JSON.stringify({error: 'bad request: ' + err.message}) + '\n');
      continue;
    }
    const id = req.id || '';
    try {
      const result = await renderOne(req.latex || '', req.format || 'svg');
      result.id = id;
      process.stdout.write(JSON.stringify(result) + '\n');
    } catch (err) {
      process.stdout.write(JSON.stringify({id, error: err.message || String(err)}) + '\n');
    }
  }
})().catch((err) => {
  process.stderr.write('mj-sre-daemon fatal: ' + (err && err.stack ? err.stack : err) + '\n');
  process.exit(1);
});
