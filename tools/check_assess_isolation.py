"""Guard the dev assessment marker's isolation (honest_reticle_assess).

The dev key ``honest_reticle_assess`` draws a SECOND marker where the game's shot
lands, beside the player's reticle. It must never REPLACE the reticle (vr.cpp
overwriting aimDir or switching the traced placement off): the player would
then aim WITH the assessment, and every test of the aim would measure the
assessment. The rule this script keeps: the assessment READS the game and the
frame, and writes nothing but its own publication and its own quad. Nothing
but the VR drawing code reads what it publishes -- no mark, shot, aim write,
drive mode, scope or reticle, and no trace but the assessment's own.

No behavioural test compiles game.cpp or vr.cpp, so this checks the source.
Assessment code lives between ``// [assess-only begin]`` and
``// [assess-only end]`` markers. It asserts:

  1. every region is closed, not nested, and lies inside a positive
     ``#if ... HALOMCCVR_DEV_DIAGNOSTICS`` branch (a player build contains none of it);
  2. every assessment symbol (the key, AssessDrawsGameAim,
     Game_GetClampedAimDirection, the g_aimClamped* publication, the assess
     ray/trace, assess_reticle::, the marker's chain and quad, any ``assess*``
     name ...) appears only inside a region -- except the config plumbing
     (config.h/.cpp, menu.cpp) and the pure headers that define it;
  3. Game_GetClampedAimDirection is CALLED only in src/dll/vr.cpp, and the key
     is read in game.cpp only as the first argument of AssessDrawsGameAim;
  4. no region mentions a sink: the convergence mark, the drive, the aim write,
     the shots/muzzle, the player's reticle (aimDir, tracedReticle,
     tracedPlacement, reticleQuad, g_reticle*...), the scope, the servo, the
     intent trace's fault latch, and the PLAYER-FACING seat limit reticle (its
     Sight_*Limit* ray/trace slots, mark and publication), which has slots of its
     own and shares only pure geometry code (shot_point_logic.h) with the
     assessment -- rule 2 keeps every assessment symbol out of it in turn;
  5. inside a region, every assignment, increment, memcpy/memset/std::copy
     destination and mutating call (store, fetch_add, push_back...) targets a
     name DECLARED in a region of that file -- except the listed exemptions
     (the marker's own layers.push_back, the assessment's own witness counters,
     and the fail-closed latch a native fault in the shared collision query
     sets);
  6. a name declared only inside regions is never used outside them (no value
     leaks out of the assessment).

Limits (heuristic, by design): a reference out-parameter of an outside name
passed to a function is not detected, and a name declared both inside and
outside regions is not leak-checked. ``--self-test`` exercises every rule on
synthetic sources first, including injected misuses that must fail.
"""

import argparse
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent

BEGIN = re.compile(r"//\s*\[assess-only begin\]")
END = re.compile(r"//\s*\[assess-only end\]")

# Files that may carry assessment symbols outside regions: the config plumbing, which parses, saves and shows the
# key (it must still be dev-gated in the .cpp files), and the pure headers that define the policy and the geometry.
PLUMBING_DEV_GATED = {"src/common/config.cpp", "src/dll/menu.cpp"}
PLUMBING_FREE = {"src/common/config.h", "src/common/assess_reticle_logic.h", "src/common/converge_policy.h"}
# shot_point_logic.h is NOT here: it is the shared, player-facing geometry, and must carry no assessment symbol.

SOURCES = [
    r"\bhonest_reticle_assess\b",
    r"\bAssessDrawsGameAim\b",
    r"\bGame_GetClampedAimDirection\b",
    r"\bg_aimClamped\w*",
    r"\bkAimClampedFreshMs\b",
    r"\bAimClampedPublication\b",
    r"\bSight_\w*Assess\w*",
    r"\bSightAssess\w*",
    r"\bassess_reticle::",
    r"\bHalo4SeatSightHonestAlongAim\b",
    r"\bg_assess\w*",
    r"\bAssessReticleReport\b",
    r"\bReachAssess\w*",
    r"\bg_reachAssess\w*",
    r"\bPrepareAssessMarkerImage\b",
    r"\bassess(?:[A-Z]\w*)?\b",
    r"\bhalo4SeatAimHonest\w*",
    r"\bhalo4SeatShot\w*",
]
# Common words that are assessment symbols only in one file.
# (`r.clamped[0]` is another struct's member, not the assessment's publication.)
FILE_SOURCES = {"src/dll/game.cpp": [r"(?<![.>])\bclamped\b"]}

