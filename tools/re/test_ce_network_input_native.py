"""Run production-generated CE actions through pinned native submission and aim.
Read-only PE emulation; no installed files or live game process are touched.
Pass the vectors file emitted by halomccvr_ce_network_input_tests.exe.
"""
import argparse,hashlib,math,struct
from pathlib import Path
from test_ce_unit_control_native import Machine,BASE,HEAP,OWNER,SHA,ROOT,require,pefile
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_RCX,UC_X86_REG_RDX

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('vectors',type=Path)
    args=parser.parse_args()
    raw=(ROOT/'out/deps/re-tools/inputs/halo1.dll').read_bytes()
    require(hashlib.sha256(raw).hexdigest().upper()==SHA,'pinned CE image mismatch')
    image=pefile.PE(data=raw,fast_load=True).get_memory_mapped_image()
    machine=Machine(image);uc=machine.uc
    source=HEAP+0x20000;actions=HEAP+0x21000
    def native_assist_stub(uc,address,size,user):
        if address==BASE+0xb69d84:
            # Model only the separate assist encoder's read/write ABI. All
            # action direction/flags/throttle copies execute native instructions.
            uc.mem_write(uc.reg_read(UC_X86_REG_RDX),bytes(uc.mem_read(uc.reg_read(UC_X86_REG_RCX),16)))
            machine.ret()
    uc.hook_add(UC_HOOK_CODE,native_assist_stub)
    data=args.vectors.read_bytes();record=0x30*2+12
    require(data and len(data)%record==0,'production vector file malformed')
    count=0
    for start in range(0,len(data),record):
        before,after=data[start:start+0x30],data[start+0x30:start+0x60]
        expected=struct.unpack_from('<3f',data,start+0x60)
        # Build a native control record whose outgoing direction and throttle
        # match the production adapter's input; verify exact creator layout.
        control=bytearray(0x40)
        control[0:4]=before[0:4];control[4:6]=before[0x1e:0x20]
        control[8:16]=before[4:12];control[16:28]=before[12:24]
        control[28:34]=before[24:30];control[36:52]=before[32:48]
        uc.mem_write(source,bytes(control));uc.mem_write(actions,b'\x99'*0xc0)
        machine.call(0xa9a8a4,RCX=source,RDX=actions)
        require(uc.mem_read(actions,0x30)==before,'native action creator ABI differs')
        require(uc.mem_read(source,len(control))==control,'native input controls changed')
        # Both role inputs are identical before native simulation. Actual MCC
        # submission copies all four actions into its native queued frame.
        for connection in (1,2):
            machine.write(BASE+0x2b236e0,'h',connection)
            machine.write(BASE+0x1b7b630,'B',1)
            uc.mem_write(actions,after+bytes(0x90))
            machine.call(0xb7cf44,RCX=1,RDX=actions)
            queued=BASE+0x2c983d4
            require(uc.mem_read(queued,0xc0)==after+bytes(0x90),'native action submission changed tracked data')
            machine.call(0xad0304,RCX=0,RDX=queued+4,R8=machine.result)
            actual=machine.read(machine.result,'3f')
            require(all(abs(a-b)<2e-6 for a,b in zip(actual,expected)),'native decoded tracked aim differs')
        yaw,pitch=struct.unpack_from('<2f',after,4)
        require(0<=yaw<2*math.pi+1e-6 and abs(pitch)<=math.pi/2+1e-6,'invalid action angle domain')
        count+=1
    print(f'PASS: {count} production vectors through native action creator, {count*2} client/server submissions and native angle/seat conversion.')
    print('LIMIT: assist encoder is modeled; this does not emulate transport latency, complete prediction, animation, collision or a headset.')

if __name__=='__main__':main()
