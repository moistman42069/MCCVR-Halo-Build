"""Run the existing pinned CE native camera emulator through the DLSS decoder.

No process is opened and no native game code is loaded into Windows. Actual
camera and constant-writer instructions run only inside the bounded emulator.
"""
from pathlib import Path
import sys, struct, subprocess, tempfile, json
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'out/pydeps'))
from verify_ce_native_camera_math import NativeCamera, CAMERA, SHADER_DATA, rotate, cross

native=NativeCamera(ROOT/'out/deps/re-tools/inputs/halo1.dll')
exe=ROOT/'out/build/release/Release/ce_dlss_camera_tests.exe'
cases=0
with tempfile.TemporaryDirectory(prefix='ce-dlss-native-',dir=ROOT/'out') as folder:
    fixture=Path(folder)/'camera.bin'
    for position in [(0,0,0),(132,-98,43)]:
        for yaw,pitch in [(0,0),(.7,-.3),(-1.9,.6)]:
            forward,up=(0,0,1),(0,1,0)
            for axis,angle in [((0,1,0),yaw),((1,0,0),pitch)]:
                forward,up=rotate(forward,axis,angle),rotate(up,axis,angle)
            native.build(position,cross(forward,up),up,forward,width=1920,height=1080)
            native.upload()
            fixture.write_bytes(bytes(native.machine.mem_read(CAMERA,0x398))+
                bytes(native.machine.mem_read(SHADER_DATA+0x240,16))+
                bytes(native.machine.mem_read(SHADER_DATA+0x270,64)))
            subprocess.run([str(exe),'--native',str(fixture)],check=True)
            cases+=1
print(json.dumps({'result':'PASS_OFFLINE_ONLY','native_camera_cases':cases,
    'native_instructions':native.instructions,'projection':'actual native constant writer to production DLSS decoder'}))