SINKS = [
    # The PLAYER-FACING seat limit reticle (every build): its own ray/trace slots, mark and publication. The
    # assessment never shares or touches them.
    r"\bSight_\w*Limit\w*",
    r"\b(?:g_)?[sS]eatLimit\w*",
    r"\bGame_GetSeatLimitReticle\b",
    r"\blimit(?:Dir|Placement|FromEye|FromController|State|RangeM|Surface)\b",
    r"\bSight_Mark\w*",
    # The intent trace's fault latch belongs to the aim: an assessment fault latches only its own
    # (Sight_NoteAssessTraceFault), never the title's trace.
    r"\bSight_NoteTraceFault\b",
    r"\bfaulted\b",
    r"\bSight_Publish(?!Assess)\w*",
    r"\bSight_NoteConverge\w*",
    r"\bSight_NoteMuzzle\w*",
    r"\bSight_AimMuzzle\w*",
    r"\w*Muzzle\w*",
    r"\bLegacyDrive\w*",
    r"\bReach(?:Record|Write|Pc|DirectDrive|Ddp|Publish(?!UnitAim))\w*",
    r"\bReachPublishAimFeedback\b",
    r"\b(?:outRx|outRy|desiredYaw|desiredPitch|errYaw|errPitch|convergeWhy)\b",
    r"\bservoAim\w*",
    r"\bseatedClaim\w*",
    r"\bsight(?:OnFoot|Seated)\w*",
    r"\bConvergeNote\w*",
    r"\bg_aimFwd\w*",
    r"\bg_reachWritten\w*",
    r"\bg_reachSightActuated\b",
    r"\bg_reachAimFeedback\b",
    r"\bg_reticle\w*",
    r"\bg_secondaryReticle\w*",
    r"\bg_dualReticle\w*",
    r"\b(?:aimDir|tracedReticle|tracedPlacement|reticleQuad|secondaryReticleQuad|reticleOrigin)\b",
    r"\bPublishPresentedReticleAimPose\b",
    r"\bg_scope\w*",
    r"\bscopeQuad\b",
    r"\bWriteProcessMemory\b",
    r"\bVirtualProtect\w*",
]

# Exact forms allowed inside regions although they write or call outside the region's own names.
EXEMPT = [
    # The marker's own layer: the compositor's list takes its quad, after the reticle's.
    r"\blayers\.push_back\(\s*reinterpret_cast<XrCompositionLayerBaseHeader\*>\(\s*&assessMarkerQuad\s*\)\s*\)",
    # The assessment's own witness counters in the Halo 4 seat lane (dev lines only).
    r"\bg_h4SeatEyeWitness\.honest\w*",
]

MUTATORS = (r"store|fetch_add|fetch_sub|fetch_or|fetch_and|fetch_xor|exchange|compare_exchange_weak|"
            r"compare_exchange_strong|push_back|emplace_back|insert|erase|clear|assign|resize|reset|swap")
MUTATING_CALL = re.compile(r"\b([A-Za-z_]\w*)((?:\s*(?:\[[^\]\n]*\]|\.\w+|->\w+))*?)\s*(?:\.|->)(?:" + MUTATORS
                           + r")\s*\(")
ASSIGN = re.compile(r"(?<![\w.>])([A-Za-z_]\w*)((?:\s*(?:\[[^\]\n]*\]|\.\w+|->\w+))*)\s*"
                    r"(?:[-+*/%&|^]|<<|>>)?=(?!=)")
INCDEC = re.compile(r"(?:\+\+|--)\s*([A-Za-z_]\w*)|(?<![\w.>])([A-Za-z_]\w*)((?:\s*(?:\[[^\]\n]*\]|\.\w+|->\w+))*)"
                    r"\s*(?:\+\+|--)")
COPY_DEST = re.compile(r"\b(?:memcpy|memset|memmove)\s*\(\s*&?\s*([A-Za-z_]\w*)")
STD_COPY = re.compile(r"\bstd::copy(?:_n)?\s*\(")
TYPE = r"(?:[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*(?:\s*<[^<>;()]*>)?)"
DECL = re.compile(r"(?:(?<=[;{}(,])|(?<=^)|(?<=\s))(?:(?:const|static|constexpr|inline|volatile|thread_local)\s+)*"
                  r"(" + TYPE + r")\s*(?:[&*]\s*)*\s*\b([A-Za-z_]\w*)\s*(?:\[[^\]\n]*\]\s*)*(?=[=;{,)(\[])", re.M)
