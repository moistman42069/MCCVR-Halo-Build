"""Read-only proof of the H3 selected HUD record binding. Requires pefile."""
import pathlib, re, struct, sys
import pefile
header=(pathlib.Path(__file__).resolve().parents[2]/'src/common/halo3_live_hud_layout.h').read_text()
pe=pefile.PE(sys.argv[1]); data=pe.get_memory_mapped_image()
def unique(name):
    expr=re.search(r'\b'+name+r'\s*=\s*"([^"]+)"',header)[1]
    pattern=b''.join(b'.' if b=='??' else re.escape(bytes([int(b,16)])) for b in expr.split())
    hits=[m.start() for m in re.finditer(pattern,data,re.DOTALL)]
    assert len(hits)==1,(name,hits)
    print(name,hex(hits[0]));return hits[0]
selector=unique('kSelector');tail=unique('kSelectorTail');assert selector<tail<selector+0x100
caller=unique('kCaller');consumer=unique('kSafeFrameConsumer')
assert caller+5+struct.unpack_from('<i',data,caller+1)[0]==selector
assert caller<consumer<caller+0x1000
# Getter bounds/stride, and frame consumer layout must match this exact pinned build.
assert bytes.fromhex('48 6b c0 64 48 03 c3') in data[selector:selector+0xbc]
print('Verified unique selector, native frame caller, safe-frame consumer and 0x64 record stride')
