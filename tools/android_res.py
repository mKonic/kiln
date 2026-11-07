"""Reading Android string resources for kiln's string table generators."""
import os, xml.etree.ElementTree as ET

# Locale.toLanguageTag gives the modern codes; resource folders keep the old ones.
LEGACY = {"iw": "he", "in": "id", "ji": "yi"}


def text_of(el):
    """A <string>'s value as Resources.getString returns it (quotes and escapes resolved)."""
    s = "".join(el.itertext()).strip()
    if len(s) >= 2 and s[0] == '"' and s[-1] == '"':
        s = s[1:-1]
    return s.replace("\\'", "'").replace('\\"', '"').replace("\\n", "\n")


def tag_of(folder):
    """values-<qualifiers> to a BCP 47 tag ('' for the default); None for non-locale folders."""
    q = folder[len("values"):].lstrip("-")
    if not q:
        return ""
    if q.startswith("b+"):
        parts = q[2:].split("+")
    else:
        parts = q.split("-")
        if not parts[0].isalpha() or len(parts[0]) not in (2, 3):
            return None         # values-v34 and the like
        parts = [parts[0]] + [p[1:] if p.startswith("r") and len(p) == 3 else p for p in parts[1:]]
    parts[0] = LEGACY.get(parts[0], parts[0])
    return "-".join(parts)


def read_tables(res_root):
    """{tag: {name: value}} for every values*/strings.xml under res_root."""
    tables = {}
    for folder in sorted(os.listdir(res_root)):
        if not folder.startswith("values"):
            continue
        path = os.path.join(res_root, folder, "strings.xml")
        if not os.path.exists(path):
            continue
        tag = tag_of(folder)
        if tag is None:
            continue
        values = tables.setdefault(tag, {})
        for el in ET.parse(path).getroot().iter("string"):
            values[el.get("name")] = text_of(el)
    return tables


def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'
