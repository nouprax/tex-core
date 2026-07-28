/* Audits the shared render-tree conformance corpus: manifest shape, file
 * discovery, dump/error-record grammar, declared coverage, and completeness
 * against the schema contract. Modeled on markdown-core's canonical-AST
 * checker. Fails closed: every vocabulary here must exactly match the
 * manifest, and every declared label must be demonstrated. */

import { readFile, readdir } from "node:fs/promises";
import path from "node:path";
import process from "node:process";
import { TextDecoder, TextEncoder } from "node:util";

const root = process.cwd();
const contractPath = path.join(root, "docs/specs/render-tree.md");
const dumpGrammarPath = path.join(root, "docs/specs/render-tree-dump.md");
const specPath = path.join(root, "specs/render-tree");
const manifestPath = path.join(specPath, "manifest.json");
const decoder = new TextDecoder("utf-8", { fatal: true });

const [contract, dumpGrammar, manifestText, entries] = await Promise.all([
    readFile(contractPath, "utf8"),
    readFile(dumpGrammarPath, "utf8"),
    readFile(manifestPath, "utf8"),
    readdir(specPath)
]);
const manifest = JSON.parse(manifestText);
const failures = [];
const difference = (left, right) => [...left].filter((value) => !right.has(value)).sort();
const sameArray = (left, right) => left.length === right.length && left.every((value, index) => value === right[index]);
const set = (values) => new Set(values);