NOT_TYPES = {"return", "else", "case", "goto", "delete", "new", "throw", "co_return", "co_yield", "using",
             "typedef", "namespace", "sizeof", "do", "if", "while", "for", "switch", "struct", "class", "enum",
             "union", "operator", "template", "typename", "public", "private", "protected", "default"}
BUILTIN_TYPES = {"bool", "char", "short", "int", "long", "float", "double", "unsigned", "signed", "auto", "void",
                 "wchar_t"}
KEYWORDS = NOT_TYPES | {"true", "false", "nullptr", "this", "auto", "const", "static", "float", "int", "bool",
                        "void", "char", "double", "unsigned", "signed", "long", "short", "constexpr", "inline"}
PP = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def blank(match):
    return re.sub(r"[^\n]", " ", match.group(0))


def strip_comments_and_strings(text):
    """Blank comments and string/char literals, keeping every offset and newline."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", text[i:j]))
            i = j
        elif c in "\"'":
            # A raw string R"( ... )" is not used in these files; plain literals only.
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(c + " " * max(0, j - i - 2) + (text[j - 1] if j - i >= 2 else ""))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def line_of(text, offset):
    return text.count("\n", 0, offset) + 1


def dev_polarity(kind, cond):
    """+1: the condition requires the dev build (`#if HALOMCCVR_DEV_DIAGNOSTICS`, `#if A && HALOMCCVR_DEV_DIAGNOSTICS`);
    -1: it requires a player build and nothing else (`#if !HALOMCCVR_DEV_DIAGNOSTICS`), so its #else is dev; 0: neither."""
    cond = re.sub(r"//.*|/\*.*?\*/", "", cond).strip()
    if "HALOMCCVR_DEV_DIAGNOSTICS" not in cond:
        return 0
    if kind == "ifndef":
        return -1 if cond == "HALOMCCVR_DEV_DIAGNOSTICS" else 0
    if kind == "ifdef":
        return 1 if cond == "HALOMCCVR_DEV_DIAGNOSTICS" else 0
    if re.fullmatch(r"!\s*(?:defined\s*\(\s*HALOMCCVR_DEV_DIAGNOSTICS\s*\)|HALOMCCVR_DEV_DIAGNOSTICS)", cond) or \
            re.fullmatch(r"HALOMCCVR_DEV_DIAGNOSTICS\s*==\s*0", cond):
        return -1
    if "||" in cond:
        return 0
    terms = [t.strip() for t in cond.split("&&")]
    positive = re.compile(r"\(?\s*(?:defined\s*\(\s*HALOMCCVR_DEV_DIAGNOSTICS\s*\)|HALOMCCVR_DEV_DIAGNOSTICS)"
                          r"(?:\s*!=\s*0)?\s*\)?")
    return 1 if any(positive.fullmatch(t) for t in terms) else 0


class DevTracker:
    """Follows #if/#elif/#else/#endif and answers: is this line in a branch only a dev build compiles?"""

    def __init__(self):
        self.stack = []   # per open #if: [polarity, in_else]

    def feed(self, line):
        pp = PP.match(line)
        if not pp:
            return
        kind, cond = pp.group(1), pp.group(2)
        if kind in ("if", "ifdef", "ifndef"):
            self.stack.append([dev_polarity(kind, cond), False])
        elif kind == "elif" and self.stack:
            self.stack[-1] = [dev_polarity("if", cond), False]
        elif kind == "else" and self.stack:
            self.stack[-1][1] = True
        elif kind == "endif" and self.stack:
            self.stack.pop()

    def dev(self):
        return any((p == 1 and not e) or (p == -1 and e) for p, e in self.stack)

    def depth(self):
        return len(self.stack)


