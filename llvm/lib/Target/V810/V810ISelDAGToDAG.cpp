#include "V810TargetMachine.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
using namespace llvm;

#define DEBUG_TYPE "v810-isel"
#define PASS_NAME "V810 DAG-> DAG Pattern Instruction Selection"

namespace {
class V810DAGToDAGISel : public SelectionDAGISel {
  /// Subtarget - Keep a pointer to the Sparc Subtarget around so that we can
  /// make the right decision when generating code for different targets.
  const V810Subtarget *Subtarget = nullptr;
public:
  static char ID;

  V810DAGToDAGISel() = delete;

  explicit V810DAGToDAGISel(V810TargetMachine &tm) : SelectionDAGISel(ID, tm) {}

  StringRef getPassName() const override { return PASS_NAME; }

  bool runOnMachineFunction(MachineFunction &MF) override {
    Subtarget = &MF.getSubtarget<V810Subtarget>();
    return SelectionDAGISel::runOnMachineFunction(MF);
  }

  void Select(SDNode *N) override;

  // Complex Pattern Selectors.
  bool SelectADDRri(SDValue N, SDValue &Base, SDValue &Offset);

  // Include the pieces autogenerated from the target description.
#include "V810GenDAGISel.inc"
};
} // end anonymous namespace

char V810DAGToDAGISel::ID = 0;

INITIALIZE_PASS(V810DAGToDAGISel, DEBUG_TYPE, PASS_NAME, false, false)

bool V810DAGToDAGISel::SelectADDRri(SDValue Addr,
                                    SDValue &Base, SDValue &Offset) {
  if (FrameIndexSDNode *FIN = dyn_cast<FrameIndexSDNode>(Addr)) {
    Base = CurDAG->getTargetFrameIndex(
      FIN->getIndex(), TLI->getPointerTy(CurDAG->getDataLayout()));
    Offset = CurDAG->getTargetConstant(0, SDLoc(Addr), MVT::i32);
    return true;
  }
  if (Addr.getOpcode() == ISD::TargetExternalSymbol ||
      Addr.getOpcode() == ISD::TargetGlobalAddress ||
      Addr.getOpcode() == ISD::TargetGlobalTLSAddress)
    return false; // direct calls.
  if (Addr.getOpcode() == V810ISD::REG_RELATIVE) {
    // This address is directly expressible as a register plus a 16-bit immediate offset.
    Base = Addr.getOperand(1);
    Offset = Addr.getOperand(0);
    return true;
  }
  if (CurDAG->isBaseWithConstantOffset(Addr)) {
    ConstantSDNode *CN = cast<ConstantSDNode>(Addr.getOperand(1));
    if (isInt<16>(CN->getSExtValue())) {
      if (FrameIndexSDNode *FIN =
              dyn_cast<FrameIndexSDNode>(Addr.getOperand(0))) {
        // Constant offset from frame ref.
        Base = CurDAG->getTargetFrameIndex(FIN->getIndex(), Addr->getValueType(0));
      } else {
        Base = Addr.getOperand(0);
      }
      Offset =
          CurDAG->getTargetConstant(CN->getSExtValue(), SDLoc(Addr), MVT::i32);
      return true;
    }
  }
  if (ConstantSDNode *CN = dyn_cast<ConstantSDNode>(Addr)) {
    assert(isInt<32>(CN->getSExtValue()));
    // When working with constant addresses, MOVHI the high bits and stick the low bits in the offset
    uint64_t lo = EvalLo(CN->getSExtValue());
    uint64_t hi = EvalHi(CN->getSExtValue());
    if (hi == 0) {
      Base = CurDAG->getRegister(V810::R0, MVT::i32);
    } else {
      SDValue Ops[] = {CurDAG->getRegister(V810::R0, MVT::i32), CurDAG->getTargetConstant(hi, SDLoc(Addr), MVT::i32)};
      Base = SDValue(CurDAG->getMachineNode(V810::MOVHI, SDLoc(Addr), MVT::i32, Ops), 0);
    }
    Offset = CurDAG->getTargetConstant(lo, SDLoc(Addr), MVT::i32);
    return true;
  }
  Base = Addr;
  Offset = CurDAG->getTargetConstant(0, SDLoc(Addr), MVT::i32);
  return true;
}

void V810DAGToDAGISel::Select(SDNode *N) {
  if (N->isMachineOpcode()) {
    N->setNodeId(-1);
    return; // Already selected.
  }

  SelectCode(N);
}

FunctionPass *llvm::createV810IselDag(V810TargetMachine &TM) {
  return new V810DAGToDAGISel(TM);
}