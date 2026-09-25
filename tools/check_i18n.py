"""Checks that every tr("...") string used in src/ has a Russian translation in src/core/I18n.cpp,
and that printf placeholders match between the English key and its translation.

    python tools/check_i18n.py            # exit code 1 if something is missing
"""
import ast
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LIT = r'"(?:[^"\\]|\\.)*"'
SEQ = rf'(?:{LIT}\s*)+'
PLACEHOLDER = re.compile(r"%[-+#0]*\d*(?:\.\d+)?(?:ll|l|h)?[diuoxXfFeEgGcsp]")


def unescape(seq: str) -> str:
    return "".join(ast.literal_eval(m.group(0)) for m in re.finditer(LIT, seq))


def used_keys():
    keys = {}
    call = re.compile(rf"\btr\(\s*({SEQ})[,)]")
    for path in sorted((ROOT / "src").rglob("*.cpp")) + sorted((ROOT / "src").rglob("*.h")):
        if path.name == "I18n.cpp":
            continue
        text = path.read_text(encoding="utf-8")
        for m in call.finditer(text):
            line = text.count("\n", 0, m.start()) + 1
            keys.setdefault(unescape(m.group(1)), f"{path.relative_to(ROOT)}:{line}")
    return keys


def table():
    text = (ROOT / "src" / "core" / "I18n.cpp").read_text(encoding="utf-8")
    body = text[text.index("kRu[]"):]
    pairs = {}
    for m in re.finditer(rf"\{{\s*({SEQ}),\s*({SEQ})\}}", body):
        pairs[unescape(m.group(1))] = unescape(m.group(2))
    return pairs


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    used, ru = used_keys(), table()
    missing = {k: w for k, w in used.items() if k not in ru}
    bad = [k for k, v in ru.items() if sorted(PLACEHOLDER.findall(k)) != sorted(PLACEHOLDER.findall(v))]
    unused = [k for k in ru if k not in used]
    print(f"{len(used)} strings used in code, {len(ru)} translations")
    for k, w in missing.items():
        print(f"MISSING  {w}: {k!r}")
    for k in bad:
        print(f"PLACEHOLDER MISMATCH: {k!r} -> {ru[k]!r}")
    if unused:
        print(f"note: {len(unused)} translation keys are not referenced by a literal tr() call "
              "(key names, profile names and enum names are looked up dynamically)")
    return 1 if missing or bad else 0


if __name__ == "__main__":
    sys.exit(main())