def scan_regions(path, text):
    """Returns (regions as [(start, end)] char offsets, violations). Checks closure, nesting and the dev gate."""
    violations = []
    regions = []
    tracker = DevTracker()
    open_at = None
    offset = 0
    for number, line in enumerate(text.split("\n"), 1):
        tracker.feed(line)
        if open_at is not None and tracker.depth() < open_at[2]:
            violations.append(f"{path}:{number}: an #endif closes the dev branch of the region opened at line "
                              f"{open_at[1]} before the region ends")
            open_at = (open_at[0], open_at[1], tracker.depth())
        if BEGIN.search(line):
            if open_at is not None:
                violations.append(f"{path}:{number}: a region opens inside the region opened at line {open_at[1]}")
            elif not tracker.dev():
                violations.append(f"{path}:{number}: a region outside #if HALOMCCVR_DEV_DIAGNOSTICS (a player build "
                                  f"would contain it)")
            open_at = (offset, number, tracker.depth())
        elif END.search(line):
            if open_at is None:
                violations.append(f"{path}:{number}: a region end with no region open")
            else:
                regions.append((open_at[0], offset + len(line)))
                open_at = None
        offset += len(line) + 1
    if open_at is not None:
        violations.append(f"{path}:{open_at[1]}: a region is never closed")
    return regions, violations


def in_regions(regions, offset):
    return any(start <= offset < end for start, end in regions)


def dev_mask(text):
    """Per-line flag: inside a branch only a dev build compiles."""
    flags = []
    tracker = DevTracker()
    for line in text.split("\n"):
        tracker.feed(line)
        flags.append(tracker.dev())
    return flags


def statement_names(code, start):
    """The further names a declaration starting at `start` declares: any after a top-level comma (`float a, b;`).
    A function (the name is followed by `(`) declares no more."""
    names = []
    if re.match(r"\s*\(", code[start:]):
        return names
    depth = 0
    i = start
    n = len(code)
    while i < n:
        c = code[i]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            if depth == 0:
                break
            depth -= 1
        elif c == ";" and depth == 0:
            break
        elif c == "," and depth == 0:
            m = re.match(r"\s*[&*]*\s*([A-Za-z_]\w*)\s*(?:\[[^\]\n]*\]\s*)*(?:=|\{|,|;)", code[i + 1:])
            if m:
                names.append(m.group(1))
        i += 1
    return names


def declared_names(code, spans):
    names = set()
    for start, end in spans:
        chunk = code[start:end]
        for m in DECL.finditer(chunk):
            type_name, name = m.group(1), m.group(2)
            if type_name.split("::")[-1].split("<")[0].strip() in NOT_TYPES or name in KEYWORDS:
                continue
            # Only something that looks like a type: a builtin, a qualified or templated name, or a capitalised
            # one. `a && b)` or `x * y,` must not make `b` or `y` the region's own.
            if not (type_name in BUILTIN_TYPES or "::" in type_name or "<" in type_name or
                    re.fullmatch(r"[A-Z]\w*|\w+_t", type_name)):
                continue
            names.add(name)
            names.update(statement_names(chunk, m.end()))
        # Lambda / function parameters: `(T a, T& b, const T c[3])`.
        for params in re.finditer(r"(\w*)\s*\(([^()]*)\)\s*(?:const\s*)?(?:->\s*\w+\s*)?\{", chunk):
            if params.group(1) in ("if", "while", "for", "switch", "catch", "__except"):
                continue
            for part in params.group(2).split(","):
                m = re.fullmatch(r"(?:const\s+)?(" + TYPE + r")\s*[&*]?\s*([A-Za-z_]\w*)\s*(?:\[[^\]]*\]\s*)*"
                                 r"(?:=.*)?", part.strip(), re.S)
                if m and (m.group(1) in BUILTIN_TYPES or "::" in m.group(1) or "<" in m.group(1) or
                          re.fullmatch(r"[A-Z]\w*|\w+_t", m.group(1))):
                    names.add(m.group(2))
    return names


def outside_spans(regions, length):
    spans = []
    cursor = 0
    for start, end in sorted(regions):
        if start > cursor:
            spans.append((cursor, start))
        cursor = max(cursor, end)
    if cursor < length:
        spans.append((cursor, length))
    return spans