// The contract's node inventory is the field authority; the dump grammar's
// field-order table must be the same inventory minus the structural
// `children` edge. Parsing both keeps the two documents from drifting.
const inventory = contract.match(/## Node inventory[\s\S]*?## Source ranges/)?.[0];
if (inventory === undefined) throw new Error("Unable to locate the contract node inventory");
const inventoryRows = [...inventory.matchAll(/^\| `([a-z]+)` \| ([^|]+) \|/gm)];
const canonicalKinds = inventoryRows.map((match) => match[1]);
const fieldsByKind = Object.fromEntries(
    inventoryRows.map((match) => [match[1], [...match[2].matchAll(/`([A-Za-z]+):/g)].map((field) => field[1])])
);
const dumpFieldsByKind = Object.fromEntries(
    canonicalKinds.map((kind) => [kind, fieldsByKind[kind].filter((field) => field !== "children")])
);
const canonicalFields = canonicalKinds.flatMap((kind) => fieldsByKind[kind].map((field) => `${kind}.${field}`));

const orderTable = dumpGrammar.match(/## Field order by kind[\s\S]*$/)?.[0];
if (orderTable === undefined) throw new Error("Unable to locate the dump field-order table");
for (const [, kind, cell] of orderTable.matchAll(/^\| `([a-z]+)` \| ([^|]+) \|/gm)) {
    const ordered = [...cell.matchAll(/`([A-Za-z]+)`/g)].map((match) => match[1]);
    if (!sameArray(ordered, dumpFieldsByKind[kind] ?? [])) {
        failures.push(`dump field order for ${kind} drifted from the contract inventory`);
    }
}

const modes = ["document", "mathInline", "mathDisplay"];
const measure = /^-?(0|[1-9]\d*)\.\d+pt$/;
const rangeValue = /^(0|[1-9]\d*)\.\.(0|[1-9]\d*)$/;
const valueChecks = {
    x: measure,
    y: measure,
    width: measure,
    ascent: measure,
    descent: measure,
    italic: measure,
    size: measure,
    cp: /^U\+[0-9A-F]{4,6}$/,
    style: /^(upright|italic)$/,
    family: /^(main|size[1-4])$/,
    src: rangeValue
};

const glyphMeasure = (tree, field) =>
    [...tree.matchAll(new RegExp(`^ +glyph .* ${field}=(-?[\\d.]+)pt `, "gm"))].map((match) => Number(match[1]));
const kernLines = (tree) => [...tree.matchAll(/^ +kern /gm)].length;

// The bin-demotion states are witnessed in the expected tree, not just in the
// source: each validator locates the demoted glyph under the source pattern
// and checks the neighboring glyph/kern sequence for the spacing that only
// the demoted (Ord) row produces — a fixture whose tree kept the Bin spacing
// cannot certify demotion. The witnesses require the fixture to place an
// ordinary atom on the distinguishing side of the demoted glyph.
const encoder = new TextEncoder();
const THIN = "1.66672pt";
const MEDIUM = "2.22229pt";
const THICK = "2.77771pt";
const binCp = set([0x2b, 0x2212, 0x2217]);
const relCp = set([0x3a, 0x3c, 0x3d, 0x3e]);
const openCp = set([0x28, 0x5b]);
const closeCp = set([0x29, 0x5d]);
const punctCp = set([0x2c, 0x3b]);
const ordinary = (child) =>
    child?.kind === "glyph" &&
    ((child.cp >= 0x30 && child.cp <= 0x39) ||
        (child.cp >= 0x41 && child.cp <= 0x5a) ||
        (child.cp >= 0x61 && child.cp <= 0x7a));
const glyphIn = (child, cps) => child?.kind === "glyph" && cps.has(child.cp);
const kernOf = (child, width) => child?.kind === "kern" && child.width === width;
const rootChildren = (tree) =>
    [...tree.matchAll(/^ {2}(glyph|kern)((?: [a-z]+=\S+)*)$/gm)].map(([, kind, fields]) => {
        const field = (name) => fields.match(new RegExp(` ${name}=(\\S+)`))?.[1];
        const [begin, end] = (field("src") ?? "0..0").split("..").map(Number);
        return { kind, cp: Number.parseInt(field("cp")?.slice(2) ?? "0", 16), width: field("width"), begin, end };
    });
const demoted = (subject, pattern, offset, witness) => {
    const { mode, outcome, source, tree } = subject;
    if (mode === "document" || outcome !== "tree" || source === null) return false;
    const children = rootChildren(tree);
    return [...source.matchAll(pattern)].some((match) => {
        const byte = encoder.encode(source.slice(0, match.index + offset(match))).length;
        const index = children.findIndex((child) => child.kind === "glyph" && child.begin <= byte && byte < child.end);
        return index !== -1 && glyphIn(children[index], binCp) && witness(children, index);
    });
};
const head = () => 0;
const tail = (match) => match[0].length - 1;

// The script/group states are likewise witnessed in the expected tree.
// `nodes` parses every dump line with its depth; `childrenOf` recovers a
// box's direct children from the depth sequence. Printed decimals are
// exact reads of scaled values, so sums compare within 1e-9.
const nodes = (tree) =>
    [...tree.matchAll(/^((?: {2})+)(hbox|glyph|kern|rule)((?: [a-z]+=\S+)*)$/gm)].map(([, pad, kind, fields]) => {
        const field = (name) => fields.match(new RegExp(` ${name}=(\\S+)`))?.[1];
        const pt = (name) => {
            const value = field(name);
            return value === undefined ? undefined : Number(value.slice(0, -2));
        };
        return {
            depth: pad.length / 2,
            kind,
            cp: field("cp") === undefined ? undefined : Number.parseInt(field("cp").slice(2), 16),
            family: field("family"),
            x: pt("x"),
            y: pt("y"),
            width: pt("width"),
            ascent: pt("ascent"),
            italic: pt("italic"),
            size: pt("size")
        };
    });
const childrenOf = (all, index) => {
    const children = [];
    for (let next = index + 1; next < all.length && all[next].depth > all[index].depth; next += 1) {
        if (all[next].depth === all[index].depth + 1) children.push(all[next]);
    }
    return children;
};
const near = (left, right) => Math.abs(left - right) < 1e-9;
const scriptBox = (all) =>
    all.map((node, index) => ({ node, index })).filter(({ node }) => node.kind === "hbox" && node.y !== 0);
const treeStates = (validate) => (subject) => subject.outcome === "tree" && validate(nodes(subject.tree), subject);

// A fraction box is witnessed structurally: an hbox whose direct children
// carry a rule with one box shifted above it and one below.
const fractionBoxes = (all) =>
    all
        .map((node, index) => ({ node, index }))
        .filter(({ node, index }) => {
            if (node.kind !== "hbox") return false;
            const children = childrenOf(all, index);
            return (
                children.some((child) => child.kind === "rule") &&
                children.some((child) => child.kind === "hbox" && child.y > 0) &&
                children.some((child) => child.kind === "hbox" && child.y < 0)
            );
        });
const fractionParts = (all, index) => {
    const children = childrenOf(all, index);
    return {
        num: children.find((child) => child.kind === "hbox" && child.y > 0),
        den: children.find((child) => child.kind === "hbox" && child.y < 0)
    };
};

// Validators run over the whole case: `source` is the decoded input (null
// for the invalid-UTF-8 case), `tree` the expected file text, `mode` and
// `outcome` the manifest values. A declared state must validate; every
// state must be demonstrated somewhere in the corpus.
const stateValidators = {
    "mode.document": ({ mode }) => mode === "document",
    "mode.mathInline": ({ mode }) => mode === "mathInline",
    "mode.mathDisplay": ({ mode }) => mode === "mathDisplay",
    "outcome.tree": ({ outcome }) => outcome === "tree",
    "outcome.error": ({ outcome }) => outcome === "error",
    "children.none": ({ outcome, tree }) => outcome === "tree" && tree.split("\n").length === 3,
    "children.populated": ({ tree }) => /^ {2}\S/m.test(tree),
    "atom.upper": ({ tree }) => / cp=U\+00(4[1-9A-F]|5[0-9A]) /.test(tree),
    "atom.lower": ({ tree }) => / cp=U\+00(6[1-9A-F]|7[0-9A]) /.test(tree),
    "atom.digit": ({ tree }) => / cp=U\+003[0-9] /.test(tree),
    "atom.period": ({ tree }) => / cp=U\+002E /.test(tree),
    "atom.comma": ({ tree }) => / cp=U\+002C /.test(tree),
    "atom.bin": ({ mode, source, tree }) =>
        mode !== "document" && /[+*-]/.test(source ?? "") && / cp=U\+(002B|2212|2217) /.test(tree),
    "atom.rel": ({ mode, source, tree }) =>
        mode !== "document" && /[=<>:]/.test(source ?? "") && / cp=U\+003[ACDE] /.test(tree),
    "atom.open": ({ mode, source, tree }) =>
        mode !== "document" && /[([]/.test(source ?? "") && / cp=U\+00(28|5B) /.test(tree),
    "atom.close": ({ mode, source, tree }) =>
        mode !== "document" && /[)\]!?]/.test(source ?? "") && / cp=U\+00(29|5D|21|3F) /.test(tree),
    "atom.punct": ({ mode, source, tree }) =>
        mode !== "document" && /[,;]/.test(source ?? "") && / cp=U\+00(2C|3B) /.test(tree),
    "atom.remapped": ({ mode, source, tree }) =>
        mode !== "document" && /[-*|]/.test(source ?? "") && / cp=U\+2(212|217|223) /.test(tree),
    "glyph.style.upright": ({ tree }) => / style=upright /.test(tree),
    "glyph.style.italic": ({ tree }) => / style=italic /.test(tree),
    "glyph.italic.zero": ({ tree }) => glyphMeasure(tree, "italic").some((value) => value === 0),
    "glyph.italic.positive": ({ tree }) => glyphMeasure(tree, "italic").some((value) => value > 0),
    "glyph.descent.positive": ({ tree }) => glyphMeasure(tree, "descent").some((value) => value > 0),
    "blank.word": ({ source, mode, outcome, tree }) =>
        outcome === "tree" && mode === "document" && /[ \t\n]/.test(source ?? "") && kernLines(tree) > 0,
    "blank.collapsed": ({ source, outcome }) => outcome === "tree" && /[ \t\n]{2,}/.test(source ?? ""),
    "blank.ignoredInMath": ({ source, mode, outcome, tree }) =>
        outcome === "tree" && mode !== "document" && /[ \t\n]/.test(source ?? "") && kernLines(tree) === 0,
    "blank.discardedAfterControlWord": ({ source, outcome }) =>
        outcome === "tree" && /\\[A-Za-z]+[ \t\n]/.test(source ?? ""),
    "command.controlSpace": ({ source }) => /\\ /.test(source ?? ""),
    "command.thin": ({ source }) => /\\,/.test(source ?? ""),
    "command.medium": ({ source }) => /\\:/.test(source ?? ""),
    "command.thick": ({ source }) => /\\;/.test(source ?? ""),
    "command.negativeThin": ({ source }) => /\\!/.test(source ?? ""),
    "command.quad": ({ source }) => /\\quad/.test(source ?? ""),
    "command.qquad": ({ source }) => /\\qquad/.test(source ?? ""),
    "command.symbol.greekLower": ({ tree }) => / cp=U\+03(B[1-9A-F]|C[0-9]|D[156]|F[15]) style=italic /.test(tree),
    "command.symbol.greekUpper": ({ tree }) => / cp=U\+03(9[348BE]|A[035689]) style=upright /.test(tree),
    "command.symbol.ordinary": ({ source }) =>
        /\\(infty|partial|ell|wp|Re|Im|aleph|forall|exists|neg|lnot|emptyset|nabla|surd|top|bot|angle|triangle|prime|flat|natural|sharp|clubsuit|diamondsuit|heartsuit|spadesuit|backslash)/.test(
            source ?? ""
        ),
    "command.symbol.bin": ({ source }) =>
        /\\(pm|mp|times|div|cdot|ast|star|circ|bullet|cap|cup|sqcap|sqcup|uplus|vee|lor|wedge|land|setminus|wr|diamond|oplus|ominus|otimes|oslash|odot|dagger|ddagger|amalg)/.test(
            source ?? ""
        ),
    "command.symbol.rel": ({ source }) =>
        /\\(leq?|geq?|equiv|prec|succ|ll|gg|subset|supset|subseteq|supseteq|in|ni|owns|vdash|dashv|smile|frown|mid|parallel|perp|models|asymp|sim|simeq|approx|cong|doteq|propto|bowtie)/.test(
            source ?? ""
        ),
    "command.symbol.arrow": ({ source }) =>
        /\\(to\b|gets|mapsto|leftarrow|rightarrow|Leftarrow|Rightarrow|leftrightarrow|Longrightarrow|uparrow|downarrow|nearrow|searrow|swarrow|nwarrow|harpoon)/.test(
            source ?? ""
        ),
    "command.symbol.delimiter": ({ source }) =>
        /\\(lbrace|rbrace|langle|rangle|lfloor|rfloor|lceil|rceil|[{}|])/.test(source ?? ""),
    "command.symbol.alias": ({ source }) => /\\(le|ge|to|gets|lnot|land|lor|owns)\b/.test(source ?? ""),
    "space.interAtom.none": ({ mode, outcome, tree }) =>
        mode !== "document" && outcome === "tree" && /^ {2}glyph .*\n {2}glyph /m.test(tree),
    "space.interAtom.thin": ({ mode, source, tree }) =>
        mode !== "document" && !/\\,/.test(source ?? "") && / width=1\.66672pt /.test(tree),
    "space.interAtom.medium": ({ mode, source, tree }) =>
        mode !== "document" && !/\\:/.test(source ?? "") && / width=2\.22229pt /.test(tree),
    "space.interAtom.thick": ({ mode, source, tree }) =>
        mode !== "document" && !/\\;/.test(source ?? "") && / width=2\.77771pt /.test(tree),
    "space.interAtom.acrossExplicit": ({ mode, tree }) => mode !== "document" && /^ {2}kern .*\n {2}kern /m.test(tree),
    "kern.boundaryRange": ({ tree }) =>
        [...tree.matchAll(/^ {2}kern .* src=(\d+)\.\.(\d+)$/gm)].some((match) => match[1] === match[2]),
    "bin.kept": ({ mode, source, tree }) =>
        mode !== "document" && /[+*]|\\pm|\\times|\\cdot/.test(source ?? "") && / width=2\.22229pt /.test(tree),
    "bin.demoted.leading": (subject) =>
        demoted(subject, /^[+*-]/g, head, (children, index) => index === 0 && ordinary(children[1])),
    "bin.demoted.trailing": (subject) =>
        demoted(
            subject,
            /[+*-]\n?$/g,
            head,
            (children, index) => index === children.length - 1 && ordinary(children[index - 1])
        ),
    "bin.demoted.afterBin": (subject) =>
        demoted(
            subject,
            /[+*][+*-]/g,
            tail,
            (children, index) =>
                kernOf(children[index - 1], MEDIUM) &&
                glyphIn(children[index - 2], binCp) &&
                ordinary(children[index + 1])
        ),
    "bin.demoted.afterRel": (subject) =>
        demoted(
            subject,
            /[=<>:][+*-]/g,
            tail,
            (children, index) => kernOf(children[index - 1], THICK) && glyphIn(children[index - 2], relCp)
        ),
    "bin.demoted.afterOpen": (subject) =>
        demoted(
            subject,
            /[([][+*-]/g,
            tail,
            (children, index) => glyphIn(children[index - 1], openCp) && ordinary(children[index + 1])
        ),
    "bin.demoted.afterPunct": (subject) =>
        demoted(
            subject,
            /[,;] ?[+*-]/g,
            tail,
            (children, index) => kernOf(children[index - 1], THIN) && glyphIn(children[index - 2], punctCp)
        ),
    "bin.demoted.beforeRel": (subject) =>
        demoted(
            subject,
            /[+*-][=<>:]/g,
            head,
            (children, index) =>
                ordinary(children[index - 1]) &&
                kernOf(children[index + 1], THICK) &&
                glyphIn(children[index + 2], relCp)
        ),
    "bin.demoted.beforeClose": (subject) =>
        demoted(
            subject,
            /[+*-][)\]]/g,
            head,
            (children, index) => ordinary(children[index - 1]) && glyphIn(children[index + 1], closeCp)
        ),
    "bin.demoted.beforePunct": (subject) =>
        demoted(
            subject,
            /[+*-][,;]/g,
            head,
            (children, index) => ordinary(children[index - 1]) && glyphIn(children[index + 1], punctCp)
        ),
    "measure.zero": ({ tree }) => /=0\.0pt/.test(tree),
    "measure.negative": ({ tree }) => /=-/.test(tree),
    "input.empty": ({ bytes }) => bytes !== undefined && bytes.length === 0,
    "input.noFinalNewline": ({ bytes }) => bytes !== undefined && bytes.length > 0 && bytes.at(-1) !== 0x0a,
    "script.sup": treeStates((all) => all.some((node) => node.kind === "hbox" && node.y > 0)),
    "script.sub": treeStates((all) => all.some((node) => node.kind === "hbox" && node.y < 0)),
    "script.both": treeStates(
        (all) =>
            all.some((node) => node.kind === "hbox" && node.y > 0) &&
            all.some((node) => node.kind === "hbox" && node.y < 0)
    ),
    "script.subFirst": treeStates((all) =>
        all.some((node, index) => {
            if (node.kind !== "hbox" || !(node.y < 0)) return false;
            for (let next = index + 1; next < all.length; next += 1) {
                if (all[next].depth < node.depth) return false;
                if (all[next].depth === node.depth) return all[next].kind === "hbox" && all[next].y > 0;
            }
            return false;
        })
    ),
    "script.display": ({ mode, outcome, tree }) =>
        mode === "mathDisplay" && outcome === "tree" && nodes(tree).some((node) => node.kind === "hbox" && node.y > 0),
    "script.group": treeStates((all) =>
        scriptBox(all).some(({ index }) => childrenOf(all, index).filter((child) => child.kind === "glyph").length >= 2)
    ),
    "script.simplified": treeStates((all) =>
        scriptBox(all).some(({ node, index }) => {
            const children = childrenOf(all, index);
            return children.length === 1 && children[0].kind === "glyph" && near(node.width, children[0].width + 0.5);
        })
    ),
    "script.scriptscript": treeStates((all) => all.some((node) => node.kind === "glyph" && node.size === 5)),
    "size.script": treeStates((all) => all.some((node) => node.kind === "glyph" && node.size === 7)),
    "size.scriptscript": treeStates((all) => all.some((node) => node.kind === "glyph" && node.size === 5)),
    "group.nucleus": treeStates((all) =>
        all.some((node, index) => node.kind === "hbox" && node.y === 0 && childrenOf(all, index).length > 0)
    ),
    "group.empty": treeStates((all) =>
        all.some((node, index) => node.kind === "hbox" && childrenOf(all, index).length === 0)
    ),
    "italic.withheld": treeStates((all) =>
        all.some(
            (node, index) =>
                node.kind === "glyph" &&
                node.italic > 0 &&
                all[index + 1]?.kind === "hbox" &&
                all[index + 1].depth === node.depth &&
                all[index + 1].y < 0 &&
                near(all[index + 1].x, node.x + node.width)
        )
    ),
    "italic.offset": treeStates((all) =>
        all.some((node) => {
            if (node.kind !== "glyph" || !(node.italic > 0)) return false;
            return all.some(
                (box) =>
                    box.kind === "hbox" &&
                    box.depth === node.depth &&
                    box.y > 0 &&
                    near(box.x, node.x + node.width + node.italic)
            );
        })
    ),
    "spacing.scriptDropped": treeStates((all) =>
        scriptBox(all).some(({ index }) => {
            const children = childrenOf(all, index);
            return (
                children.some((child) => child.kind === "glyph" && binCp.has(child.cp)) &&
                children.every((child) => child.kind !== "kern")
            );
        })
    ),
    "error.doubleScript": ({ outcome, tree }) =>
        outcome === "error" && / message=double (superscript|subscript)\n$/.test(tree),
    "error.missingScriptArgument": ({ outcome, tree }) =>
        outcome === "error" && / message=missing (superscript|subscript) argument\n$/.test(tree),
    "error.unclosedGroup": ({ outcome, tree }) => outcome === "error" && / message=unclosed group\n$/.test(tree),
    "error.unmatchedBrace": ({ outcome, tree }) =>
        outcome === "error" && / message=unmatched closing brace\n$/.test(tree),
    "error.deepNesting": ({ outcome, tree }) => outcome === "error" && / message=group nesting too deep\n$/.test(tree),
    "error.unsupportedCharacter": ({ outcome, tree }) =>
        outcome === "error" && / message=unsupported character U\+/.test(tree),
    "error.unsupportedCommand": ({ outcome, tree }) =>
        outcome === "error" && / message=unsupported command \\/.test(tree),
    "error.paragraphBreak": ({ outcome, tree }) =>
        outcome === "error" && / message=unsupported paragraph break\n/.test(tree),
    "error.invalidUtf8": ({ outcome, tree }) => outcome === "error" && / status=invalid-utf8 /.test(tree),
    "error.incompleteControl": ({ outcome, tree }) =>
        outcome === "error" && / message=incomplete control sequence/.test(tree),
    "fraction.frac": treeStates(
        (all, { source }) => /\\frac[^a-z]/.test(source ?? "") && fractionBoxes(all).length > 0
    ),
    "fraction.dfrac": treeStates((all, { source }) => /\\dfrac/.test(source ?? "") && fractionBoxes(all).length > 0),
    "fraction.tfrac": treeStates((all, { source }) => /\\tfrac/.test(source ?? "") && fractionBoxes(all).length > 0),
    "fraction.charArgument": treeStates(
        (all, { source }) => /\\[dt]?frac[^{a-z]/.test(source ?? "") && fractionBoxes(all).length > 0
    ),
    "fraction.groupArgument": treeStates(
        (all, { source }) => /\\[dt]?frac\{/.test(source ?? "") && fractionBoxes(all).length > 0
    ),
    "fraction.numeratorWider": treeStates((all) =>
        fractionBoxes(all).some(({ index }) => {
            const { num, den } = fractionParts(all, index);
            return num !== undefined && den !== undefined && num.width > den.width && den.x > num.x;
        })
    ),
    "fraction.denominatorWider": treeStates((all) =>
        fractionBoxes(all).some(({ index }) => {
            const { num, den } = fractionParts(all, index);
            return num !== undefined && den !== undefined && den.width > num.width && num.x > den.x;
        })
    ),
    "fraction.nested": treeStates((all) => {
        const boxes = fractionBoxes(all);
        return boxes.some(({ node, index }) => {
            let end = index + 1;
            while (end < all.length && all[end].depth > node.depth) end += 1;
            return boxes.some((inner) => inner.index > index && inner.index < end);
        });
    }),
    "fraction.inScript": treeStates((all) => fractionBoxes(all).some(({ node }) => node.y !== 0)),
    "fraction.scripted": treeStates((all) =>
        fractionBoxes(all).some(({ node, index }) => {
            for (let next = index + 1; next < all.length; next += 1) {
                if (all[next].depth < node.depth) return false;
                if (all[next].depth === node.depth) return all[next].kind === "hbox" && all[next].y !== 0;
            }
            return false;
        })
    ),
    // The shift witnesses are the exact printed num1/num2 shifts of the
    // 10pt text size (0.677 em and 0.394 em); the script witness is the
    // 7pt rule thickness (0.049 em), which only a script-size fraction
    // produces.
    "fraction.display": treeStates((all) =>
        fractionBoxes(all).some(({ index }) => near(fractionParts(all, index).num?.y ?? 0, 6.77002))
    ),
    "fraction.text": treeStates((all) =>
        fractionBoxes(all).some(({ index }) => near(fractionParts(all, index).num?.y ?? 0, 3.93997))
    ),
    "fraction.script": treeStates((all) => all.some((node) => node.kind === "rule" && near(node.ascent, 0.34297))),
    "kern.nullDelimiter": ({ outcome, tree }) =>
        outcome === "tree" &&
        [...tree.matchAll(/^ +kern .*width=1\.2pt src=(\d+)\.\.(\d+)$/gm)].some((match) => match[1] === match[2]),
    "error.missingNumerator": ({ outcome, tree }) =>
        outcome === "error" && / message=missing numerator argument\n$/.test(tree),
    "error.missingDenominator": ({ outcome, tree }) =>
        outcome === "error" && / message=missing denominator argument\n$/.test(tree),
    "delim.fence": treeStates(
        (all, { source }) => /\\left/.test(source ?? "") && /\\right/.test(source ?? "") && all.length > 1
    ),
    "delim.sizeFace": ({ outcome, tree }) => outcome === "tree" && / family=size[1-4] /.test(tree),
    "delim.assembly": treeStates((all) =>
        all.some((node, index) => {
            if (node.kind !== "hbox") return false;
            const children = childrenOf(all, index);
            return (
                children.length >= 3 &&
                children.every((child) => child.kind === "glyph" && (child.family ?? "").startsWith("size"))
            );
        })
    ),
    // The \left./\right. kern keeps its dot token's non-empty range —
    // unlike the fraction null delimiters, whose ranges are empty.
    "delim.null": ({ outcome, tree }) =>
        outcome === "tree" &&
        [...tree.matchAll(/^ +kern .*width=1\.2pt src=(\d+)\.\.(\d+)$/gm)].some((match) => match[1] !== match[2]),
    "delim.explicit": ({ outcome, source, tree }) =>
        outcome === "tree" && /\\[bB]igg?[lmr]?/.test(source ?? "") && / family=size[1-4] /.test(tree),
    "delim.binom": treeStates(
        (all, { source }) =>
            /\\[dt]?binom/.test(source ?? "") &&
            all.some((node, index) => {
                if (node.kind !== "hbox") return false;
                const children = childrenOf(all, index);
                return (
                    children.some((child) => child.kind === "hbox" && child.y > 0) &&
                    children.some((child) => child.kind === "hbox" && child.y < 0) &&
                    !children.some((child) => child.kind === "rule") &&
                    children.some((child) => child.kind === "glyph")
                );
            })
    ),
    "delim.punctClose": treeStates((all) =>
        all.some((node, index) => {
            if (node.kind !== "hbox") return false;
            const children = childrenOf(all, index);
            return (
                children.length >= 3 &&
                children[children.length - 2]?.kind === "kern" &&
                children[children.length - 1]?.kind === "glyph"
            );
        })
    ),
    "error.missingRight": ({ outcome, tree }) => outcome === "error" && / message=missing \\right\n$/.test(tree),
    "error.unmatchedRight": ({ outcome, tree }) => outcome === "error" && / message=unmatched \\right\n$/.test(tree),
    "error.missingDelimiter": ({ outcome, tree }) => outcome === "error" && / message=missing delimiter\n$/.test(tree),
    "radical.sign": ({ outcome, source, tree }) =>
        outcome === "tree" && /\\sqrt/.test(source ?? "") && / cp=U\+221A /.test(tree) && /^ +rule /m.test(tree),
    "radical.index": ({ outcome, source, tree }) =>
        outcome === "tree" && /\\sqrt\[/.test(source ?? "") && / width=-/.test(tree),
    "error.missingRadical": ({ outcome, tree }) =>
        outcome === "error" && / message=missing radical argument\n$/.test(tree),
    "error.unclosedIndex": ({ outcome, tree }) =>
        outcome === "error" && / message=unclosed radical index\n$/.test(tree),
    "op.big": ({ outcome, tree }) =>
        outcome === "tree" &&
        / cp=U\+(2211|220F|2210|222B|222E|22C[0-3]|2A0[0-6]) style=upright family=size[12] /.test(tree),
    // A limits assembly: a box with no rule, at most one glyph (the
    // operator; function-name operators are boxes), and a shifted limit
    // box — which excludes fraction boxes (rule) and binomials (two
    // parenthesis glyphs).
    "op.limits": treeStates(
        (all, { source }) =>
            /\\(sum|prod|coprod|int|oint|big[a-z]+|lim|limsup|liminf|max|min|sup|inf|det|gcd|Pr)/.test(source ?? "") &&
            all.some((node, index) => {
                if (node.kind !== "hbox") return false;
                const children = childrenOf(all, index);
                if (children.some((child) => child.kind === "rule")) return false;
                const above = children.some((child) => child.kind === "hbox" && child.y > 0);
                const below = children.some((child) => child.kind === "hbox" && child.y < 0);
                const glyphs = children.filter((child) => child.kind === "glyph");
                return (above || below) && glyphs.length <= 1;
            })
    ),
    "op.scripts": treeStates(
        (all, { source }) =>
            /\\(sum|prod|int|oint|sin|log|limsup)/.test(source ?? "") &&
            all.some((node) => {
                if (node.kind !== "glyph" || !(node.family ?? "").startsWith("size")) return false;
                return all.some(
                    (box) =>
                        box.kind === "hbox" && box.depth === node.depth && box.y !== 0 && box.x >= node.x + node.width
                );
            })
    ),
    "op.functionName": treeStates(
        (all, { source }) =>
            /\\(sin|cos|log|ln|lim|limsup|max|Pr)/.test(source ?? "") &&
            all.some((node, index) => {
                if (node.kind !== "hbox") return false;
                const glyphs = childrenOf(all, index).filter((child) => child.kind === "glyph");
                return (
                    glyphs.length >= 2 &&
                    glyphs.every((glyph) => glyph.family === "main") &&
                    new Set(glyphs.map((glyph) => `${glyph.cp}`)).size >= 2
                );
            })
    ),
    "error.misplacedLimits": ({ outcome, tree }) =>
        outcome === "error" && / message=misplaced \\(no)?limits\n$/.test(tree),
    // An accent box: exactly one accent-codepoint glyph over one nucleus
    // box, the box keeping the nucleus width.
    "accent.over": treeStates(
        (all, { source }) =>
            /\\(hat|check|tilde|acute|grave|dot|ddot|breve|bar|vec)/.test(source ?? "") &&
            all.some((node, index) => {
                if (node.kind !== "hbox") return false;
                const children = childrenOf(all, index);
                return (
                    children.length === 2 &&
                    children[0].kind === "glyph" &&
                    [0x2c6, 0x2c7, 0x2c9, 0x2ca, 0x2cb, 0x2d8, 0x2d9, 0x2dc, 0xa8, 0x20d7].includes(children[0].cp) &&
                    children[1].kind === "hbox" &&
                    near(node.width, children[1].width)
                );
            })
    ),
    "accent.wide": ({ outcome, source, tree }) =>
        outcome === "tree" &&
        /\\wide(hat|tilde)/.test(source ?? "") &&
        / cp=U\+02(C6|DC) style=upright family=size[1-4] /.test(tree),
    "accent.raised": treeStates((all) =>
        all.some(
            (node, index) =>
                node.kind === "hbox" &&
                childrenOf(all, index).some(
                    (child) => child.kind === "glyph" && child.y > 0 && [0x2c6, 0x20d7].includes(child.cp)
                )
        )
    ),
    "error.missingAccent": ({ outcome, tree }) =>
        outcome === "error" && / message=missing accent argument\n$/.test(tree)
};
const orderValidators = {
    "root.hbox": ({ outcome, tree }) =>
        outcome === "tree" && new RegExp(`^render-tree ${manifest.schemaVersion}\\nhbox `).test(tree),
    "children.sourceOrder": ({ outcome, tree }) => {
        if (outcome !== "tree") return false;
        const begins = [...tree.matchAll(/^ {2}\S.* src=(\d+)\.\./gm)].map((match) => Number(match[1]));
        return begins.length >= 2 && begins.every((begin, index) => index === 0 || begins[index - 1] <= begin);
    }
};

if (manifest.schemaVersion !== 4) failures.push("manifest schemaVersion must be 4");
if (manifest.contract !== "docs/specs/render-tree.md" || manifest.dumpGrammar !== "docs/specs/render-tree-dump.md") {
    failures.push("manifest contract paths drifted from the repository specifications");
}
if (
    manifest.format?.encoding !== "UTF-8" ||
    manifest.format?.lineEndings !== "LF" ||
    manifest.format?.finalNewline !== true ||
    manifest.format?.caseOrder !== "manifest"
) {
    failures.push("manifest must freeze UTF-8, LF, one final newline, and manifest case order");
}
if (!sameArray(manifest.coverageRequirements?.kinds ?? [], canonicalKinds)) {
    failures.push("manifest kind inventory must exactly match the contract inventory order");
}
if (!sameArray(manifest.coverageRequirements?.states ?? [], Object.keys(stateValidators))) {
    failures.push("manifest state vocabulary must exactly match the fail-closed audit vocabulary");
}
if (!sameArray(manifest.coverageRequirements?.orders ?? [], Object.keys(orderValidators))) {
    failures.push("manifest order vocabulary must exactly match the fail-closed audit vocabulary");
}
if (!Array.isArray(manifest.cases) || manifest.cases.length === 0) {
    failures.push("manifest cases must be a non-empty array");
}

const allowedEntries = new Set(["README.md", "manifest.json"]);
const names = new Set();
const allCoveredKinds = new Set();
const allCoveredStates = new Set();
const allCoveredOrders = new Set();
const allObservedFields = new Set();

for (const testCase of manifest.cases ?? []) {
    const label = typeof testCase.name === "string" ? testCase.name : "<unnamed>";
    if (!/^[a-z][a-z0-9-]*$/.test(label) || names.has(label)) {
        failures.push(`invalid or duplicate case name: ${label}`);
    }
    names.add(label);
    if (testCase.input !== `${label}.tex` || testCase.expected !== `${label}.tree`) {
        failures.push(`${label} paths must be ${label}.tex and ${label}.tree`);
    }
    allowedEntries.add(testCase.input);
    allowedEntries.add(testCase.expected);

    const mode = testCase.compileOptions?.mode;
    if (!sameArray(Object.keys(testCase.compileOptions ?? {}), ["mode"]) || !modes.includes(mode)) {
        failures.push(`${label} compileOptions must freeze exactly one mode of ${modes.join("|")}`);
    }
    const outcome = testCase.outcome;
    if (outcome !== "tree" && outcome !== "error") {
        failures.push(`${label} outcome must be tree or error`);
    }

    const declaredStates = testCase.coverage?.states ?? [];
    let bytes;
    let tree;
    try {
        bytes = await readFile(path.join(specPath, testCase.input));
    } catch (error) {
        failures.push(`${testCase.input} is missing: ${error.message}`);
    }
    try {
        const expectedBytes = await readFile(path.join(specPath, testCase.expected));
        tree = decoder.decode(expectedBytes);
    } catch (error) {
        failures.push(`${testCase.expected} is missing or is not valid UTF-8: ${error.message}`);
    }
    if (bytes === undefined || tree === undefined) continue;

    // Inputs are byte-exact compile input. The declared coverage names the
    // documented exceptions: invalid UTF-8 stays undecoded, and only the
    // empty/no-final-newline cases may omit the final LF.
    let source = null;
    if (declaredStates.includes("error.invalidUtf8")) {
        let decodes = true;
        try {
            decoder.decode(bytes);
        } catch {
            decodes = false;
        }
        if (decodes) failures.push(`${testCase.input} covers error.invalidUtf8 but is valid UTF-8`);
    } else {
        try {
            source = decoder.decode(bytes);
        } catch (error) {
            failures.push(`${testCase.input} is not valid UTF-8: ${error.message}`);
            continue;
        }
        if (source.includes("\r")) failures.push(`${testCase.input} must not contain CR`);
        const exemptFromFinalLF =
            declaredStates.includes("input.empty") || declaredStates.includes("input.noFinalNewline");
        if (!exemptFromFinalLF && !source.endsWith("\n")) {
            failures.push(`${testCase.input} must end with a final newline`);
        }
    }
    if (!tree.endsWith("\n") || tree.endsWith("\n\n") || tree.includes("\r")) {
        failures.push(`${testCase.expected} must use LF and exactly one final newline`);
    }

    const lines = tree.slice(0, -1).split("\n");
    const actualKinds = new Set();
    if (outcome === "tree") {
        if (lines[0] !== `render-tree ${manifest.schemaVersion}`) {
            failures.push(`${testCase.expected}:1 schema line must be "render-tree ${manifest.schemaVersion}"`);
        }
        let previousDepth = -1;
        for (const [index, line] of lines.slice(1).entries()) {
            const match = line.match(/^((?: {2})*)([a-z]+)((?: [a-z]+=\S+)*)$/);
            const depth = match === null ? -1 : match[1].length / 2;
            if (match === null || (index === 0 ? depth !== 0 : depth < 1 || depth > previousDepth + 1)) {
                failures.push(`${testCase.expected}:${index + 2} does not match the canonical line grammar`);
                continue;
            }
            previousDepth = depth;
            const kind = match[2];
            actualKinds.add(kind);
            for (const field of fieldsByKind[kind] ?? []) allObservedFields.add(`${kind}.${field}`);
            const pairs = [...match[3].matchAll(/ ([a-z]+)=(\S+)/g)];
            if (
                !sameArray(
                    pairs.map((pair) => pair[1]),
                    dumpFieldsByKind[kind] ?? []
                )
            ) {
                failures.push(`${testCase.expected}:${index + 2} fields drift from the ${kind} canonical order`);
            }
            for (const [, name, value] of pairs) {
                if (valueChecks[name] !== undefined && !valueChecks[name].test(value)) {
                    failures.push(`${testCase.expected}:${index + 2} has a malformed ${name} value: ${value}`);
                }
                if (name === "src") {
                    const [begin, end] = value.split("..").map(Number);
                    if (begin > end) failures.push(`${testCase.expected}:${index + 2} src begin exceeds end`);
                }
            }
        }
        const rootSrc = lines[1]?.match(/ src=(\d+)\.\.(\d+)$/);
        if (rootSrc === null || rootSrc?.[1] !== "0" || Number(rootSrc?.[2]) !== bytes.length) {
            failures.push(`${testCase.expected} root must span the whole input, src=0..${bytes.length}`);
        }
    } else {
        if (lines[0] !== `render-error ${manifest.schemaVersion}`) {
            failures.push(`${testCase.expected}:1 schema line must be "render-error ${manifest.schemaVersion}"`);
        }
        if (
            lines.length !== 2 ||
            !/^error status=(invalid-utf8|unsupported) src=((0|[1-9]\d*)\.\.(0|[1-9]\d*)|none) message=[ -~]+$/.test(
                lines[1]
            )
        ) {
            failures.push(`${testCase.expected} does not match the canonical error-record grammar`);
        }
    }

    const declaredKinds = set(testCase.coverage?.kinds ?? []);
    for (const [description, values] of [
        ["missing declared kinds", difference(declaredKinds, actualKinds)],
        ["undeclared kinds", difference(actualKinds, declaredKinds)]
    ]) {
        if (values.length > 0) failures.push(`${label} ${description}: ${values.join(", ")}`);
    }
    for (const kind of declaredKinds) allCoveredKinds.add(kind);

    const subject = { bytes, source, tree, mode, outcome };
    for (const state of declaredStates) {
        allCoveredStates.add(state);
        if (!(state in stateValidators)) failures.push(`${label} declares unknown state: ${state}`);
        else if (!stateValidators[state](subject))
            failures.push(`${label} does not demonstrate declared state: ${state}`);
    }
    for (const order of testCase.coverage?.orders ?? []) {
        allCoveredOrders.add(order);
        if (!(order in orderValidators)) failures.push(`${label} declares unknown order: ${order}`);
        else if (!orderValidators[order](subject))
            failures.push(`${label} does not demonstrate declared order: ${order}`);
    }
}

for (const [description, actual, required] of [
    ["node kind coverage", allCoveredKinds, set(canonicalKinds)],
    ["behavior-bearing field coverage", allObservedFields, set(canonicalFields)],
    ["state coverage", allCoveredStates, set(Object.keys(stateValidators))],
    ["child-order coverage", allCoveredOrders, set(Object.keys(orderValidators))]
]) {
    const missing = difference(required, actual);
    const unknown = difference(actual, required);
    if (missing.length > 0) failures.push(`${description} is missing: ${missing.join(", ")}`);
    if (unknown.length > 0) failures.push(`${description} is undeclared: ${unknown.join(", ")}`);
}

const unexpectedEntries = entries.filter((entry) => !allowedEntries.has(entry));
const missingEntries = difference(allowedEntries, set(entries));
if (unexpectedEntries.length > 0) failures.push(`unmanifested spec entries: ${unexpectedEntries.sort().join(", ")}`);
if (missingEntries.length > 0) failures.push(`manifested spec entries missing on disk: ${missingEntries.join(", ")}`);

if (failures.length > 0) throw new Error(failures.join("\n"));
process.stdout.write(
    `Render-tree manifest v${manifest.schemaVersion} covers ${canonicalKinds.length} node kinds, ` +
        `${canonicalFields.length} fields, and ${manifest.cases.length} cases.\n`
);
