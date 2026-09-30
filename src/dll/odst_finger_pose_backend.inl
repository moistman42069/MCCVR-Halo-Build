#include "../common/odst_finger_pose_logic.h"
namespace odst_fingers {
enum class Result {NotApplicable,Applied,Refused,Faulted};
// Called only after native FP palette completion. Native wrists/arms/guns
// remain unchanged; a complete private finger candidate precedes any write.
__declspec(noinline) Result Apply(Matrix* destination,const Pose& inverseBind,const Request& request) noexcept {
    AnatomicalPalmMarkers markers{};
    if(!destination||request.count!=Count||
       !OdstAnatomicalPalmMarkers(request.checksum,int(request.count),markers))return Result::NotApplicable;
    Pose original{},candidate{};volatile bool fault=false,writing=false;bool solved=false;
    __try {
        std::memcpy(original.data(),destination,sizeof(original));
        solved=Solve(original,inverseBind,request,candidate);
        if(solved) {
            writing=true;
            for(unsigned node=0;node<Count;++node)
                if((request.allowed[0]&&request.fingers[0].valid&&IsFinger(node,0))||
                   (request.allowed[1]&&request.fingers[1].valid&&IsFinger(node,1)))
                    std::memcpy(destination+node,&candidate[node],sizeof(Matrix));
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){fault=true;}
    if(fault) {
        if(writing) {
            // Immediate same-thread rollback, before returning to a native
            // consumer. Skip unchanged read-only bytes after a partial store.
            __try {
                for(unsigned node=0;node<Count;++node)
                    if((request.allowed[0]&&request.fingers[0].valid&&IsFinger(node,0))||
                       (request.allowed[1]&&request.fingers[1].valid&&IsFinger(node,1))) {
                        auto* current=reinterpret_cast<volatile uint8_t*>(destination+node);
                        const auto* before=reinterpret_cast<const uint8_t*>(&original[node]);
                        for(unsigned byte=0;byte<sizeof(Matrix);++byte)if(current[byte]!=before[byte])current[byte]=before[byte];
                    }
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
        return Result::Faulted;
    }
    return solved?Result::Applied:Result::Refused;
}
}