def check_files(files):
    """files: {posix relative path: text}. Returns a list of violations."""
    violations = []
    parsed = {}
    for path, text in sorted(files.items()):
        regions, region_violations = scan_regions(path, text)
        violations += region_violations
        parsed[path] = (text, strip_comments_and_strings(text), regions)

    for path, (text, code, regions) in parsed.items():
        sources = SOURCES + FILE_SOURCES.get(path, [])
        dev = dev_mask(text) if path in PLUMBING_DEV_GATED else None
        # Rule 2: assessment symbols only inside regions (plumbing excepted, dev-gated where it is code).
        for pattern in sources:
            for m in re.finditer(pattern, code):
                if path in PLUMBING_FREE:
                    continue
                if path in PLUMBING_DEV_GATED:
                    if not dev[line_of(code, m.start()) - 1]:
                        violations.append(f"{path}:{line_of(code, m.start())}: '{m.group(0)}' outside "
                                          f"#if HALOMCCVR_DEV_DIAGNOSTICS")
                    continue
                if not in_regions(regions, m.start()):
                    violations.append(f"{path}:{line_of(code, m.start())}: '{m.group(0)}' outside an "
                                      f"[assess-only] region")

        # Rule 3: who may call the publication, and how game.cpp reads the key.
        for m in re.finditer(r"\bGame_GetClampedAimDirection\s*\(", code):
            before = code[max(0, m.start() - 12):m.start()]
            is_declaration = re.search(r"\bbool\s*$", before) is not None
            if not is_declaration and path != "src/dll/vr.cpp":
                violations.append(f"{path}:{line_of(code, m.start())}: Game_GetClampedAimDirection is called "
                                  f"outside the VR drawing code")
        if path == "src/dll/game.cpp":
            for m in re.finditer(r"\bhonest_reticle_assess\b", code):
                before = code[max(0, m.start() - 60):m.start()]
                if not re.search(r"\bAssessDrawsGameAim\s*\(\s*g_config\s*\.\s*$", before):
                    violations.append(f"{path}:{line_of(code, m.start())}: game.cpp reads honest_reticle_assess "
                                      f"other than as AssessDrawsGameAim's first argument")

        if not regions:
            continue
        exempt_spans = []
        for pattern in EXEMPT:
            for start, end in regions:
                for m in re.finditer(pattern, code[start:end]):
                    exempt_spans.append((start + m.start(), start + m.end()))

        def exempt(offset):
            return any(s <= offset < e for s, e in exempt_spans)

        # Rule 4: no sinks in a region.
        for start, end in regions:
            chunk = code[start:end]
            for pattern in SINKS:
                for m in re.finditer(pattern, chunk):
                    if not exempt(start + m.start()):
                        violations.append(f"{path}:{line_of(code, start + m.start())}: a region mentions "
                                          f"'{m.group(0)}' (a mark, shot, write, drive, reticle or scope)")

        # Rule 5: writes only to the regions' own names.
        own = declared_names(code, regions)

        def target_ok(name, offset):
            # An assessment-named target (rule 2 keeps every such name inside regions) is the assessment's own.
            return (name in own or name in KEYWORDS or exempt(offset) or
                    any(re.fullmatch(p.replace(r"", ""), name) for p in sources))

        for start, end in regions:
            chunk = code[start:end]
            for m in ASSIGN.finditer(chunk):
                if not target_ok(m.group(1), start + m.start()):
                    violations.append(f"{path}:{line_of(code, start + m.start())}: a region assigns "
                                      f"'{m.group(1)}', which it does not own")
            for m in INCDEC.finditer(chunk):
                name = m.group(1) or m.group(2)
                if not target_ok(name, start + m.start()):
                    violations.append(f"{path}:{line_of(code, start + m.start())}: a region increments "
                                      f"'{name}', which it does not own")
            for m in COPY_DEST.finditer(chunk):
                if not target_ok(m.group(1), start + m.start()):
                    violations.append(f"{path}:{line_of(code, start + m.start())}: a region copies into "
                                      f"'{m.group(1)}', which it does not own")
            for m in STD_COPY.finditer(chunk):
                args = chunk[m.end():].split(";", 1)[0]
                parts = [p.strip() for p in re.split(r",(?![^()]*\))", args)]
                dest = re.match(r"(?:std::begin\(\s*)?&?\s*([A-Za-z_]\w*)", parts[2]) if len(parts) >= 3 else None
                if dest and not target_ok(dest.group(1), start + m.start()):
                    violations.append(f"{path}:{line_of(code, start + m.start())}: a region copies into "
                                      f"'{dest.group(1)}', which it does not own")
            for m in MUTATING_CALL.finditer(chunk):
                if not target_ok(m.group(1), start + m.start()):
                    violations.append(f"{path}:{line_of(code, start + m.start())}: a region mutates "
                                      f"'{m.group(1)}', which it does not own")

        # Rule 6: a name declared only in regions does not leak out of them.
        outside = outside_spans(regions, len(code))
        declared_outside = declared_names(code, outside)
        only_inside = {n for n in own if n not in declared_outside and n not in KEYWORDS and len(n) > 1}
        if only_inside:
            # A member access (`x.clear()`, `t.rangeWu`) or a qualified name is another object's, not the region's.
            leak = re.compile(r"(?<![.>:])\b(" + "|".join(sorted(map(re.escape, only_inside))) + r")\b")
            for s, e in outside:
                for m in leak.finditer(code, s, e):
                    violations.append(f"{path}:{line_of(code, m.start())}: '{m.group(1)}' is declared only "
                                      f"inside [assess-only] regions but used outside them")
    return violations


