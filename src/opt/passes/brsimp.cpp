#include <cassert>

#include "opt/pass.h"
#include "opt/passman.h"

using namespace mimic::mid;
using namespace mimic::opt;

namespace {

/*
  simplify branch instructions
  this pass will:
  1.  replace branch with two equal targets to jump
  2.  replace branch with constant condition to jump
*/
class BranchSimplifyPass : public BlockPass {
 public:
  BranchSimplifyPass() {}

  bool RunOnBlock(const BlockPtr &block) override {
    cur_block_ = block.get();
    // scan the last instruction
    replace_ = false;
    block->insts().back()->RunPass(*this);
    // if can be replaced
    if (replace_) {
      // create jump instruction
      auto jump = std::make_shared<JumpSSA>(target_);
      jump->set_logger(block->insts().back()->logger());
      // replace last instruction with jump
      block->insts().back() = jump;
    }
    // release values
    cond_ = nullptr;
    target_ = nullptr;
    opr_val_ = nullptr;
    opr_block_ = nullptr;
    return replace_;
  }

  void RunOn(BranchSSA &ssa) override {
    if (scan_phi_) return;
    // record condition
    cond_ = ssa[0].value();
    // check if can be replaced
    if (ssa[1].value() == ssa[2].value()) {
      replace_ = true;
      target_ = ssa[1].value();
    }
    else if (cond_->IsConst()) {
      // mark as replaced
      replace_ = true;
      cond_->RunPass(*this);
      target_ = val_ ? ssa[1].value() : ssa[2].value();
      // remove current block from non-target block's predecessor list
      const auto &block = !val_ ? ssa[1].value() : ssa[2].value();
      auto ptr = static_cast<BlockSSA *>(block.get());
      ptr->RemoveValue(cur_block_);
      // check & handle phi nodes in non-target block
      scan_phi_ = true;
      for (const auto &i : ptr->insts()) i->RunPass(*this);
      scan_phi_ = false;
    }
  }

  void RunOn(ConstIntSSA &ssa) override {
    // method will be called if value is a condition
    val_ = ssa.value();
  }

  void RunOn(PhiSSA &ssa) override {
    // scan operands
    for (auto &&i : ssa) {
      i.value()->RunPass(*this);
      // remove operand from block where branch instrution located
      if (opr_block_.get() == cur_block_) {
        i.set_value(nullptr);
      }
    }
    // rearrange operands
    ssa.RemoveValue(nullptr);
    // check if phi node can be eliminate
    if (ssa.size() == 1) {
      ssa[0].value()->RunPass(*this);
      // replace phi node with operand's value
      ssa.ReplaceBy(opr_val_);
    }
  }

  void RunOn(PhiOperandSSA &ssa) override {
    opr_val_ = ssa[0].value();
    opr_block_ = ssa[1].value();
  }

 private:
  bool replace_, scan_phi_;
  BlockSSA *cur_block_;
  // used when scanning branch instructions
  SSAPtr cond_, target_;
  std::uint32_t val_;
  // used when scanning phi nodes
  SSAPtr opr_val_, opr_block_;
};

}  // namespace

// register current pass
REGISTER_PASS(BranchSimplifyPass, branch_simp, 1, false);
