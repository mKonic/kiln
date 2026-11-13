#!/usr/bin/env python3
"""Writes a new app's folder for `tools/kiln new`: kiln-new.py DIR NAME PACKAGE LABEL."""
import os
import struct
import sys
import zlib

CMAKE = '''# {label}: built by kiln (tools/kiln build|apk|run {name}), on Android or in a desktop window.
kiln_add_app({name}
    SOURCES src/main.cpp
    LIBS kiln_material3 kiln_navigation
    PACKAGE {package}
    LABEL "{label}"
    ASSETS assets
    ICON res/icon.png
    VERSION_CODE 1
    VERSION_NAME 1.0
    # PERMISSIONS android.permission.CAMERA
    # APPLICATION_XML android/application.xml   (extra elements inside <application>)
    # MANIFEST_XML android/manifest.xml         (extra elements inside <manifest>)
)
'''

MAIN = '''#include <kiln/kiln.hpp>
#include <kiln/foundation/Compose.h>
#include <kiln/foundation/Layouts.h>
#include <kiln/foundation/Lazy.h>
#include <kiln/material3/AppBar.h>
#include <kiln/material3/Button.h>
#include <kiln/material3/MaterialTheme.h>
#include <kiln/material3/Scaffold.h>
#include <kiln/material3/Switch.h>
#include <kiln/material3/Text.h>

#include <string>

using namespace kiln::foundation;
using namespace kiln::material3;
using namespace kiln::ui;

namespace {{
    void Counter() {{
        auto& count = RememberState(0);
        auto& fast = RememberState(false);
        ScaffoldOptions options;
        options.topBar = [] {{ TopAppBar([] {{ Text("{label}"); }}, {{}}); }};
        Scaffold({{}}, options, [&](const PaddingValues& padding) {{
            Column(Padding(Padding(FillMax(Modifier{{}}, Axis::Both), padding), 16_dp),
                   Arrangement::SpacedBy(16_dp), AlignmentH::Start(), [&] {{
                Text("Pressed " + std::to_string(count.Get()) + " times",
                     {{}}, {{ .style = MaterialTheme::typography().headlineMedium }});
                Row(Modifier{{}}, Arrangement::SpacedBy(8_dp), AlignmentV::CenterVertically(), [&] {{
                    Switch(fast.Get(), [&](bool on) {{ fast.Set(on); }});
                    Text("Count by ten");
                }});
                Button([&] {{ count.Set(count.Get() + (fast.Get() ? 10 : 1)); }}, {{}}, {{}}, [] {{ Text("Add"); }});
                auto& list = RememberLazyListState();
                LazyColumn(Weight(Modifier{{}}, 1), list, [&](LazyListScope& s) {{
                    s.Items(count.Get(), [](int i) {{ Text("Row " + std::to_string(i)); }});
                }});
            }});
        }});
    }}
}}

extern "C" void kiln_main(kiln_app* app) {{
    kiln::SetContent(app, [] {{ MaterialTheme([] {{ Counter(); }}); }});
}}
'''


def icon_png(size=192):
    """A launcher icon: a purple circle on transparent, anti-aliased (stdlib only)."""
    rows = []
    c, r = (size - 1) / 2, size * 0.46
    for y in range(size):
        row = bytearray([0])
        for x in range(size):
            d = ((x - c) ** 2 + (y - c) ** 2) ** 0.5
            a = max(0.0, min(1.0, r - d + 0.5))
            row += bytes((0x67, 0x50, 0xA4, int(a * 255)))
        rows.append(bytes(row))
    raw = b"".join(rows)

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main():
    out, name, package, label = sys.argv[1:5]
    for sub in ("src", "assets", "res"):
        os.makedirs(os.path.join(out, sub), exist_ok=True)
    with open(os.path.join(out, "CMakeLists.txt"), "w") as f:
        f.write(CMAKE.format(name=name, package=package, label=label))
    with open(os.path.join(out, "src", "main.cpp"), "w") as f:
        f.write(MAIN.format(label=label.replace('"', '\\"')))
    with open(os.path.join(out, "assets", ".keep"), "w"):
        pass
    with open(os.path.join(out, "res", "icon.png"), "wb") as f:
        f.write(icon_png())
    with open(os.path.join(out, ".gitignore"), "w") as f:
        f.write("build/\n")


if __name__ == "__main__":
    main()