def load_tree(root):
    files = {}
    for path in sorted((root / "src").rglob("*")):
        if path.suffix in (".cpp", ".h", ".hpp", ".inl") and path.is_file():
            files[path.relative_to(root).as_posix()] = path.read_text(encoding="utf-8-sig", errors="replace")
    return files


# ---- self-test ------------------------------------------------------------------------------------------------
GOOD_VR = """
#include "game.h"
#if HALOMCCVR_DEV_DIAGNOSTICS
// [assess-only begin]
XrSwapchain g_assessMarkerChain = XR_NULL_HANDLE;
bool g_assessMarkerReady = false;
bool PrepareAssessMarkerImage()
{
    if (g_assessMarkerReady) return true;
    g_assessMarkerReady = true;   // "aimDir = 1" in a string is fine
    return true;
}
// [assess-only end]
#endif
void Frame()
{
    float aimDir[3]{};
    bool tracedReticle = true;
    XrCompositionLayerQuad reticleQuad{};
    layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&reticleQuad));
#if HALOMCCVR_DEV_DIAGNOSTICS
    // [assess-only begin]
    XrCompositionLayerQuad assessMarkerQuad{};
    float assessDir[3]{}, assessRangeM = 0.0f;
    if (g_config.honest_reticle_assess && Game_GetClampedAimDirection(assessDir, &assessRangeM))
    {
        const float assessWidth = assess_reticle::MarkerWidth(1.0f);
        assessMarkerQuad.size = {assessWidth, assessWidth};
        assessMarkerQuad.pose.position = {assessDir[0], assessDir[1], assessDir[2]};
        layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&assessMarkerQuad));
    }
    // [assess-only end]
#endif
    aimDir[0] = 1.0f;
}
"""

GOOD_GAME = """
#if HALOMCCVR_DEV_DIAGNOSTICS
// [assess-only begin]
std::atomic<uint32_t> g_aimClampedValid{0};
struct AimClampedPublication { bool valid = false; void set(float a) { valid = a > 0.0f; } };
bool Game_GetClampedAimDirection(float outDir[3], float* outRangeM, bool* outSurface)
{
    if (outRangeM) *outRangeM = 0.0f;
    outDir[0] = 1.0f;
    return g_aimClampedValid.load() != 0;
}
// [assess-only end]
#endif
bool Game_ComputeAimStick(float& outRx, float& outRy)
{
#if HALOMCCVR_DEV_DIAGNOSTICS
    // [assess-only begin]
    AimClampedPublication clamped;
    // [assess-only end]
#endif
    float aimForward[3]{1.0f, 0.0f, 0.0f};
    float desiredYaw = 0.0f;
#if HALOMCCVR_EXPERIMENTAL_X && HALOMCCVR_DEV_DIAGNOSTICS
    // [assess-only begin]
    const auto assessGameAim = [&](bool seatedNow) {
        if (!converge::AssessDrawsGameAim(g_config.honest_reticle_assess, seatedNow))
            return;
        float assessAim[3]{aimForward[0], aimForward[1], aimForward[2]};
        float vrYaw = 0.0f, vrPitch = 0.0f;
        vrYaw += assessAim[0];
        memcpy(assessAim, aimForward, sizeof(assessAim));
        clamped.set(vrYaw + vrPitch);
        g_assessUnplaced.fetch_add(1);
    };
    assessGameAim(false);
    // [assess-only end]
#endif
    outRx = desiredYaw;
    outRy = 0.0f;
    return true;
}
"""

GOOD_PLUMBING = """
struct Config { int honest_reticle_assess = 0; };
"""

GOOD_MENU = """
void Draw()
{
#if HALOMCCVR_DEV_DIAGNOSTICS
    int assess = g_config.honest_reticle_assess;
#endif
}
"""


def good_tree():
    return {
        "src/dll/vr.cpp": GOOD_VR,
        "src/dll/game.cpp": GOOD_GAME,
        "src/common/config.h": GOOD_PLUMBING,
        "src/dll/menu.cpp": GOOD_MENU,
    }


def run_self_test():
    clean = check_files(good_tree())
    require(not clean, "the good synthetic tree must pass:\n  " + "\n  ".join(clean))

    def must_fail(path, old, new, why):
        tree = good_tree()
        require(old in tree[path], f"self-test setup: '{old}' not in {path}")
        tree[path] = tree[path].replace(old, new, 1)
        require(check_files(tree), why)

    # The original fault: the assessment replaced the reticle.
    must_fail("src/dll/vr.cpp", "        assessMarkerQuad.size = {assessWidth, assessWidth};\n",
              "        assessMarkerQuad.size = {assessWidth, assessWidth};\n        aimDir[0] = assessDir[0];\n",
              "the assessment writing the reticle's direction must be rejected")
    must_fail("src/dll/vr.cpp", "        assessMarkerQuad.size = {assessWidth, assessWidth};\n",
              "        assessMarkerQuad.size = {assessWidth, assessWidth};\n        tracedReticle = false;\n",
              "the assessment switching the traced reticle off must be rejected")
    must_fail("src/dll/vr.cpp", "    aimDir[0] = 1.0f;\n",
              "    if (g_config.honest_reticle_assess) aimDir[0] = 1.0f;\n",
              "the key read outside a region must be rejected")
    must_fail("src/dll/vr.cpp", "    aimDir[0] = 1.0f;\n",
              "    aimDir[0] = assessDir[0];\n",
              "an assessment value leaking out of its region must be rejected")
    must_fail("src/dll/vr.cpp", "        layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&assessMarkerQuad));\n",
              "        layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&reticleQuad));\n",
              "the assessment pushing the reticle's layer must be rejected")
    must_fail("src/dll/vr.cpp", "        assessMarkerQuad.size = {assessWidth, assessWidth};\n",
              "        assessMarkerQuad.size = {assessWidth, assessWidth};\n        std::copy(assessDir, assessDir + 3, aimDir);\n",
              "the assessment copying into the reticle's direction must be rejected")
    # The game side: marks, shots, writes, drive.
    must_fail("src/dll/game.cpp", "        clamped.set(vrYaw + vrPitch);\n",
              "        clamped.set(vrYaw + vrPitch);\n        Sight_MarkConverged(title, 1.0f);\n",
              "the assessment placing the convergence mark must be rejected")
    must_fail("src/dll/game.cpp", "        clamped.set(vrYaw + vrPitch);\n",
              "        clamped.set(vrYaw + vrPitch);\n        desiredYaw = vrYaw;\n",
              "the assessment steering the aim must be rejected")
    must_fail("src/dll/game.cpp", "        clamped.set(vrYaw + vrPitch);\n",
              "        clamped.set(vrYaw + vrPitch);\n        aimForward[0] = vrYaw;\n",
              "the assessment writing the servo's feedback must be rejected")
    must_fail("src/dll/game.cpp", "        memcpy(assessAim, aimForward, sizeof(assessAim));\n",
              "        memcpy(aimForward, assessAim, sizeof(assessAim));\n",
              "the assessment copying into the servo's feedback must be rejected")
    must_fail("src/dll/game.cpp", "        g_assessUnplaced.fetch_add(1);\n",
              "        g_assessUnplaced.fetch_add(1);\n        g_reachDesired.store(vrYaw);\n",
              "the assessment storing into an outside atomic must be rejected")
    must_fail("src/dll/game.cpp", "        clamped.set(vrYaw + vrPitch);\n",
              "        clamped.set(vrYaw + vrPitch);\n        ReachAimMuzzleShot(a, b, c, d);\n",
              "the assessment touching the shots must be rejected")
    must_fail("src/dll/game.cpp", "    outRx = desiredYaw;\n",
              "    if (converge::AssessDrawsGameAim(g_config.honest_reticle_assess, false)) outRx = 0.0f;\n",
              "the drive reading the key must be rejected")
    must_fail("src/dll/game.cpp", "    outRx = desiredYaw;\n",
              "    float d[3]; if (Game_GetClampedAimDirection(d, nullptr, nullptr)) outRx = d[0];\n",
              "game.cpp calling the publication must be rejected")
    must_fail("src/dll/game.cpp", "    outRx = desiredYaw;\n",
              "    outRx = desiredYaw + (clamped.valid ? 1.0f : 0.0f);\n",
              "the publication read outside a region must be rejected")
    must_fail("src/dll/game.cpp", "        if (!converge::AssessDrawsGameAim(g_config.honest_reticle_assess, seatedNow))\n",
              "        if (g_config.honest_reticle_assess == 0)\n",
              "game.cpp reading the key other than through AssessDrawsGameAim must be rejected")
    # The assessment's trace fault must latch only its own trace, never the aim's.
    must_fail("src/dll/game.cpp", "        g_assessUnplaced.fetch_add(1);\n",
              "        g_assessUnplaced.fetch_add(1);\n        trace.faulted.store(true, std::memory_order_release);\n",
              "an assessment region latching the intent trace's faulted flag must be rejected")
    must_fail("src/dll/game.cpp", "        g_assessUnplaced.fetch_add(1);\n",
              "        g_assessUnplaced.fetch_add(1);\n        Sight_NoteTraceFault(title);\n",
              "an assessment region calling Sight_NoteTraceFault must be rejected")
    # The seat limit reticle (player-facing, every build) and the assessment never share state.
    must_fail("src/dll/game.cpp", "        g_assessUnplaced.fetch_add(1);\n",
              "        g_assessUnplaced.fetch_add(1);\n        Sight_PublishLimitRay(title, ray);\n",
              "an assessment region writing the seat limit ray's slot must be rejected")
    must_fail("src/dll/game.cpp", "    outRx = desiredYaw;\n",
              "    bool h; float r; if (Sight_ReadAssessTrace(t, 1, 100, h, r)) outRx = r;\n",
              "the seat limit (or any player code) reading the assessment's slot must be rejected")
    must_fail("src/dll/game.cpp", "    outRx = desiredYaw;\n",
              "    if (nowMs - markMs > kAimClampedFreshMs) outRx = 0.0f;\n",
              "player code borrowing the assessment publication's constants must be rejected")
    # The dev gate and the region structure.
    must_fail("src/dll/vr.cpp", "#if HALOMCCVR_DEV_DIAGNOSTICS\n// [assess-only begin]\nXrSwapchain",
              "#if !HALOMCCVR_DEV_DIAGNOSTICS\n// [assess-only begin]\nXrSwapchain",
              "a region in a player build must be rejected")
    must_fail("src/dll/game.cpp", "    // [assess-only end]\n#endif\n    outRx",
              "#endif\n    outRx", "an unclosed region must be rejected")
    must_fail("src/dll/menu.cpp", "#if HALOMCCVR_DEV_DIAGNOSTICS\n    int assess", "#if 1\n    int assess",
              "the menu reading the key in a player build must be rejected")
    tree = good_tree()
    tree["src/dll/scope.cpp"] = "void S() { if (g_config.honest_reticle_assess) {} }\n"
    require(check_files(tree), "another file reading the key must be rejected")
    # A sink named in a comment or a string is not a use.
    tree = good_tree()
    tree["src/dll/vr.cpp"] = tree["src/dll/vr.cpp"].replace(
        "        assessMarkerQuad.size = {assessWidth, assessWidth};\n",
        "        assessMarkerQuad.size = {assessWidth, assessWidth};   // never aimDir = assessDir\n")
    require(not check_files(tree), "a comment naming a sink must not be rejected")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=ROOT, help="repository root (default: this checkout)")
    parser.add_argument("--self-test", action="store_true", help="also exercise the rules on synthetic sources")
    args = parser.parse_args()

    if args.self_test:
        run_self_test()

    files = load_tree(args.root)
    require(files, f"no sources under {args.root / 'src'}")
    violations = check_files(files)
    if violations:
        for violation in violations:
            print(f"FAIL: {violation}", file=sys.stderr)
        return 1
    regions = sum(len(scan_regions(p, t)[0]) for p, t in files.items())
    print(f"assess isolation guard passed: {regions} [assess-only] regions in {len(files)} files; nothing but the "
          f"VR drawing code reads the assessment, and the assessment writes nothing it does not own")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except ValueError as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
